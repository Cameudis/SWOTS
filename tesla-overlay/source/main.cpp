// SPDX-License-Identifier: GPL-2.0-or-later

#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include "config.hpp"
#include "settings_format.hpp"
#include "tesla_exit_intent.hpp"
#include "tesla_lifecycle.hpp"

#include <cstdio>
#include <mutex>

namespace {

FsFileSystem g_sd{};
bool g_sdOpen = false;
swots::settings::Values g_settings{};
bool g_settingsDirty = false;
std::recursive_mutex g_stateMutex;
u64 g_lifecycleSession = 0;
u64 g_lifecycleGeneration = 0;
tesla_lifecycle::ExitIntentState g_exitIntent;

bool fileExists(const char *path) {
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file))) return false;
    fsFileClose(&file);
    return true;
}

bool readSettingsFile(const char *path, swots::settings::Values *values) {
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, path, FsOpenMode_Read, &file))) return false;

    s64 size = 0;
    Result rc = fsFileGetSize(&file, &size);
    char text[swots::settings::kMaxTextSize]{};
    if (R_SUCCEEDED(rc) && size > 0 &&
        size <= static_cast<s64>(sizeof(text))) {
        u64 bytesRead = 0;
        rc = fsFileRead(&file, 0, text, static_cast<u64>(size),
                        FsReadOption_None, &bytesRead);
        if (R_SUCCEEDED(rc) && bytesRead == static_cast<u64>(size)) {
            const auto defaults = swots::settings::Values{};
            const bool parsed = swots::settings::parse(
                text, static_cast<std::size_t>(size), defaults, values);
            fsFileClose(&file);
            return parsed;
        }
    }

    fsFileClose(&file);
    return false;
}

void loadSettings() {
    const std::lock_guard lock(g_stateMutex);
    g_settings = {};
    if (!g_sdOpen) return;
    if (!readSettingsFile(SWOTS_SETTINGS_FILE, &g_settings)) {
        // A power loss may leave a fully flushed temp document waiting for its
        // final rename, or the previous complete document in the backup.
        g_settingsDirty =
            readSettingsFile(SWOTS_SETTINGS_TEMP_FILE, &g_settings) ||
            readSettingsFile(SWOTS_SETTINGS_BACKUP_FILE, &g_settings);
        return;
    }
    g_settingsDirty = false;
}

Result flushSettings() {
    const std::lock_guard lock(g_stateMutex);
    if (!g_sdOpen || !g_settingsDirty) return 0;

    char text[swots::settings::kMaxTextSize]{};
    const std::size_t size = swots::settings::serialize(
        g_settings, text, sizeof(text));
    if (size == 0) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);
    if (fileExists(SWOTS_SETTINGS_TEMP_FILE))
        fsFsDeleteFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE);

    Result rc = fsFsCreateFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                               static_cast<s64>(size), 0);
    if (R_FAILED(rc)) return rc;

    FsFile file{};
    rc = fsFsOpenFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                      FsOpenMode_Write, &file);
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&file, 0, text, size, FsWriteOption_Flush);
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) {
        fsFsDeleteFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE);
        return rc;
    }
    rc = fsFsCommit(&g_sd);
    if (R_FAILED(rc)) return rc;

    const bool hadPrimary = fileExists(SWOTS_SETTINGS_FILE);
    if (hadPrimary) {
        if (fileExists(SWOTS_SETTINGS_BACKUP_FILE)) {
            rc = fsFsDeleteFile(&g_sd, SWOTS_SETTINGS_BACKUP_FILE);
            if (R_FAILED(rc)) return rc;
        }
        rc = fsFsRenameFile(&g_sd, SWOTS_SETTINGS_FILE,
                            SWOTS_SETTINGS_BACKUP_FILE);
        if (R_FAILED(rc)) return rc;
        rc = fsFsCommit(&g_sd);
        if (R_FAILED(rc)) return rc;
    }

    rc = fsFsRenameFile(&g_sd, SWOTS_SETTINGS_TEMP_FILE,
                        SWOTS_SETTINGS_FILE);
    if (R_FAILED(rc)) {
        if (hadPrimary && !fileExists(SWOTS_SETTINGS_FILE)) {
            fsFsRenameFile(&g_sd, SWOTS_SETTINGS_BACKUP_FILE,
                           SWOTS_SETTINGS_FILE);
            fsFsCommit(&g_sd);
        }
        return rc;
    }
    rc = fsFsCommit(&g_sd);
    if (R_SUCCEEDED(rc)) g_settingsDirty = false;
    return rc;
}

