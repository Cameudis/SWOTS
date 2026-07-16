# IPC Control Architecture

Status: implemented in `v0.1.0-alpha.3`;
release remains gated on the hardware checklist below.

This design is based on the repository at that commit, the patched libtesla
commit `f766e9b607a05e9756843cbd62b3bfb98be1646c`, and the locally installed
libnx `4.12.0-1` headers. It deliberately leaves the title ID, NPDM capability
bytes, VI implementation, HID implementation, and release layout unchanged.

## Decision

Keep the Tesla overlay and renderer as two programs. A hidden `.ovl` cannot own
the persistent cue layer, so merging them would remove the core feature.

Replace SD-card control files with a small, versioned CMIF service registered by
the renderer as `swots:u`. Use kernel events for bounded transition completion.
Keep files only for durable settings and diagnostic logs.

The renderer exists only while cues are enabled. It starts suspended, renders
after the overlay releases foreground, suspends before Tesla acquires
foreground, and exits on Off, frontend loss, startup timeout, or an unrecoverable
control failure. There is no live-but-disabled renderer state.

## Current behavior and motivation

The current renderer polls:

| Input | Interval while enabled | Purpose |
|---|---:|---|
| `enabled.flag` | 250 ms | Enable/disable state |
| `settings.cfg` | 500 ms | Runtime configuration |
| `tesla_lifecycle.bin` | 50 ms | Tesla foreground coordination |

This normally means about 26 file opens or reads per second while cues are
enabled. When Tesla is visible, the renderer has already destroyed its VI layer
and stopped the motion sensor, but it still wakes every 50 ms to poll control
files. When `enabled.flag` disappears, the renderer sleeps for 250 ms and loops
forever instead of exiting; PM termination in the overlay is the only normal
exit path.

The current safety properties must be preserved:

- Tesla must not acquire foreground until the renderer has destroyed its layer.
- An unresponsive renderer must be terminated within a bounded time.
- A stale or out-of-order resume must never recreate the layer.
- The renderer must start paused until the owning overlay has proved that Tesla
  released foreground.

## Goals

- No SD-card polling for enable, configuration, lifecycle, or acknowledgements.
- No periodic control wakeups while the renderer is suspended.
- Runtime configuration changes while cues are enabled.
- `A` is the only commit action in the settings screen; other exits cancel.
- A bounded, fail-closed transition before Tesla becomes visible.
- A renderer process that cannot become a permanent orphan.
- Explicit protocol versioning and deterministic mixed-version behavior.
- Host-testable state and protocol logic.

## Non-goals

- Do not merge rendering into the Tesla overlay.
- Do not add shared memory; the control data is small and low-frequency.
- Do not change the VI layer, motion model, sensor selection, title ID, or
  tested NPDM capability bytes in the IPC change.
- Do not shrink service or kernel permissions in the same change.
- Do not promise a quantified battery improvement without hardware measurement.

## Ownership

| Resource | Owner | Notes |
|---|---|---|
| Settings draft | Settings GUI | Exists only while the page is open |
| Committed settings | Tesla overlay | Resolved from durable, revisioned settings files |
| Applied runtime settings | Renderer | Full snapshot identified by revision |
| Enabled state | Renderer process plus valid IPC session | No persistent flag |
| VI layer and framebuffer | Renderer render thread | Created only in Running |
| Motion sensors | Renderer render thread | Started only in Running |
| Lifecycle ordering | Renderer control state machine | Session plus sequence |
| Troubleshooting logs | Renderer | Diagnostic output, never control input |

The overlay keeps one IPC session open for its lifetime, including while it is
hidden. A dedicated overlay IPC worker is the only thread allowed to use that
session. Closing the actual Horizon session unexpectedly is treated as frontend
loss; a client-supplied number is never sufficient proof of ownership.

## User-visible behavior

### Main screen

- The normal UI shows only the `Motion cues` On/Off toggle. Remove the separate
  `Renderer` status row.
- `Motion cues: On` means cues remain enabled. Internal Running/Suspended states
  do not change the toggle and are never shown to the user.
