# Troubleshooting

Set SWOTS to Off before replacing SD card files. Restart first if Tesla or controller input is broken.

## `Couldn't start` and log result `0x0000FC01`

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
2. Move the Switch itself. `source=Console` is the preferred source in handheld
   mode; controller sources are fallbacks.
3. Camera movement inside a game has no effect.
4. Read `/config/swots/sensor.log`.
5. The log keeps up to 32 KiB of records, writes a normal heartbeat every 30
   seconds, and is always limited to one record per second. Compare consecutive
   `sampling` values.
6. `NoSamples` means no fresh report for at least 250 ms; `WaitingRetry`
   means the source was released after one second without reports.
7. `ZeroData` means sampling numbers advanced but the three-axis acceleration
   did not contain physical gravity, so SWOTS rejected that source.
8. If `sampling` stops increasing, report the controller, connection, system
   versions, `last_attempt`, `last_start_result`, `console_init_result`, and
   `console_start_result`. A nonzero console result means SWOTS automatically
   fell back to ordinary controller HID. A console stream that becomes stale
   is retried only after a 30-second cooldown.
9. Games which activate a real controller six-axis stream should switch from
   `Console` to `Handheld`, `FullKey`, or `JoyDual` after about 600 ms and show
   one upper-left `MOTION: CONTROLLER` toast. The exact inactive vector
   `(0,0,-1)` with a zero gyroscope is treated as a placeholder and cannot
   trigger a promotion.

## Tesla or Buttons Stop Working

- B on the SWOTS main screen closes the overlay and resumes cues.
- Use `Stop and return to Tesla` to stop cues before returning.
- The Tesla shortcut also opens and closes the overlay.
- Restart if input is already broken.
- If Off cannot stop the cues, restart before replacing the overlay or
  renderer, then confirm both files came from the same release.

After B closes SWOTS, the dots should return without blocking normal input.

## Settings Do Not Save

- Change a value with Left or Right. Press A to save and return.
- Check that `/config/swots/settings.cfg` is writable.
- When cues are On, a successful save is sent to the renderer before the
  settings page closes. It takes effect after Tesla closes.
- To reset, back up and delete `settings.cfg`, `settings.tmp`, and `settings.bak`.

## MTP Paths on Linux

`mtp://...` is a file-manager URI, not a shell path. Connect with DBI or a file manager, then use the GVfs mount path:

```text
/run/user/<uid>/gvfs/mtp:host=<device>/1: SD Card
```

Quote paths with spaces. Wait for transfers to finish before disconnecting.