bool isEnabled() {
    const std::lock_guard lock(g_stateMutex);
    if (!g_sdOpen) return false;
    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file))) {
        return false;
    }
    fsFileClose(&file);
    return true;
}

Result setEnabled(bool enabled) {
    const std::lock_guard lock(g_stateMutex);
    if (!g_sdOpen) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);

    if (enabled) {
        FsFile file{};
        if (R_SUCCEEDED(fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file))) {
            fsFileClose(&file);
            return 0;
        }
        Result rc = fsFsCreateFile(&g_sd, SWOTS_ENABLED_FILE, 0, 0);
        if (R_FAILED(rc)) return rc;
    } else {
        FsFile file{};
        if (R_FAILED(fsFsOpenFile(&g_sd, SWOTS_ENABLED_FILE, FsOpenMode_Read, &file))) {
            return 0;
        }
        fsFileClose(&file);
        Result rc = fsFsDeleteFile(&g_sd, SWOTS_ENABLED_FILE);
        if (R_FAILED(rc)) return rc;
    }
    return fsFsCommit(&g_sd);
}

Result publishLifecycle(tesla_lifecycle::State state,
                        tesla_lifecycle::Record *published = nullptr) {
    const std::lock_guard lock(g_stateMutex);
    if (!g_sdOpen) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    fsFsCreateDirectory(&g_sd, "/config");
    fsFsCreateDirectory(&g_sd, SWOTS_CONFIG_DIR);

    tesla_lifecycle::Record record{};
    record.state = state;
    record.session = g_lifecycleSession;
    record.generation = ++g_lifecycleGeneration;

    if (fileExists(SWOTS_TESLA_LIFECYCLE_TEMP_FILE)) {
        fsFsDeleteFile(&g_sd, SWOTS_TESLA_LIFECYCLE_TEMP_FILE);
    }

    Result rc = fsFsCreateFile(&g_sd, SWOTS_TESLA_LIFECYCLE_TEMP_FILE,
                               sizeof(record), 0);
    if (R_FAILED(rc)) return rc;

    FsFile file{};
    rc = fsFsOpenFile(&g_sd, SWOTS_TESLA_LIFECYCLE_TEMP_FILE,
                      FsOpenMode_Write, &file);
    if (R_SUCCEEDED(rc)) {
        rc = fsFileWrite(&file, 0, &record, sizeof(record),
                         FsWriteOption_Flush);
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) return rc;
    rc = fsFsCommit(&g_sd);
    if (R_FAILED(rc)) return rc;

    if (fileExists(SWOTS_TESLA_LIFECYCLE_FILE)) {
        rc = fsFsDeleteFile(&g_sd, SWOTS_TESLA_LIFECYCLE_FILE);
        if (R_FAILED(rc)) return rc;
    }
    rc = fsFsRenameFile(&g_sd, SWOTS_TESLA_LIFECYCLE_TEMP_FILE,
                        SWOTS_TESLA_LIFECYCLE_FILE);
    if (R_FAILED(rc)) return rc;
    rc = fsFsCommit(&g_sd);
    if (R_SUCCEEDED(rc) && published != nullptr) *published = record;
    return rc;
}

Result publishLifecycleWithRetry(tesla_lifecycle::State state,
                                 tesla_lifecycle::Record *published = nullptr) {
    Result rc = 0;
    for (u32 attempt = 0; attempt < 3; ++attempt) {
        rc = publishLifecycle(state, published);
        if (R_SUCCEEDED(rc)) return rc;
        svcSleepThread(20'000'000ULL);
    }
    return rc;
}

bool lifecycleAckMatches(const tesla_lifecycle::Record &record) {
    const std::lock_guard lock(g_stateMutex);
    if (!g_sdOpen) return false;

    FsFile file{};
    if (R_FAILED(fsFsOpenFile(&g_sd, SWOTS_TESLA_LIFECYCLE_ACK_FILE,
                              FsOpenMode_Read, &file))) {
        return false;
    }

    tesla_lifecycle::Ack ack{};
    u64 bytesRead = 0;
    const Result rc = fsFileRead(&file, 0, &ack, sizeof(ack),
                                 FsReadOption_None, &bytesRead);
    fsFileClose(&file);
    return R_SUCCEEDED(rc) && bytesRead == sizeof(ack) &&
           tesla_lifecycle::valid(ack) && ack.session == record.session &&
           ack.generation == record.generation;
}

