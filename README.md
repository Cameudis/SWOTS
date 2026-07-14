# SWOTS — Switch While On The Subway

SWOTS is an experimental motion-cue overlay for Atmosphère and Tesla Menu. It draws animated edge dots from Switch or controller six-axis data.

> [!IMPORTANT]
> SWOTS is not a medical device and does not guarantee relief. Stop and rest if you feel unwell; seek medical advice for persistent or severe symptoms.

## Requirements

Atmosphère, Tesla Menu, and nx-ovlloader. Building also requires devkitPro `switch-dev`.

Compatibility is still limited; see [compatibility](docs/compatibility.md).

## Install

1. Verify the release ZIP with its published SHA-256.
2. Extract it to the SD card root.
3. Confirm these files exist:

   ```text
   atmosphere/contents/4200000000007E09/exefs.nsp
   atmosphere/contents/4200000000007E09/toolbox.json
   switch/.overlays/SWOTS.ovl
   ```

4. Restart the Switch.
5. Open Tesla → SWOTS → `Motion cues` → `On`.

The package must not contain `boot2.flag`; Tesla starts the renderer when needed.

## Use

- Configure opacity, dot radius, sensitivity, and smoothing in Tesla → SWOTS.
- Press `B` on the SWOTS main screen to close the overlay and resume cues.
- Select `Stop and return to Tesla` to stop cues and return to Tesla.
- The Tesla shortcut also opens and closes the overlay.
- `Off` removes the cues and stops the renderer.
- Cues respond only to physical console/controller motion, not the in-game camera.

Settings and logs: `/config/swots/settings.cfg`, `/config/swots/renderer.log`, and `/config/swots/sensor.log`.

See [troubleshooting](docs/troubleshooting.md) for `0x0000FC01`, missing cues, sensor problems, or Tesla conflicts.

## Build and test

```sh
source scripts/env.sh
make setup-libtesla
make verify -j2
./scripts/deploy.sh /path/to/SWITCH_SD
```

Host tests do not need devkitPro:

```sh
env -u DEVKITPRO make test
```

Build and verify the release:

```sh
make dist -j2
cd dist
sha256sum -c SWOTS-v0.1.0-alpha.2.zip.sha256
```

NPDM compatibility depends on the tested `ffff0400` capability bytes. Changes to the toolchain, permissions, VI, HID, or PM require hardware testing. Title ID: `4200000000007E09`.

## Upgrade or uninstall

Turn cues `Off` before replacing files. Restart after upgrading; settings remain in `/config/swots/settings.cfg`.

To uninstall, turn cues `Off`, restart, then delete:

```text
atmosphere/contents/4200000000007E09/
switch/.overlays/SWOTS.ovl
/config/swots/ (optional)
```

## Project

SWOTS is alpha software with broad legacy-compatible NPDM permissions. Use trusted builds and verify release hashes. See [contributing](CONTRIBUTING.md), [security](SECURITY.md), and [third-party notices](THIRD_PARTY_NOTICES.md).

SWOTS is independent and is not affiliated with KineStop or Nintendo.

Licensed under [GPL-2.0-or-later](LICENSE).
