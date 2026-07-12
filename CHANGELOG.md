# Changelog

## Unreleased

- No changes.

## 0.1.0-alpha.1 — 2026-07-12

- Added the Tesla frontend, managed VI renderer, settings, logs, and six-axis source recovery.
- Added responsive frame-rate-independent motion, subpixel drawing, alpha blending, and corrected horizontal direction.
- Added tests, NPDM regression checks, release packaging, and project documentation.
- Fixed `0x0000FC01`, VI initialization, framebuffer memory, Tesla input contention, and stalled sensor samples.
- Fixed Tesla lifecycle races. B safely resumes cues; returning to Tesla stops the renderer first.