void requestResumeGame() {
    g_exitIntent.requestResumeGame();
}

enum class BackendStatus {
    Running,
    Absent,
    Error,
};

BackendStatus queryBackendStatus() {
    u64 pid = 0;
    const Result rc = pmdmntGetProcessId(&pid, SWOTS_TITLE_ID);
    if (R_SUCCEEDED(rc)) return pid != 0 ? BackendStatus::Running
                                        : BackendStatus::Absent;
    // pm:dmnt ProcessNotFound (module 15, description 1).
    if (R_VALUE(rc) == 0x20F) return BackendStatus::Absent;
    return BackendStatus::Error;
}

bool backendRunning() {
    return queryBackendStatus() == BackendStatus::Running;
}

bool stopUnresponsiveBackend() {
    const BackendStatus initial = queryBackendStatus();
    if (initial == BackendStatus::Absent) return true;
    if (initial == BackendStatus::Error) return false;

    Result rc = pmshellTerminateProgram(SWOTS_TITLE_ID);
    if (R_FAILED(rc) && R_VALUE(rc) != 0x20F) return false;
    for (u32 attempt = 0; attempt < 20; ++attempt) {
        const BackendStatus status = queryBackendStatus();
        if (status == BackendStatus::Absent) return true;
        svcSleepThread(50'000'000ULL);
    }
    return queryBackendStatus() == BackendStatus::Absent;
}

Result launchBackend() {
    if (backendRunning()) {
        Result rc = pmshellTerminateProgram(SWOTS_TITLE_ID);
        if (R_FAILED(rc)) return rc;

        // PM termination is asynchronous. Wait up to one second so the next
        // launch cannot race the old process.
        for (u32 attempt = 0; attempt < 20 && backendRunning(); ++attempt) {
            svcSleepThread(50'000'000ULL);
        }
        if (backendRunning()) return MAKERESULT(Module_Kernel, KernelError_TimedOut);
    }

    const NcmProgramLocation location{
        .program_id = SWOTS_TITLE_ID,
        .storageID = NcmStorageId_None,
    };
    u64 pid = 0;
    return pmshellLaunchProgram(0, &location, &pid);
}

class SettingsGui final : public tsl::Gui {
public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("SWOTS", "Settings");
        auto *list = new tsl::elm::List();

        m_opacityHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_opacityHeader);
        auto *opacity = new tsl::elm::TrackBar("O");
        opacity->setProgress(swots::settings::opacityToPercent(g_settings.opacity));
        opacity->setValueChangedListener([this](u8 progress) {
            const std::lock_guard lock(g_stateMutex);
            g_settings.opacity = swots::settings::percentToOpacity(progress);
            g_settingsDirty = true;
            updateLabels();
        });
        list->addItem(opacity);

        m_radiusHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_radiusHeader);
        constexpr u8 radiusSteps = swots::settings::kMaxDotRadius -
                                   swots::settings::kMinDotRadius + 1;
        auto *radius = new tsl::elm::StepTrackBar("R", radiusSteps);
        radius->setProgress(swots::settings::dotRadiusToStep(g_settings.dotRadius));
        // StepTrackBar callbacks receive the discrete step index (0..10),
        // unlike TrackBar callbacks which receive a percentage.
        radius->setValueChangedListener([this](u8 step) {
            const std::lock_guard lock(g_stateMutex);
            g_settings.dotRadius = swots::settings::stepToDotRadius(step);
            g_settingsDirty = true;
            updateLabels();
        });
        list->addItem(radius);

        m_sensitivityHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_sensitivityHeader);
        auto *sensitivity = new tsl::elm::TrackBar("S");
        sensitivity->setProgress(g_settings.sensitivity);
        sensitivity->setValueChangedListener([this](u8 progress) {
            const std::lock_guard lock(g_stateMutex);
            g_settings.sensitivity = progress;
            g_settingsDirty = true;
            updateLabels();
        });
        list->addItem(sensitivity);

        m_smoothingHeader = new tsl::elm::CategoryHeader("", true);
        list->addItem(m_smoothingHeader);
        auto *smoothing = new tsl::elm::TrackBar("M");
        smoothing->setProgress(g_settings.smoothing);
        smoothing->setValueChangedListener([this](u8 progress) {
            const std::lock_guard lock(g_stateMutex);
            g_settings.smoothing = progress;
            g_settingsDirty = true;
            updateLabels();
        });
        list->addItem(smoothing);
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer *renderer,
                                                        s32 x, s32 y, s32, s32) {
            const char *text = m_saveResult[0] != '\0'
                                   ? m_saveResult
                                   : "Left/Right: adjust    A: apply and return";
            renderer->drawString(text, false, x + 8, y + 22, 15,
                                 renderer->a(tsl::style::color::ColorDescription));
        }), 42);

        updateLabels();
        frame->setContent(list);
        return frame;
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &,
                     HidAnalogStickState, HidAnalogStickState) override {
        if (!(keysDown & HidNpadButton_A)) return false;
        const Result rc = flushSettings();
        if (R_FAILED(rc)) {
            std::snprintf(m_saveResult, sizeof(m_saveResult),
                          "Failed %08X", static_cast<unsigned>(rc));
            return true;
        }
        tsl::goBack();
        return true;
    }