- After Tesla closes, the renderer resumes cues with the latest committed
  configuration.
- `Off` cleans up and exits the renderer. If graceful stop times out, PM kills
  it before the toggle changes to Off.
- After a reboot cues are Off. This matches the effective current behavior:
  there is no `boot2.flag`, and a stale enabled flag without a process is shown
  as Off.

### Settings screen

- The screen opens a private draft copied from the committed settings.
- Trackbar movement changes only the draft: no IPC and no file writes.
- `A` validates and commits the complete draft.
- `B`, Home, Power, an out-of-bounds dismissal, overlay shutdown, or any other
  exit without `A` discards the draft. Because libtesla keeps the GUI stack when
  hidden, this requires an explicit transaction-cancel hook rather than relying
  on `SettingsGui` destruction.
- After a successful `A`, the screen returns to the SWOTS main screen and shows
  only `Saved`.
- No game-layer live preview is attempted. A future preview, if wanted, must be
  drawn inside the settings GUI by the overlay itself.

The settings GUI must not mutate the global committed object. Its callbacks
write to a page-local `draft`; the old unconditional `flushSettings()` calls in
overlay hide/exit paths must not commit that draft. `onHide()` may run from
libtesla's background input thread, so it only marks the settings transaction
canceled under a mutex. The next main-thread `onShow()` pops/rebuilds the page
before restoring focus. It must not mutate the GUI stack from the background
thread.

### User-facing copy

Do not expose `Renderer`, `Running`, `Suspended`, `Paused for Tesla`, `Stopping`,
IPC, PM, Result codes, or backend state in normal UI copy. Keep technical detail
in logs. User-visible notices are short and action-oriented:

| Situation | Copy |
|---|---|
| Settings committed | `Saved` |
| Settings could not be committed | `Couldn't save` |
| Enable failed | `Couldn't start` |
| Off could not be confirmed | `Couldn't stop` |
| Installed ovl/renderer versions do not match | `Restart required` |

If settings were saved but runtime IPC recovery turns cues Off, still show only
`Saved`; the main toggle reflects Off. Do not append an explanation to the save
confirmation. Unexpected technical failures are logged and represented by the
truthful toggle state rather than a permanent status label.

## Service transport

The renderer registers a CMIF service named `swots:u` with a small session
limit, initially two. Horizon service names are limited to eight bytes; this
name is seven. The implementation should use the libnx 4.12 APIs
`smRegisterServiceCmif`, service dispatch helpers on the client, kernel Events,
and wait primitives for internal wakeups. Service acquisition after libtesla's
initial `initServices()` callback must use `tsl::hlp::doWithSmSession` or an
explicitly reference-counted lifetime SM session; it must not assume SM remains
initialized after that callback.

libnx 4.12 has client dispatch helpers but no high-level CMIF server manager for
this use. Server-side `svcAcceptSession`, `svcReplyAndReceive`, HIPC/CMIF parsing,
reply construction, and Close-message handling must be isolated in
`source/ipc_server.cpp`. Descriptor counts, raw-data sizes, CMIF magic/type,
command size, handle attributes, and reserved fields must be validated before
reading a payload. Wire validation and the state machine remain
platform-independent so host tests do not need Horizon IPC.

All server commands return promptly after validation and queueing. Completion is
observed through a state-change event and `GetStatus`; the server never holds a
request open while waiting for VI or HID cleanup.

This does **not** make libnx `serviceDispatch` bounded: it ultimately uses a
synchronous request without a timeout and can block if the renderer control
thread is wedged before replying. Therefore the libtesla/UI thread never calls
CMIF directly. A single long-lived overlay IPC worker owns the `Service` and
remote Event handles and serializes every dispatch. The UI submits work through
a single-flight mailbox/Event and waits only on a local completion Event with one absolute
deadline. Enable, Suspend, SetConfig, Resume, Stop, and status transactions all
use this rule; `onForegroundReleased()` also waits for its local Resume
completion before returning. Foreground callbacks are forbidden from calling
`serviceDispatch*` directly.

