# Compatibility

SWOTS depends on Atmosphere, Tesla, nx-ovlloader, VI managed layers, and HID six-axis input.

## Tested

| HOS | Atmosphere | Tesla / nx-ovlloader | Result |
|---|---|---|---|
| 18.1.0 (emuMMC) | 1.7.1 | Custom or rebuilt versions | Core features pass on commit `02d4502` with the legacy NPDM `ffff0400` capability |

Atmosphère 1.7.1 was identified from the official `package3` hash. The installed Tesla Menu and nx-ovlloader do not match any official release through Tesla v1.2.3 or nx-ovlloader v1.0.7. This is not proof that every older version works.

## Test Checklist

1. Start SWOTS from Tesla. Confirm `Renderer: Running` and visible dots.
2. Confirm normal game input still works.
3. Open and close Tesla with its shortcut. Confirm the dots return.
4. Press B on the SWOTS main screen. Confirm SWOTS closes and the dots return.
5. Select `Stop and return to Tesla`. Confirm the renderer stops before Tesla appears.
6. Select Off. Confirm the dots disappear and the renderer stops.
7. Save settings. Confirm they work after a restart.
8. Move the active controller. Confirm smooth motion without idle drift.
9. Confirm screenshots exclude the dots. If possible, confirm capture-card output includes them.

Use the report template in [CONTRIBUTING.md](../CONTRIBUTING.md). Add a matrix row only after testing every item.