private:
    void updateLabels() {
        const std::lock_guard lock(g_stateMutex);
        char label[40];
        std::snprintf(label, sizeof(label), "Opacity  %u%%",
                      static_cast<unsigned>(
                          swots::settings::opacityToPercent(g_settings.opacity)));
        m_opacityHeader->setText(label);
        std::snprintf(label, sizeof(label), "Dot radius  %u px",
                      static_cast<unsigned>(g_settings.dotRadius));
        m_radiusHeader->setText(label);
        std::snprintf(label, sizeof(label), "Sensitivity  %u%%",
                      static_cast<unsigned>(g_settings.sensitivity));
        m_sensitivityHeader->setText(label);
        std::snprintf(label, sizeof(label), "Smoothing  %u%%",
                      static_cast<unsigned>(g_settings.smoothing));
        m_smoothingHeader->setText(label);
    }

    tsl::elm::CategoryHeader *m_opacityHeader = nullptr;
    tsl::elm::CategoryHeader *m_radiusHeader = nullptr;
    tsl::elm::CategoryHeader *m_sensitivityHeader = nullptr;
    tsl::elm::CategoryHeader *m_smoothingHeader = nullptr;
    char m_saveResult[24]{};
};

class MainGui final : public tsl::Gui {
public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("SWOTS", APP_VERSION);
        auto *list = new tsl::elm::List();

        // A stale flag must not look enabled after a reboot or renderer crash.
        // Showing Off lets A relaunch the backend immediately.
        m_toggle = new tsl::elm::ToggleListItem("Motion cues",
                                                isEnabled() && backendRunning());
        m_status = new tsl::elm::ListItem("Renderer", backendRunning() ? "Running" : "Stopped");

        m_toggle->setStateChangedListener([this](bool enabled) {
            Result rc = setEnabled(enabled);
            if (R_SUCCEEDED(rc) && enabled) rc = launchBackend();
            if (R_SUCCEEDED(rc) && !enabled && backendRunning()) {
                rc = pmshellTerminateProgram(SWOTS_TITLE_ID);
            }

            if (R_FAILED(rc)) {
                setEnabled(false);
                m_toggle->setState(false);
                std::snprintf(m_resultText, sizeof(m_resultText),
                              "Failed 0x%08X", static_cast<unsigned>(rc));
                m_status->setValue(m_resultText);
                m_lastFailure = rc;
                return;
            }

            m_lastFailure = 0;
            m_status->setValue(enabled ? "Starting..." : "Idle");
            // Return focus to the game; the independent sysmodule owns the
            // persistent full-screen cue layer.
            if (enabled) requestResumeGame();
            tsl::Overlay::get()->hide();
        });