Any CMIF dispatch/status deadline freezes submissions, invalidates and
increments the local worker generation, and suppresses callbacks such as a
pending `onForegroundReleased()` Resume from that generation. The libtesla
thread calls PM termination directly rather than queueing RequestStop behind a
blocked dispatch. After PM confirms process absence, renderer death closes the
session, the blocked worker must return, and the overlay joins it, closes the
client Service/Event, clears the old mailbox, and reports Off. A fresh
worker/session is created only by an explicit Enable. If PM cannot confirm
absence, enter an internal Fault state rather than reporting Off; if UI is
available, use the contextual `Couldn't start` or `Couldn't stop`. A prompt,
explicit protocol error is not a timeout and may follow its command-specific
recovery rule.

### PM termination invariant

Every path that invokes `pmshellTerminateProgram`--startup failure, version
mismatch, Suspend/SetConfig/Resume timeout, immediate Stop error, explicit Off,
or migration--uses a separate absolute one-second process-absence deadline.
Only confirmed `pmdmnt` ProcessNotFound permits Off, worker/session replacement,
or foreground acquisition. Failure to confirm absence leaves the worker
generation invalid, forbids Resume/relaunch, and records internal Fault; absence
polling never restarts its deadline and is never unbounded. UI copy remains the
short contextual message defined above.

### Wire rules

- Every structure has a magic, ABI major/minor, and byte size where applicable.
- Use fixed-width integers and explicit reserved zero fields.
- Add `static_assert` checks for every wire structure size.
- Never send C++ classes, bools, pointers, compiler-dependent enums, or structs
  with implicit padding.
- Unknown command IDs, nonzero reserved fields, invalid enum values, and
  incompatible ABI majors return an error without changing state.
- A newer minor version is accepted only when the advertised capabilities cover
  every command the client will use.
- `renderer_version` is a paired-release build number embedded in both
  artifacts and must be bumped when either side's required behavior changes.
  A mismatch fails closed even when the ABI major still matches.

Suggested protocol data:

```text
ProtocolInfo
  magic = "SWIP"
  abi_major
  abi_minor
  capabilities
  renderer_version

SettingsWire
  size
  version
  opacity
  dot_radius
  sensitivity
  smoothing
  reserved[] = 0

StatusWire
  process_state
  desired_state
  applied_state
  owner_epoch
  caller_is_owner
  applied_lifecycle_sequence
  accepted_config_revision
  applied_config_revision
  accepted_config_valid
  applied_config_valid
  last_result
```

The explicit validity bits let a migrated version-1 settings document use its
real revision zero without confusing it with “no configuration received”.

Lifecycle sequence and configuration revision are separate, owner-epoch-scoped
counters. Slider or settings activity can never make a Suspend or Resume look
stale. `owner_epoch` is a random stale-message guard, not an authorization
credential. The server authorizes against the actual accepted kernel session
slot plus its internal slot generation, and never exposes that internal identity
on the wire.

### Command set

| ID | Command | Behavior |
|---:|---|---|
| 0 | `GetProtocolInfo` | Returns ABI and capabilities |
| 1 | `AcquireStateChangedEvent` | Returns this server session's copied readable Event handle |
| 2 | `GetStatus` | Returns desired/applied state and revisions |
| 3 | `ClaimAndSuspend` | Safely suspends and then transfers ownership |
| 4 | `SetConfig` | Queues a complete validated settings snapshot |
| 5 | `Resume` | Resumes only for the current owner and fresh sequence |
| 6 | `RequestStop` | Cleans up and exits for the current owner |

Every accepted server session owns a separate non-autoclear state Event. A state
change signals every live session Event, but one client clearing its Event
cannot consume another client's notification. Events are hints only; generation
and status are the source of truth.

