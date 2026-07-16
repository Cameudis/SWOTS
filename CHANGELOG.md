# Changelog

## Unreleased

- No changes.

## 0.1.0-alpha.3 — 2026-07-16

- Replaced SD-card control-file polling with a private, versioned IPC service
  and event-driven lifecycle acknowledgements.
- Stopped the renderer completely when cues are Off and removed periodic
  control wakeups while it is suspended for Tesla.
- Added explicit `A`-to-save settings transactions while cues are On. Other
  exits cancel the draft, and saved values apply when Tesla closes.
- Added crash-safe, checksummed settings with automatic migration from the
  previous format and paired overlay/renderer version checks.
- Simplified user-facing status messages and strengthened startup, disconnect,
  timeout, metadata, and release verification paths.
- Hardware-tested startup, visual cues, runtime settings, stop, and restart.

## 0.1.0-alpha.2 — 2026-07-14

- Added a lower-level console motion source that remains available when games
  do not expose controller six-axis data.
- Added automatic promotion to a higher-quality controller stream, automatic
  fallback to the console stream, and a single native-style source indicator.
- Fixed inactive third-party controller placeholders, low-rate console stream
  stutter, source-selection jitter, and motion loss after changing games.
- Added bounded diagnostic history with source, sampling, style, retry, and
  startup-result details without allowing log spam.
- Reduced idle power use by stopping sensors with the renderer, lowering SD and
  controller polling, and adaptively reducing framebuffer submissions.
- Hardware-tested handheld rail-controller operation and source switching.

## 0.1.0-alpha.1 — 2026-07-12

- Added the Tesla frontend, managed VI renderer, settings, logs, and six-axis source recovery.
- Added responsive frame-rate-independent motion, subpixel drawing, alpha blending, and corrected horizontal direction.
- Added tests, NPDM regression checks, release packaging, and project documentation.
- Fixed `0x0000FC01`, VI initialization, framebuffer memory, Tesla input contention, and stalled sensor samples.
- Fixed Tesla lifecycle races. B safely resumes cues; returning to Tesla stops the renderer first.