        list->addItem(new tsl::elm::CategoryHeader("Vehicle motion cues", true));
        list->addItem(m_toggle);
        list->addItem(m_status);
        auto *settings = new tsl::elm::ListItem("Settings", ">");
        settings->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            tsl::changeTo<SettingsGui>();
            return true;
        });
        list->addItem(settings);
        auto *teslaMenu = new tsl::elm::ListItem("Stop and return to Tesla", ">");
        teslaMenu->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            g_exitIntent.requestReturnParent();
            const Result rc =
                publishLifecycleWithRetry(tesla_lifecycle::State::Parent);
            if (R_FAILED(rc)) {
                g_exitIntent.reset();
                std::snprintf(m_resultText, sizeof(m_resultText),
                              "Failed 0x%08X", static_cast<unsigned>(rc));
                m_status->setValue(m_resultText);
                m_lastFailure = rc;
                return true;
            }

            const Result disableRc = setEnabled(false);
            if (R_FAILED(disableRc) || !stopUnresponsiveBackend()) {
                g_exitIntent.reset();
                if (R_SUCCEEDED(disableRc)) m_toggle->setState(false);
                const Result shownRc = R_FAILED(disableRc)
                                           ? disableRc
                                           : MAKERESULT(Module_Kernel,
                                                        KernelError_TimedOut);
                std::snprintf(m_resultText, sizeof(m_resultText),
                              "Failed 0x%08X",
                              static_cast<unsigned>(shownRc));
                m_status->setValue(m_resultText);
                m_lastFailure = shownRc;
                return true;
            }
            // Close this NRO without hiding it first. nx-ovlloader will load
            // its default ovlmenu after the renderer is fully stopped.
            tsl::Overlay::get()->close();
            return true;
        });
        list->addItem(teslaMenu);
        list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer,
                                                    s32 x, s32 y, s32, s32) {
            renderer->drawString("Peripheral dots stay visible after this menu closes.",
                                 false, x + 8, y + 22, 15,
                                 renderer->a(tsl::style::color::ColorDescription));
        }), 42);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        if (++m_counter % 20 != 0 || !m_status) return;
        if (R_FAILED(m_lastFailure)) return;
        m_status->setValue(backendRunning() ? "Running" : "Stopped");
    }

    bool handleInput(u64 keysDown, u64, const HidTouchState &,
                     HidAnalogStickState, HidAnalogStickState) override {
        if (!(keysDown & HidNpadButton_B)) return false;
        if (g_exitIntent.current() ==
            tesla_lifecycle::ExitIntent::ReturnParent) {
            return true;
        }
        if (isEnabled() && backendRunning()) requestResumeGame();
        tsl::Overlay::get()->hide();
        return true;
    }

private:
    tsl::elm::ToggleListItem *m_toggle = nullptr;
    tsl::elm::ListItem *m_status = nullptr;
    u32 m_counter = 0;
    Result m_lastFailure = 0;
    char m_resultText[32]{};
};

class SWOTSOverlay final : public tsl::Overlay {
public:
    void initServices() override {
        pmshellInitialize();
        g_sdOpen = R_SUCCEEDED(fsOpenSdCardFileSystem(&g_sd));
        g_lifecycleSession = randomGet64();
        if (g_lifecycleSession == 0) g_lifecycleSession = 1;
        loadSettings();
    }

    void exitServices() override {
        flushSettings();
        if (g_sdOpen) fsFsClose(&g_sd);
        g_sdOpen = false;
        pmshellExit();
    }

    bool onBeforeForegroundAcquire() override {
        g_exitIntent.reset();
        tesla_lifecycle::Record visible{};
        if (R_FAILED(publishLifecycleWithRetry(
                tesla_lifecycle::State::Visible, &visible))) {
            return false;
        }

        if (queryBackendStatus() == BackendStatus::Absent) return true;
        for (u32 attempt = 0; attempt < 50; ++attempt) {
            if (lifecycleAckMatches(visible)) return true;
            svcSleepThread(20'000'000ULL);
        }

        // If the renderer cannot acknowledge that its layer is gone, stop it.
        // Kernel process cleanup is the final fail-closed guarantee.
        return stopUnresponsiveBackend();
    }

    void onShow() override { g_exitIntent.reset(); }

    void onHide() override {
        // Trackbars only mutate RAM. Persist once when Tesla is dismissed so
        // holding Left/Right never turns into a stream of SD writes.
        flushSettings();
        // B, shortcut, Home, Power, and out-of-bounds touch all return focus
        // to the game. The record is published only after foreground release.
        requestResumeGame();
    }

    void onForegroundReleased() override {
        const auto intent = g_exitIntent.consume();
        if (intent == tesla_lifecycle::ExitIntent::ResumeGame) {
            publishLifecycleWithRetry(tesla_lifecycle::State::Hidden);
        }
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainGui>();
    }
};

} // namespace

int main(int argc, char **argv) {
    return tsl::loop<SWOTSOverlay>(argc, argv);
}