The current owner session may use `ClaimAndSuspend` whenever SWOTS opens. A new
session may claim only in Starting/Unowned/Orphan state, after the old kernel
session has closed. A live different owner makes the request fail; the overlay
may then use the bounded PM recovery path. This deliberately avoids live-owner
takeover races. When an orphan claim is accepted, the server establishes a
takeover fence immediately: no command from the old slot generation can be
applied, and ownership transfers only after Suspended is applied.

`SetConfig`, `Resume`, and graceful `RequestStop` require both the owning kernel
session and matching `owner_epoch`. The render thread rechecks the queued command
epoch before applying it. Duplicate sequence/revision numbers are idempotent
only when command and complete payload are identical; the same number with a
different command or payload is rejected. Lower numbers are rejected.

Each per-session state event is level-triggered. The IPC worker obtains it once
and uses this pattern to avoid missed wakeups:

1. Clear the event.
2. Have the IPC worker send the synchronous CMIF request, whose server-side
   action only queues state and replies.
3. Read status.
4. If the requested sequence is not applied, wait with the remaining timeout.
5. Clear, reread status, and repeat until applied or timed out.
6. At the absolute deadline, read status once more before declaring timeout.

The initial session limit of two is sufficient for the normal owner plus one
orphan-recovery client only if unaffiliated sessions are rejected and closed
promptly. Increase the limit after handle/heap review if diagnostics need another
connection. This protocol assumes trusted local homebrew: another process with
service access can attempt a denial of service, and `owner_epoch` is not an
authentication secret. Preventing malicious local clients is outside the first
release's threat model; display safety still fails closed.

## Renderer state model

```text
Starting -> Suspended -> Running
                ^          |
                |          v
                +---- Suspending

Starting, Suspended, Running, Suspending -> Stopping -> process exit
```

- **Starting:** register the service, create control primitives, and start a
  two-second ownership watchdog. Do not initialize VI or start motion sensors.
- **Suspended:** no layer and no active motion sensor. Wait indefinitely on IPC
  or an internal control event; there is no 50 ms timer.
- **Running:** initialize the layer, start motion, update and present at the
  existing adaptive rates.
- **Suspending:** stop motion first, destroy the layer, publish the applied
  lifecycle sequence, and signal the state event.
- **Stopping:** the render thread stops motion, destroys the layer, publishes
  `resources_safe`, and signals render completion. The control thread then
  replies to any accepted request, closes sessions/port, unregisters the
  service, joins the render thread, closes process services, and exits.

Keep the existing 500 ms post-foreground-release cooldown for the first IPC
implementation. Implement it with a one-shot timer so it does not delay control
requests. It may be shortened only after hardware coexistence testing.

### Desired and applied state

IPC changes `desired_state`; only the render thread changes `applied_state`.
The control thread never manipulates VI, framebuffer, or HID objects. An applied
Suspend is acknowledged only after both motion and the layer are gone. Every
queued state/config command carries the internal owner slot generation and
wire `owner_epoch`; the render thread discards it if either no longer matches.

Configuration is also desired/applied. The complete pending snapshot is copied
under a short lock and applied at a frame boundary. While Suspended it can be
applied immediately without creating the layer. A Resume always carries the
latest committed revision and full snapshot as an atomic, idempotent recovery
path: validation/revision failure leaves desired state Suspended, and the
snapshot is applied before layer creation so the first resumed frame uses it.

## Threading and wakeups

Use two renderer threads plus the overlay's sleeping IPC worker:

- The renderer control thread owns the service port and sessions, validates
  commands, updates desired state, handles session closure/deadlines, and
  signals a local `UEvent`.
- The render thread owns VI, framebuffer, HID motion objects, applied state,
  logs, and the existing motion/power policy.
- The overlay IPC worker owns the client `Service` and per-session remote Event.
  Its single-flight submission gate prevents an accepted `SetConfig` from being
  reordered after Resume; competing callers share the same absolute deadline.

The raw server loop uses `svcReplyAndReceive` over the service port, accepted
session handles, and a render-to-control kernel Event. A `UEvent` cannot be put
directly in that syscall. Control-to-render may use level state plus `UEvent`;
render-to-control uses the kernel Event so cleanup/config completion wakes the
server loop. The loop passes the nearest startup/orphan/stop absolute deadline
as its wait timeout. Every hard deadline has an explicit control-thread action:

