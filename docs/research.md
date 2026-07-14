# Technical Notes

SWOTS uses two programs:

- A libtesla overlay provides the menu.
- A dynamic sysmodule keeps drawing after Tesla closes.

A normal `.ovl` stops rendering when hidden, so it cannot do both jobs.

SWOTS is an independent project. It is not a KineStop port or an official KineStop product.

## Stack

| Part | Choice |
|---|---|
| System | Atmosphere contents sysmodule |
| SDK | devkitA64, libnx, C++20 |
| Menu | libtesla |
| Drawing | Transparent `vi:m` managed layer |
| Motion | Console six-axis HID, controller HID fallback |
| State | Files on the SD card |

## References

- [libtesla](https://github.com/WerWolv/libtesla), commit `f766e9b607a05e9756843cbd62b3bfb98be1646c`: Tesla lifecycle and managed layers. SWOTS applies a small tracked hook after foreground release.
- [nx-ovlloader](https://github.com/WerWolv/nx-ovlloader), commit `c78178ae66388f4f9ba4d7aafe90cd208dceac73`: sysmodule startup, exit handling, firmware detection, and NPDM capabilities.
- [ovl-sysmodules](https://github.com/WerWolv/ovl-sysmodules), commit `9b114b3ceb1d2d343c66454c5734135d6dde2bc2`: process checks, dynamic launch, and `toolbox.json`.
- [motion-sickness-app](https://github.com/DavidVentura/motion-sickness-app), commit `8f248236d03fb5d68a109a064e782c75ebb5baf3`: peripheral cues, acceleration, rotation, and damping. SWOTS does not copy its Kotlin source.
- [libnx](https://github.com/switchbrew/libnx): VI, framebuffer, HID, and PM APIs.

Public KineStop behavior was used only to compare motion direction and sensitivity.

## Important Findings

- Handheld sensors use `HidNpadIdType_Handheld`, not `HidNpadIdType_No1`.
- Motion integration must use the real frame time.
- Old systems require the verified nx-ovlloader NPDM baseline, including `ffff0400`.
- A 1280x720 double buffer used too much memory. SWOTS draws at 640x360 and lets VI scale it.
- Motion cues need acceleration and rotation. A gyroscope line alone is not enough.
- Gyroscope bias is learned only while the device is still. This avoids hiding small hand movements.
- In handheld use, the console's built-in IMU is rigidly coupled to the screen.
  `SevenSixAxisSensor` therefore provides motion without depending on the game
  to start controller six-axis input or on a third-party rail controller's
  motion protocol. It is the primary source; ordinary Npad six-axis remains a
  fallback for incompatible systems.
- `SevenSixAxisSensor` uses a 0x80000-byte libnx transfer-memory buffer and is
  available on HOS 5.0.0+. SWOTS already has enough heap for this allocation.
- Retail testing shows the fused console stream advances at approximately
  10 Hz. SWOTS zero-order-holds the latest valid acceleration and angular
  velocity for at most 150 ms, allowing its render-rate motion integrator and
  damping to remain continuous without driving indefinitely after a dropout.
- While the console source is active, SWOTS passively samples the advertised
  Npad six-axis LIFO without relying on it. Six hundred milliseconds of
  connected, non-placeholder controller data promotes that source; 800 ms of
  continuous loss returns to the console source. A three-second post-switch
  cooldown prevents oscillation.
- Source changes use a single in-layer toast (`MOTION: CONTROLLER` or
  `MOTION: CONSOLE`) for 2.5 seconds. The upper-left card resembles a native
  Switch notification but never opens a system applet and has no notification
  queue, allocation, or extra thread.
- SWOTS starts the active sensor only after its VI layer is available and stops
  it while disabled or suspended for Tesla. Visual motion and source notices
  render at full rate; a stable frame steps down to 30 Hz after one second and
  10 Hz after five seconds. Lifecycle checks remain responsive within 50 ms.
- While disabled, only the enable flag is checked every 250 ms. Settings and
  Tesla lifecycle files are not polled until SWOTS is enabled, avoiding
  unnecessary SD-card wakeups.
- Bluetooth-driver reports do not cover controllers attached through the
  rails, while an HID MITM or raw XCD/controller-protocol implementation would
  be substantially more invasive and controller-specific.

## Next Work

- Test more system, controller, handheld, and docked combinations.
- Reduce NPDM permissions without breaking old systems.
- Tune motion on more devices.
- Validate console-sensor axis direction on retail hardware and tune only if
  the main-unit coordinate convention differs from Npad six-axis.
- Add a pinned Switch cross-build to CI.
