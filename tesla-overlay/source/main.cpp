// SPDX-License-Identifier: GPL-2.0-or-later

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include "config.hpp"
#include "ipc_client.hpp"
#include "settings_format.hpp"
#include "tesla_exit_intent.hpp"

#include <atomic>
#include <cstdio>
#include <limits>
#include <mutex>

namespace {

FsFileSystem g_sd{};
bool g_sdOpen = false;
swots::settings::Document g_committed{};
std::mutex g_stateMutex;
bool g_settingsTransactionOpen = false;
bool g_settingsTransactionCanceled = false;
char g_notice[24]{};

swots::ipc::ClientWorker g_ipc;
bool g_workerStarted = false;
// onHide may run on libtesla's background event thread for Home, Power, and
// shortcut dismissal while the remaining lifecycle callbacks use the UI loop.
std::atomic<bool> g_enabled{false};
bool g_fault = false;
u64 g_ownerEpoch = 0;
u64 g_lifecycleSequence = 0;
std::atomic<u64> g_frontendGeneration{1};
std::atomic<u64> g_resumeGeneration{0};
tesla_lifecycle::ExitIntentState g_exitIntent;

enum class BackendStatus { Running, Absent, Error };
enum class SaveOutcome { Committed, RecoveredOld, Unknown };

bool fileExists(const char *path) {
    if (!g_sdOpen) return false;
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file)))
        return false;
    fsFileClose(&file);
    return true;
}

bool readDocument(const char *path, swots::settings::Document *document) {
    if (!g_sdOpen || document == nullptr) return false;
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file)))
        return false;
    s64 size = 0;
    Result rc = fsFileGetSize(&file, &size);
    char text[swots::settings::kMaxTextSize]{};
    if (R_SUCCEEDED(rc) && size > 0 &&
        size <= static_cast<s64>(sizeof(text))) {
        u64 bytesRead = 0;
        rc = fsFileRead(&file, 0, text, static_cast<u64>(size),
                        FsReadOption_None, &bytesRead);
        if (R_SUCCEEDED(rc) && bytesRead == static_cast<u64>(size)) {
            const bool parsed = swots::settings::parseDocument(
                text, static_cast<std::size_t>(size),
                swots::settings::Values{}, document);
            fsFileClose(&file);
            return parsed;
        }
    }
    fsFileClose(&file);
    return false;
}

bool selectDiskDocument(bool duringSave, u64 submittedRevision,
                        bool submittedDurable,
                        swots::settings::Document *selected) {
    constexpr const char *paths[] = {
        SWOTS_SETTINGS_FILE,
        SWOTS_SETTINGS_TEMP_FILE,
        SWOTS_SETTINGS_BACKUP_FILE,
    };
    swots::settings::Candidate candidates[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        candidates[i].source =
            static_cast<swots::settings::CandidateSource>(i);
        candidates[i].valid = readDocument(paths[i], &candidates[i].document);
        candidates[i].durabilityEligible =
            candidates[i].valid &&
            (!duringSave ||
             candidates[i].document.revision != submittedRevision ||
             submittedDurable);
    }
    const auto *winner = swots::settings::selectCandidate(
        candidates, 3, duringSave);
    if (winner == nullptr) return false;
    *selected = winner->document;
    return true;
}

void loadSettings() {
    g_committed = {};
    swots::settings::Document selected{};
    if (selectDiskDocument(false, 0, true, &selected))
        g_committed = selected;
}

