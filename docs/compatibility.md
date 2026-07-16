# Compatibility

SWOTS depends on Atmosphere, Tesla, nx-ovlloader, VI managed layers, and HID six-axis input.

## Tested

| HOS | Atmosphere | Tesla / nx-ovlloader | Result |
|---|---|---|---|
| 18.1.0 (emuMMC) | 1.7.1 | Custom or rebuilt versions | Core features pass on commit `02d4502` with the legacy NPDM `ffff0400` capability |

Atmosphère 1.7.1 was identified from the official `package3` hash. The installed Tesla Menu and nx-ovlloader do not match any official release through Tesla v1.2.3 or nx-ovlloader v1.0.7. This is not proof that every older version works.

## Test Checklist

1. Set `Motion cues` to On, close Tesla, and confirm visible dots.
2. Confirm normal game input still works.
3. Open and close Tesla with its shortcut. Confirm the dots return.
4. Press B on the SWOTS main screen. Confirm SWOTS closes and the dots return.
5. Select `Stop and return to Tesla`. Confirm the renderer stops before Tesla appears.
6. Select Off. Confirm the dots disappear and the renderer stops.
7. While cues are On, change settings and press A. Confirm the first resumed
   frame uses them and that they remain after a restart.
8. Change settings and leave with B, Home, Power, the Tesla shortcut, and touch
   dismissal. Confirm the draft is neither saved nor applied.
9. Move the active controller. Confirm smooth motion without idle drift.
10. Confirm screenshots exclude the dots. If possible, confirm capture-card output includes them.
11. Confirm Home, Power, sleep/wake, and repeated hide/reopen cycles preserve
    cue ownership without leaving a renderer process after Off.

Use the report template in [CONTRIBUTING.md](../CONTRIBUTING.md). Add a matrix row only after testing every item.
