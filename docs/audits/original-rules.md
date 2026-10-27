> Historical audit of executable bf7f. For the overnight corrections and their exact qualification, read [NIGHTLY_NOTES.md](../../NIGHTLY_NOTES.md).

# Original rules: rival neon and drift handling

Source comparison performed on October 25, 2026 for executable `bf7f21e3`.
The four source files reviewed matched the frozen release-build-06 source:
`main.c`, `steering.c`, `perf.c` and `physics_ug2.c`.
This was a static audit, with no new visual, dynamic or hardware test.

## Rival neon, including free roam

The port already assigns and draws neon for free-roam/Outrun rivals.
Its selection rule comes from the original game; it does not require every
rival to have neon.

The original GameCube `Speed.elf` identifies
`RaceStarter::AddRandomEncounterCars` at `0x8011a7d8`. Its pipeline assigns
random parts and paint, initially selects a level-2 neon in slot `0x9a`,
then matches opponent performance to the player. That match calls
`RideInfo::SyncVisualPartsWithPhysics` (`0x80244a38`), which calls
`RideInfo::BuildRandomRide` (`0x802470a8`) and selects visual parts again.
The final neon therefore follows the upgrade level, rather than being
unconditionally fixed at level 2.

Reading the ELF's data independently confirmed the slot `0x9a` record at
`0x803717e4` in `RandomPartUpgrades` (`0x80371694`). It contains these weights:

| Car upgrade level | No neon | Part level 1 | Part level 2 | Part level 3 |
|---|---:|---:|---:|---:|
| 0 | 90% | 10% | 0% | 0% |
| 1 | 40% | 50% | 10% | 0% |
| 2 | 30% | 20% | 50% | 0% |
| 3 | 30% | 10% | 30% | 30% |

The port's `ride_level` and `neon_roll` reproduce this table. `rivals_load`
assigns the result to the free-roam rivals, and `draw_rival` renders it.
The implementation uses its own random generator, catalog and performance
packages. Complete equivalence for all individual upgrade combinations was
not established. Seeing a rival without neon is consistent with the rule,
especially early in the career.

## Drift handling

The steering derives from the original `PlayerSteering`. Its drift base
steering rate is 110 degrees per second, compared with 180 normally, before
speed/input modifiers. These values were confirmed in
`PlayerSteering::CalculateSteeringSpeed` (`0x80023ab4`). Nine steering tables
were also read directly from the ELF and matched the port, including the
drift input curve, countersteering curve and yaw control data.

This does not establish full handling parity. The port converts the steering
angle into the normalized command expected by its adapted OpenUG2 physics
model. Its special mode tuning applies mass, drive distribution and brake
balance, while the original `ApplySpecialTunings` (`0x801c5c34`) additionally
adjusts downforce, center of mass, axle tire properties and inertia.
Those original mode-specific adjustments are not all implemented in the port.

The port also has `drift_grip`, an empirical adjustment calibrated against
PS2 Peugeot 206 measurements. After sustained steering above 0.7 for 0.7
seconds, it gradually increases its lateral handling parameter by up to 10%
over 0.5 seconds. It was not traced to an identified original algorithm.

The supported conclusion is that drift steering uses original rules and
values, while the complete physics remains adapted and requires further
comparison. Open handling work remains in the [roadmap](../../ROADMAP.md).