SaveOutcome saveSettings(const swots::settings::Values &draft,
                         swots::settings::Document *selected) {
    if (!g_sdOpen)
        return SaveOutcome::Unknown;
    if (g_committed.revision == std::numeric_limits<u64>::max()) {
        *selected = g_committed;
        return SaveOutcome::RecoveredOld;
    }

    swots::settings::Document submitted{};
    submitted.values = draft;
    submitted.revision = g_committed.revision + 1;
    submitted.version = 2;
    char text[swots::settings::kMaxTextSize]{};
    const std::size_t size = swots::settings::serializeV2(
        submitted, text, sizeof(text));
    if (size == 0) return SaveOutcome::Unknown;

    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);
    bool submittedDurable = false;
    Result rc = 0;
    if (fileExists(SWOTS_SETTINGS_TEMP_FILE))
        rc = fsFsDeleteFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE);
    if (R_SUCCEEDED(rc))
        rc = fsFsCreateFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                            static_cast<s64>(size), 0);
    FsFile file{};
    if (R_SUCCEEDED(rc))
        rc = fsFsOpenFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                          FsOpenMode_Write, &file);
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&file, 0, text, size, FsWriteOption_Flush);
        fsFileClose(&file);
    }
    if (R_SUCCEEDED(rc)) {
        rc = fsFsCommit(&g_sd);
        submittedDurable = R_SUCCEEDED(rc);
    }

    const bool hadPrimary = fileExists(SWOTS_SETTINGS_FILE);
    if (R_SUCCEEDED(rc) && hadPrimary) {
        if (fileExists(SWOTS_SETTINGS_BACKUP_FILE)) {
            rc = fsFsDeleteFile(&g_sd, SWOTS_SETTINGS_BACKUP_FILE);
            if (R_SUCCEEDED(rc)) rc = fsFsCommit(&g_sd);
        }
        if (R_SUCCEEDED(rc))
            rc = fsFsRenameFile(&g_sd, SWOTS_SETTINGS_FILE,
                                SWOTS_SETTINGS_BACKUP_FILE);
        if (R_SUCCEEDED(rc)) rc = fsFsCommit(&g_sd);
    }
    if (R_SUCCEEDED(rc))
        rc = fsFsRenameFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                            SWOTS_SETTINGS_FILE);
    if (R_SUCCEEDED(rc)) rc = fsFsCommit(&g_sd);

    swots::settings::Document recovered{};
    if (!selectDiskDocument(true, submitted.revision, submittedDurable,
                            &recovered))
        return SaveOutcome::Unknown;
    *selected = recovered;
    return recovered.revision == submitted.revision
               ? SaveOutcome::Committed
               : SaveOutcome::RecoveredOld;
}

BackendStatus queryBackendStatus() {
    u64 pid = 0;
    const Result rc = pmdmntGetProcessId(&pid, SWOTS_TITLE_ID);
    if (R_SUCCEEDED(rc))
        return pid != 0 ? BackendStatus::Running : BackendStatus::Absent;
    if (R_VALUE(rc) == 0x20F) return BackendStatus::Absent;
    return BackendStatus::Error;
}

bool confirmBackendAbsent(u64 timeoutNs) {
    const u64 deadline = armGetSystemTick() + armNsToTicks(timeoutNs);
    do {
        const BackendStatus status = queryBackendStatus();
        if (status == BackendStatus::Absent) return true;
        if (status == BackendStatus::Error) return false;
        svcSleepThread(20'000'000ULL);
    } while (armGetSystemTick() < deadline);
    return queryBackendStatus() == BackendStatus::Absent;
}