- startup reaches its two-second deadline without an owner: `svcExitProcess`;
- orphan grace/cleanup reaches its two-second deadline unless takeover has
  completed **and** `resources_safe` has been published: `svcExitProcess`;
- accepted Stop reaches its one-second cleanup deadline without render
  completion: `svcExitProcess`.

Thus no exit path depends on a wedged render thread to prevent an immortal
process. Non-owner sessions that do not complete protocol negotiation/claim
within two seconds are closed so they cannot consume the recovery slot.

The render thread waits on the local control event and separate sampling and
presentation deadlines. While Running, timed sampling is still required because
the HID APIs do not provide a suitable event for every motion sample. Preserve
current behavior: full rate while moving, 30 Hz after one quiet second, and
10 Hz presentation after five quiet seconds. Deep idle still has a bounded
maximum sampling interval for motion response, but it does not keep a fixed
high-frequency presentation timer. While Suspended, stop both cadence deadlines
and wait only for control. Diagnostic heartbeats are forbidden while Suspended;
only a state change or error may write a log there.

Each process keeps one filesystem thread owner: the overlay main thread performs
settings reads/saves, while the renderer render thread writes diagnostics. The
overlay background callback and both IPC control threads perform no filesystem
operations, so the existing one-FS-session-per-process assumptions are not
accidentally violated.

Normal Stop ordering is fixed:

1. Control accepts and promptly replies that Stop was queued.
2. Render stops HID and destroys VI resources.
3. Render publishes `resources_safe` and signals the kernel completion Event.
4. Control closes server sessions/port, unregisters `swots:u`, and joins render.
5. Process services close and the process exits.

The overlay considers Stop successful only after PM reports process absence;
observing Stopping or `resources_safe` alone is not enough.

## Operational flows

### Enable from Off

1. The overlay has already loaded committed settings from disk.
2. It launches the title through `pmshellLaunchProgram`.
3. It retries `smGetService("swots:u")` for at most one second. This bounded
   startup polling happens only during an explicit user action.
4. It checks ABI major and required capabilities.
5. It acquires the state event and sends `ClaimAndSuspend` with a random client
   `owner_epoch` and lifecycle sequence.
6. It waits until status confirms caller ownership, applied Suspended, and the
   applied lifecycle sequence. Sending config before this point is invalid.
7. It sends the complete committed configuration and waits for its applied
   revision.
8. It hides the overlay.
9. `onForegroundReleased()` queues `Resume` on the IPC worker with a new
   lifecycle sequence and
   the same configuration revision/snapshot.
10. The renderer validates and applies that snapshot, waits the compatibility
   cooldown, creates its layer, starts
   motion, and publishes Running.

If service registration or ownership does not complete before its timeout, the
overlay follows the uniform PM termination rule below. It reports Off only after
process absence is confirmed; otherwise it shows `Couldn't start`.

### Open Tesla while cues are running

1. `onBeforeForegroundAcquire()` submits a `ClaimAndSuspend` transaction to the
   IPC worker. One absolute one-second deadline starts before queue submission
   and covers every CMIF dispatch, status read, Event wait, and retry.
2. The renderer stops motion and destroys the layer.
3. The renderer publishes applied Suspended and signals the event.
4. The worker verifies status and signals its local completion Event; only then
   does the libtesla thread permit foreground acquisition.
5. If the local completion does not arrive before the absolute deadline, use
   the uniform worker-generation/PM recovery rule. Foreground is permitted only
   after process absence has been confirmed; the invalid generation suppresses
   any later release-callback Resume.

This replaces lifecycle and ack files without weakening the current fail-closed
behavior.

### Confirm settings with A

1. Validate the draft and serialize a complete revisioned settings document.
2. Run the durable-save/recovery procedure described below. Do not infer the
   disk result solely from the final FS Result code.
