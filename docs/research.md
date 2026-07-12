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
| Motion | libnx six-axis HID |
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

## Next Work

- Test more system, controller, handheld, and docked combinations.
- Reduce NPDM permissions without breaking old systems.
- Tune motion on more devices.
- Add a pinned Switch cross-build to CI.
