# Troubleshooting

Set SWOTS to Off before replacing SD card files. Restart first if Tesla or controller input is broken.

## `Start failed: 0x0000FC01`

An old system probably rejected the NPDM capabilities.

1. Install `exefs.nsp` from the release package.
2. Run `make verify`.
3. Keep the legacy `ffff0400` capability. Do not casually change the NPDM JSON.
4. Report your HOS, Atmosphere, and nx-ovlloader versions.

## Running, but No Dots

Read `/config/swots/renderer.log`.

- `viInitialize` or `0x0000E401`: VI startup may be incompatible.
- `framebufferMakeLinear` or `0x00000559`: the heap may be too small.
- No new log: check that `/config/swots/` is writable and the Title ID path is correct. Then restart.

Report the last `stage` and full Result value.

## Dots Do Not Move

1. Set Sensitivity to 70-100%.
2. Move the active sensor source: the Switch in handheld mode, or the active controller in docked mode.
3. Camera movement inside a game has no effect.
4. Read `/config/swots/sensor.log`.
5. If `sampling_number` stops increasing, report the controller, connection, and system versions.

## Tesla or Buttons Stop Working

- B on the SWOTS main screen closes the overlay and resumes cues.
- Use `Stop and return to Tesla` to stop cues before returning.
- The Tesla shortcut also opens and closes the overlay.
- Restart if input is already broken.
- If removing `/config/swots/enabled.flag` fixes it, include `renderer.log` in the report.

After B closes SWOTS, the dots should return without blocking normal input.

## Settings Do Not Save

- Change a value with Left or Right. Press A to save and return.
- Check that `/config/swots/settings.cfg` is writable.
- Settings reload about every 500 ms.
- To reset, back up and delete `settings.cfg`, `settings.tmp`, and `settings.bak`.

## MTP Paths on Linux

`mtp://...` is a file-manager URI, not a shell path. Connect with DBI or a file manager, then use the GVfs mount path:

```text
/run/user/<uid>/gvfs/mtp:host=<device>/1: SD Card
```

Quote paths with spaces. Wait for transfers to finish before disconnecting.