3. Only if recovery selects the draft as the authoritative snapshot, promote it
   to committed memory and use that selected revision.
4. If cues are enabled, submit `SetConfig` through the IPC worker and verify that
   the renderer accepted/applied the owner-scoped revision while Suspended.
5. On success, return to the main screen. The visible cues change only after
   Tesla closes.

If recovery selects the old snapshot, committed memory and renderer configuration
remain old; stay on the settings page and show the save error. If no authoritative
snapshot can be recovered, do not promote or apply the draft: stop cues, remain
on the settings page, and show `Couldn't save`.

If persistence succeeds but IPC fails after a bounded retry, the saved settings
remain authoritative. Use the uniform timeout recovery directly, not a Stop
queued behind the worker. After confirmed process absence, show only `Saved` and
let the main toggle reflect Off. Do not report enabled with stale runtime
settings.

### Cancel settings

Destroy the draft and return without file or IPC activity. Overlay hide/exit
must resume using the last committed snapshot, not the abandoned draft.

### Turn Off

1. Ensure ownership; if necessary, safely suspend first.
2. Send `RequestStop` and wait up to one second for process exit; Stopping alone
   is only progress.
3. A timeout **or any immediate ownership/Suspend/RequestStop protocol error**
   goes directly to PM termination; do not queue further work behind the failed
   transaction.
4. Close the IPC session and report Off only after process absence is confirmed.
   Otherwise retain the invalid worker generation and show `Couldn't stop`.

### Stop and return to Tesla

Use the same stop sequence, then close the SWOTS overlay so nx-ovlloader can
return to its parent. Do not publish or consume a Parent lifecycle file.

### Frontend failure

An unexpected close of the actual owner kernel session immediately fences that
slot generation, requests Suspended, and starts a two-second orphan grace
period. A new frontend may connect and `ClaimAndSuspend` during the grace period.
Otherwise the renderer exits. Normal overlay hiding does not close its
process-global IPC session.

The assumption that hide/Home/Power preserves the overlay process and client
session is a release gate and must be proven on hardware. If testing shows that
nx-ovlloader legitimately closes it while cues should continue, this ownership
model is rejected for release. Do not add heartbeats or assume that copying an
ordinary Event handle reports peer death; design and validate an explicit
kernel-session/lifetime protocol before removing the file transport.

## Persistence and logs

Keep:

- `/config/swots/settings.cfg`
- `/config/swots/settings.tmp`
- `/config/swots/settings.bak`
- `/config/swots/renderer.log`
- `/config/swots/sensor.log`

The current multi-rename save path is crash-resistant but not a transaction:
an FS error after a rename or commit can leave the new value, the old value, or
only recovery candidates despite the returned failure. The IPC design must not
promise that every failed save leaves the old disk value.

Upgrade the settings document to a self-validating version 2 containing a
monotonic unsigned 64-bit `revision` and checksum. Continue to read version 1 as
revision zero and migrate its values on the first successful `A` commit. The
canonical version-2 text uses ASCII, decimal fields, and LF exactly:

```text
version=2
revision=<decimal u64>
opacity=<decimal>
dot_radius=<decimal>
sensitivity=<decimal>
smoothing=<decimal>
checksum=<eight lowercase hex digits>
```

Fields appear exactly once in that order. Decimal values are unsigned base-10
with no sign, whitespace, or leading zero except the value `0`; CR is forbidden.
The checksum line ends with exactly one LF followed immediately by EOF.

Use CRC-32/ISO-HDLC over the exact bytes from `version` through the newline after
`smoothing`; exclude the checksum line itself. Revision `UINT64_MAX` cannot be
incremented and makes a save fail with `Revision exhausted` rather than wrapping.

Candidate selection has two modes:

- **Clean startup:** scan primary, temp, and backup; reject incomplete,
  checksum-invalid, or out-of-range documents; choose the highest syntactically
  valid revision with primary-before-temp-before-backup tie break.
