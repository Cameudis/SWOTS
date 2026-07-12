# Contributing

Keep changes small and open an issue first for architecture, Title ID, NPDM, or user-visible behavior changes.

```sh
env -u DEVKITPRO make test
source scripts/env.sh
make setup-libtesla
make verify -j2
```

- Add host regression tests when possible.
- Hardware-test VI, HID, PM, sensor, toolchain, and NPDM changes. Report the SWOTS commit, HOS, Atmosphère, Tesla, nx-ovlloader, mode, controller, steps, expected/actual result, and minimal log excerpts.
- Do not alter unusual legacy NPDM capabilities without byte checks and a successful hardware launch.
- Keep public documentation in English and update this changelog for user-visible changes.
- Use `GPL-2.0-or-later` for new files and preserve third-party notices.
- Never submit keys, game content, personal logs, SD card images, or files you cannot redistribute.

Contributions are submitted under `GPL-2.0-or-later`.