bool terminateAndConfirm() {
    const BackendStatus status = queryBackendStatus();
    if (status == BackendStatus::Absent) return true;
    if (status == BackendStatus::Error) return false;
    const Result rc = pmshellTerminateProgram(SWOTS_TITLE_ID);
    if (R_FAILED(rc) && R_VALUE(rc) != 0x20F) return false;
    return confirmBackendAbsent(1'000'000'000ULL);
}

void retireWorkerAfterDeath() {
    g_ipc.invalidate();
    if (g_workerStarted && !g_ipc.shutdown()) {
        g_fault = true;
        return;
    }
    g_workerStarted = false;
}

bool recoverToOff() {
    g_frontendGeneration.fetch_add(1, std::memory_order_acq_rel);
    g_ipc.invalidate();
    const bool absent = terminateAndConfirm();
    if (absent) {
        retireWorkerAfterDeath();
        g_fault = g_workerStarted;
        if (!g_fault) g_enabled = false;
    } else {
        g_fault = true;
    }
    return absent;
}

Result ensureWorker() {
    // The worker is intentionally started before the renderer. It does not
    // own a Service until Connect runs after pmshellLaunchProgram.
    if (g_workerStarted) return 0;
    Result rc = g_ipc.start();
    if (R_SUCCEEDED(rc)) g_workerStarted = true;
    return rc;
}

Result launchBackend() {
    if (queryBackendStatus() == BackendStatus::Running &&
        !terminateAndConfirm())
        return KERNELRESULT(TimedOut);
    const NcmProgramLocation location{
        .program_id = SWOTS_TITLE_ID,
        .storageID = NcmStorageId_None,
    };
    u64 pid = 0;
    return pmshellLaunchProgram(0, &location, &pid);
}

u64 nextSequence() {
    if (g_lifecycleSequence == std::numeric_limits<u64>::max()) return 0;
    return ++g_lifecycleSequence;
}

u64 runtimeRevision() {
    return g_committed.revision;
}

void cleanupLegacyFiles() {
    if (!g_sdOpen) return;
    constexpr const char *paths[] = {
        SWOTS_ENABLED_FILE,
        SWOTS_TESLA_LIFECYCLE_FILE,
        SWOTS_TESLA_LIFECYCLE_TEMP_FILE,
        SWOTS_TESLA_LIFECYCLE_ACK_FILE,
        SWOTS_TESLA_LIFECYCLE_ACK_TEMP_FILE,
    };
    for (const char *path : paths)
        if (fileExists(path)) fsFsDeleteFile(&g_sd, path);
    fsFsCommit(&g_sd);
}

Result enableCues() {
    Result rc = ensureWorker();
    if (R_SUCCEEDED(rc)) rc = launchBackend();
    if (R_SUCCEEDED(rc)) rc = g_ipc.connect(1'000'000'000ULL);
    const u64 claimSequence = nextSequence();
    if (R_SUCCEEDED(rc) && claimSequence != 0)
        rc = g_ipc.claimAndSuspend(g_ownerEpoch, claimSequence,
                                   1'000'000'000ULL);
    const u64 revision = runtimeRevision();
    if (R_SUCCEEDED(rc))
        rc = g_ipc.setConfig(g_ownerEpoch, revision,
                             swots::ipc::toWire(g_committed.values),
                             1'000'000'000ULL);
    if (R_FAILED(rc) || claimSequence == 0) {
        recoverToOff();
        return R_FAILED(rc) ? rc : swots::ipc::ResultWorkerUnavailable;
    }
    cleanupLegacyFiles();
    g_enabled = true;
    g_fault = false;
    return 0;
}

bool stopCues() {
    Result rc = 0;
    if (g_enabled && g_workerStarted) {
        const u64 suspendSequence = nextSequence();
        rc = suspendSequence == 0
                 ? swots::ipc::ResultWorkerUnavailable
                 : g_ipc.claimAndSuspend(g_ownerEpoch, suspendSequence,
                                         1'000'000'000ULL);
        const u64 stopSequence = nextSequence();
        if (R_SUCCEEDED(rc))
            rc = stopSequence == 0
                     ? swots::ipc::ResultWorkerUnavailable
                     : g_ipc.requestStop(g_ownerEpoch, stopSequence,
                                         1'000'000'000ULL);
        if (R_SUCCEEDED(rc) && confirmBackendAbsent(1'000'000'000ULL)) {
            retireWorkerAfterDeath();
            g_fault = g_workerStarted;
            if (!g_fault) g_enabled = false;
            return !g_fault;
        }
    }
    return recoverToOff();
}

void setNotice(const char *text) {
    const std::lock_guard lock(g_stateMutex);
    std::snprintf(g_notice, sizeof(g_notice), "%s", text ? text : "");
}

void requestResumeGame() {
    g_resumeGeneration.store(
        g_frontendGeneration.load(std::memory_order_acquire),
        std::memory_order_release);
    g_exitIntent.requestResumeGame();
}

class SettingsGui final : public tsl::Gui {
public:
    SettingsGui() {
        const std::lock_guard lock(g_stateMutex);
        m_draft = g_committed.values;
        g_settingsTransactionOpen = true;
        g_settingsTransactionCanceled = false;
    }

    ~SettingsGui() override {
        const std::lock_guard lock(g_stateMutex);
        g_settingsTransactionOpen = false;
        g_settingsTransactionCanceled = false;
    }

    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("SWOTS", "Settings");
        auto *list = new tsl::elm::List();

        m_opacityHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_opacityHeader);
        auto *opacity = new tsl::elm::TrackBar("O");
        opacity->setProgress(
            swots::settings::opacityToPercent(m_draft.opacity));
        opacity->setValueChangedListener([this](u8 progress) {
            m_draft.opacity = swots::settings::percentToOpacity(progress);
            updateLabels();
        });
        list->addItem(opacity);

        m_radiusHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_radiusHeader);
        constexpr u8 radiusSteps = swots::settings::kMaxDotRadius -
                                   swots::settings::kMinDotRadius + 1;
        auto *radius = new tsl::elm::StepTrackBar("R", radiusSteps);
        radius->setProgress(
            swots::settings::dotRadiusToStep(m_draft.dotRadius));
        radius->setValueChangedListener([this](u8 step) {
            m_draft.dotRadius = swots::settings::stepToDotRadius(step);
            updateLabels();
        });
        list->addItem(radius);

        m_sensitivityHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_sensitivityHeader);
        auto *sensitivity = new tsl::elm::TrackBar("S");
        sensitivity->setProgress(m_draft.sensitivity);
        sensitivity->setValueChangedListener([this](u8 progress) {
            m_draft.sensitivity = progress;
            updateLabels();
        });
        list->addItem(sensitivity);

        m_smoothingHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_smoothingHeader);
        auto *smoothing = new tsl::elm::TrackBar("M");
        smoothing->setProgress(m_draft.smoothing);
        smoothing->setValueChangedListener([this](u8 progress) {
            m_draft.smoothing = progress;
            updateLabels();
        });
        list->addItem(smoothing);
        list->addItem(new tsl::elm::CustomDrawer(
            [this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32, s32) {
                const char *text = m_saveFailed ? "Couldn't save"
                                                : "A: Save";
                renderer->drawString(
                    text, false, x + 8, y + 22, 15,
                    renderer->a(tsl::style::color::ColorDescription));
            }), 42);
        updateLabels();
        frame->setContent(list);
        return frame;
    }

    void update() override {
        bool canceled = false;
        {
            const std::lock_guard lock(g_stateMutex);
            canceled = g_settingsTransactionCanceled;
        }
        if (canceled) tsl::goBack();
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &,
                     HidAnalogStickState, HidAnalogStickState) override {
        if (!(keysDown & HidNpadButton_A)) return false;
        {
            const std::lock_guard lock(g_stateMutex);
            if (g_settingsTransactionCanceled) return true;
        }
        swots::settings::Document selected{};
        const SaveOutcome outcome = saveSettings(m_draft, &selected);
        if (outcome != SaveOutcome::Committed) {
            m_saveFailed = true;
            if (outcome == SaveOutcome::RecoveredOld) {
                g_committed = selected;
                if (g_enabled) {
                    const u64 revision = runtimeRevision();
                    if (R_FAILED(g_ipc.setConfig(
                            g_ownerEpoch, revision,
                            swots::ipc::toWire(g_committed.values),
                            1'000'000'000ULL)))
                        recoverToOff();
                }
            } else {
                stopCues();
            }
            return true;
        }

        g_committed = selected;
        if (g_enabled) {
            const u64 revision = runtimeRevision();
            const Result rc = g_ipc.setConfig(
                                        g_ownerEpoch, revision,
                                        swots::ipc::toWire(g_committed.values),
                                        1'000'000'000ULL);
            if (R_FAILED(rc)) recoverToOff();
        }
        setNotice("Saved");
        tsl::goBack();
        return true;
    }

private:
    void updateLabels() {
        char label[40];
        std::snprintf(label, sizeof(label), "Opacity  %u%%",
                      static_cast<unsigned>(
                          swots::settings::opacityToPercent(m_draft.opacity)));
        m_opacityHeader->setText(label);
        std::snprintf(label, sizeof(label), "Dot radius  %u px",
                      static_cast<unsigned>(m_draft.dotRadius));
        m_radiusHeader->setText(label);
        std::snprintf(label, sizeof(label), "Sensitivity  %u%%",
                      static_cast<unsigned>(m_draft.sensitivity));
        m_sensitivityHeader->setText(label);
        std::snprintf(label, sizeof(label), "Smoothing  %u%%",
                      static_cast<unsigned>(m_draft.smoothing));
        m_smoothingHeader->setText(label);
    }

    swots::settings::Values m_draft{};
    tsl::elm::CategoryHeader *m_opacityHeader = nullptr;
    tsl::elm::CategoryHeader *m_radiusHeader = nullptr;
    tsl::elm::CategoryHeader *m_sensitivityHeader = nullptr;
    tsl::elm::CategoryHeader *m_smoothingHeader = nullptr;
    bool m_saveFailed = false;
};

class MainGui final : public tsl::Gui {
public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("SWOTS", APP_VERSION);
        auto *list = new tsl::elm::List();
        m_toggle = new tsl::elm::ToggleListItem("Motion cues", g_enabled);
        m_toggle->setStateChangedListener([this](bool enabled) {
            if (enabled) {
                const Result rc = enableCues();
                if (R_FAILED(rc)) {
                    m_toggle->setState(false);
                    setNotice(rc == swots::ipc::ResultVersionMismatch
                                  ? "Restart required"
                                  : "Couldn't start");
                    return;
                }
                setNotice("");
                requestResumeGame();
                tsl::Overlay::get()->hide();
                return;
            }
            if (!stopCues()) {
                m_toggle->setState(true);
                setNotice("Couldn't stop");
                return;
            }
            setNotice("");
        });

        list->addItem(
            new tsl::elm::CategoryHeader("Vehicle motion cues", true));
        list->addItem(m_toggle);
        auto *settings = new tsl::elm::ListItem("Settings", ">");
        settings->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<SettingsGui>();
            return true;
        });
        list->addItem(settings);
        auto *teslaMenu =
            new tsl::elm::ListItem("Stop and return to Tesla", ">");
        teslaMenu->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (!stopCues()) {
                setNotice("Couldn't stop");
                return true;
            }
            g_exitIntent.requestReturnParent();
            tsl::Overlay::get()->close();
            return true;
        });
        list->addItem(teslaMenu);
        list->addItem(new tsl::elm::CustomDrawer(
            [](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32, s32) {
                char notice[sizeof(g_notice)]{};
                {
                    const std::lock_guard lock(g_stateMutex);
                    std::snprintf(notice, sizeof(notice), "%s", g_notice);
                }
                if (notice[0] != '\0')
                    renderer->drawString(
                        notice, false, x + 8, y + 22, 15,
                        renderer->a(tsl::style::color::ColorDescription));
            }), 42);
        frame->setContent(list);
        return frame;
    }

    void update() override {
        if (++m_counter % 20 != 0 || !m_toggle) return;
        if (g_enabled && queryBackendStatus() == BackendStatus::Absent) {
            g_enabled = false;
            retireWorkerAfterDeath();
        }
        m_toggle->setState(g_enabled);
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &,
                     HidAnalogStickState, HidAnalogStickState) override {
        if (!(keysDown & HidNpadButton_B)) return false;
        if (g_exitIntent.current() ==
            tesla_lifecycle::ExitIntent::ReturnParent)
            return true;
        if (g_enabled) requestResumeGame();
        tsl::Overlay::get()->hide();
        return true;
    }

private:
    tsl::elm::ToggleListItem *m_toggle = nullptr;
    u32 m_counter = 0;
};

class SWOTSOverlay final : public tsl::Overlay {
public:
    void initServices() override {
        pmshellInitialize();
        pmdmntInitialize();
        g_sdOpen = R_SUCCEEDED(fsOpenSdCardFileSystem(&g_sd));
        g_ownerEpoch = randomGet64();
        if (g_ownerEpoch == 0) g_ownerEpoch = 1;
        g_lifecycleSequence = randomGet64() & 0x3fffffffffffffffULL;
        loadSettings();
        if (R_SUCCEEDED(g_ipc.start())) g_workerStarted = true;
    }

    void exitServices() override {
        if (g_workerStarted) g_ipc.shutdown();
        g_workerStarted = false;
        if (g_sdOpen) fsFsClose(&g_sd);
        g_sdOpen = false;
        pmdmntExit();
        pmshellExit();
    }

    bool onBeforeForegroundAcquire() override {
        g_exitIntent.reset();
        if (!g_enabled) {
            const BackendStatus status = queryBackendStatus();
            return status == BackendStatus::Absent || terminateAndConfirm();
        }
        const u64 sequence = nextSequence();
        if (sequence != 0 &&
            R_SUCCEEDED(g_ipc.claimAndSuspend(
                g_ownerEpoch, sequence, 1'000'000'000ULL)))
            return true;
        return recoverToOff();
    }

    void onShow() override { g_exitIntent.reset(); }

    void onHide() override {
        {
            const std::lock_guard lock(g_stateMutex);
            if (g_settingsTransactionOpen)
                g_settingsTransactionCanceled = true;
        }
        if (g_enabled) requestResumeGame();
    }

    void onForegroundReleased() override {
        const auto intent = g_exitIntent.consume();
        if (intent != tesla_lifecycle::ExitIntent::ResumeGame || !g_enabled)
            return;
        if (g_resumeGeneration.load(std::memory_order_acquire) !=
            g_frontendGeneration.load(std::memory_order_acquire))
            return;
        const u64 sequence = nextSequence();
        const u64 revision = runtimeRevision();
        if (sequence == 0 ||
            R_FAILED(g_ipc.resume(
                g_ownerEpoch, sequence, revision,
                swots::ipc::toWire(g_committed.values),
                1'000'000'000ULL)))
            recoverToOff();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

} // namespace

int main(int argc, char **argv) {
    return tsl::loop<SWOTSOverlay>(argc, argv);
}