- **Recovery during the current save:** apply the same validation, then exclude
  the submitted revision unless its write/flush and first `fsFsCommit` durability
  point succeeded. Previously authoritative valid revisions remain eligible.
  Choose the highest eligible revision with the same tie break.

This distinction matters when a new temp revision is readable but its first
commit failed: if the old durable revision remains valid, the result is
Recovered old; it is Unknown only when no eligible older or submitted candidate
exists. A candidate found during a later clean startup is evaluated as actual
on-disk state rather than using lost in-memory knowledge from the interrupted
save. Adopt only the selected snapshot as committed memory and as the snapshot
sent over IPC.

The save outcome is therefore one of:

- **Committed:** the selected durable revision is the submitted draft.
- **Recovered old:** a valid older revision won; keep runtime settings old and
  report that the save failed.
- **Recovered new:** an FS operation reported failure but the valid submitted
  revision won; treat it as committed and report recovery in the log.
- **Unknown:** no valid candidate exists; do not apply the draft or claim a
  durable configuration, and stop cues until the user resolves storage.

Add fault-injection coverage after create, write, flush, each commit, delete,
and rename. A strict result must come from recovery/reload, not from optimistic
in-memory rollback.

Remove as control inputs:

- `enabled.flag`
- `tesla_lifecycle.bin`
- `tesla_lifecycle.tmp`
- `tesla_lifecycle.ack`
- `tesla_lifecycle.ack.tmp`

On the first successful IPC handshake, the overlay may delete stale control
files on a best-effort basis. Their presence must never affect new behavior.

The overlay obtains live status through IPC. Logs remain useful after a crash,
but should be written only on errors, meaningful state/source changes, and a
low-frequency diagnostic heartbeat while Running. Suspended has no heartbeat.
Logs must never be read as acknowledgments.

## Failure policy

| Failure | Result |
|---|---|
| Renderer absent | Show Off; launch only on user request |
| Process exists but service never appears | Terminate; show `Couldn't start` if absence is confirmed |
| ABI major/capability mismatch | Terminate old renderer; show `Restart required` |
| Suspend timeout | PM terminate; show Tesla only after process absence |
| Any CMIF/status timeout, including Resume | Invalidate worker generation, PM terminate directly, rebuild only on explicit Enable |
| Settings save reports failure | Reload/recover all candidates; use only the selected authoritative snapshot |
| No valid settings candidate after save | Stop cues; show `Couldn't save` |
| Settings IPC failure after successful save | Stop renderer; show only `Saved`, with toggle Off |
| Unexpected owner disconnect | Suspend immediately, allow two-second takeover, then exit |
| Repeated VI initialization failure | Publish Fault, clean up, and exit after a bounded retry budget |
| Motion-sensor start/sample transient | Preserve the existing bounded retry/source fallback policy while Running |
| SD unavailable at renderer start | Rendering may run from IPC config; log only if storage returns |

The renderer no longer requires SD availability to decide whether it is enabled
or safe to draw. This removes the current infinite SD-open retry before the
control loop.

## Compatibility and migration

Ship the new ovl and renderer together. Do not silently fall back to the old
file lifecycle protocol: a mixed pair could recreate a layer while Tesla is
visible.

For a transition release:

1. In `onBeforeForegroundAcquire()`, if an existing renderer process has no
   compatible `swots:u`, terminate it and confirm absence before showing Tesla.
2. Relaunch once from the installed ExeFS package.
3. If the service is still absent or incompatible, show `Restart required` and
   stay Off.
4. Ignore and best-effort delete old control files after a successful handshake.
5. Preserve version-1 settings values; read v1 and write the self-validating v2
   document only after a successful user confirmation.

Keep the title ID and verified NPDM `ffff0400` capability bytes. The renderer
adds the narrow `swots:u` service-host ACL required for registration; wildcard
service access covers client connections, while the existing kernel
capabilities cover sessions, events, and waits. Permission reduction is a
separate, hardware-tested project.

## Verification

### Host tests

- Wire structure sizes, magic/version validation, reserved fields, and config
  range validation.
- Startup starts Suspended and cannot render without an owned fresh Resume.
- Authorization is bound to the accepted kernel-session slot/generation; a
  copied wire epoch on another session cannot Resume or Stop.
- A different live client cannot take over. Orphan takeover fences all queued
  commands from the old slot generation before Suspended is published.
- Duplicate commands are idempotent only for identical payloads; stale
  lifecycle sequences and same-number/different-payload commands are rejected.
- Configuration revisions are independent of lifecycle sequences.
- `A` commits a complete draft; `B` and external dismissal discard it.
- Settings v1 migration, exact v2 canonical bytes/CRC vectors, revision
  exhaustion, clean-start versus in-save eligibility, and fault injection at
  every FS mutation/commit step produce a deterministic recovered outcome.
- IPC failure after save transitions the UI to Off without losing the file.
- Suspend completion occurs only after simulated motion/layer teardown.
- Per-session Events cannot be cleared by another client; status-before-wait and
  a final status read at the absolute deadline still observe completion.
- A server that accepts a synchronous CMIF request but never replies cannot
  block the libtesla thread; the local deadline reaches PM termination and the
  dead server session releases the IPC worker.
- Owner disconnect suspends and exits after the grace period unless taken over.
- Startup/orphan watchdog and stop timeout reach process exit even when the
  render thread never signals cleanup completion.

### Build checks

- `env -u DEVKITPRO make test`
- `source scripts/env.sh && make setup-libtesla && make verify -j2`
- Confirm the NPDM and packaged NPDM remain byte-compatible with the verified
  baseline.
- Inspect the service name, wire struct sizes, thread stacks, heap use, and
  release ZIP layout.

### Hardware checks

Record the SWOTS commit, HOS, Atmosphere, Tesla, nx-ovlloader, mode, and
controller for every run.

1. Enable from Off and confirm the first layer appears only after Tesla closes.
2. Open/close Tesla repeatedly and spam the shortcut during every transition.
3. Change each setting while enabled, press `A`, close Tesla, and confirm the
   first resumed frame uses the new values.
4. Change settings and cancel with `B`, Home, Power, shortcut, and touch
   dismissal; confirm none are saved or applied.
5. Turn Off and confirm process absence, sensor stop, layer destruction, and no
   further SWOTS wakeups.
6. Use Stop and return to Tesla and confirm the parent appears only after exit.
7. Kill the ovl while Running and Suspended; confirm safe suspend and renderer
   exit after the grace period.
8. Hang or kill the renderer during foreground acquisition; confirm the
   one-second fail-closed PM path, including a server that receives but never
   replies to the CMIF command.
9. Test version mismatch and a partial update; confirm it stays Off.
10. Test SD removal/unavailability after launch; control safety must continue.
11. Repeat in handheld/docked modes and with console/controller motion sources.
12. Compare idle wakeups and power on hardware; report measurements rather than
    inferring battery savings from host tests.
13. Confirm hide/reopen, Home, Power, and sleep/wake preserve the same overlay
    IPC session while cues are intended to continue. Failure blocks removal of
    the old lifecycle transport until a new lifetime design is reviewed.

The change is ready to replace the file protocol only after the full existing
compatibility checklist and the cases above pass on hardware.

## Implementation sequence

1. Add pure wire definitions, validation, desired/applied state machine,
   settings-v2 recovery selection, and host tests.
2. Add renderer control/render separation and local events without changing the
   file transport yet.
3. Add the raw CMIF server, per-session Events, overlay IPC worker, and bounded
   local completion waits.
4. Convert the settings GUI to draft/commit/cancel and add `SetConfig`.
5. Switch foreground lifecycle and stop/start flows to IPC with PM fallback.
6. Remove renderer reads of enabled/settings/lifecycle control files.
7. Add migration cleanup, documentation, and failure messages.
8. Run build verification, then the complete hardware matrix.

Each step should remain independently reviewable. The IPC transport, NPDM
permissions, VI behavior, HID behavior, and toolchain must not be changed in one
undifferentiated patch.
