# NFSU2 PSP closed beta 0.2.3: known issues

0.2.3 is a closed-beta release. It does not establish full-game completion,
all-device compatibility, physical PSP-1000 support or stable 30 fps throughout the game.

## Before testing

- **Memory profiles:** one executable for every model. At boot the game picks the extended
  profile (32 MB or more) or the compact one (PSP-1000, or extended memory off). The compact
  profile was measured on a PSP-3000 limited to 24 MB for games and in PPSSPP set to the
  PSP-1000; **no physical PSP-1000 has run it yet**. Other models, firmware, Vita/Adrenaline and
  PPSSPP combinations need their own reports.
- **PSP-1000 and Drift:** a Drift right after a city race may use the previous drift handling
  when memory is tight (a notice says so).
- **Updates:** update your 0.2.0, 0.2.1 or 0.2.2 output folder with the 0.2.3 generator (same ISO)
  or generate into a new folder, and install the complete result. 0.2.3 changes `EBOOT.PBP` and
  converted data (cars, rims, brakes and mirrors, neon underglow, the shops' Visual Rating, city,
  tracks and walls); an executable-only update keeps old data that the new executable does not
  expect, and the old executable cannot read the new cars and rims.
- Back up your saves. Broad physical save/reload and full-career coverage remain pending.
- **Xbox source button icons:** the PSP prompts use the PS2 disc's pad icons. A game generated
  from the Xbox ISO uses the port's own drawn icons instead (same buttons, slightly different look).

## Gameplay and presentation

- Handling (#45): fast corners now turn harder with the downforce, about 88% of the PS2's turn
  rate in the case we measured; the analog stick's travel and the weight transfer some of you
  reported are still open.
- Menus (#29): still open are the menu camera's height and distance on the stick, the shop
  rooms' own lighting and reflections, the Body Shop's part browser and camera angles, and
  each car's own ride height in the menus.

- Rivals may become stuck in repeated recovery in **Tunnel Construction / Drag 4401**.
- Suspension and handbrake changes have bounded directed checks, not an all-car/all-upgrade
  original-physics parity approval. Human Drift uses a separate original-derived Core.
- Custom dyno and special presets retain the earlier whole-event handling path.
- Horizontal collisions retain a 2D adaptation; extreme behavior, hydraulics and special
  branches remain incompletely qualified.
- Full-career progression, natural rewards and every native save/cold-load path have not
  been requalified on RC21. Earlier executable results remain historical.
- Tone, materials, UVs, visibility/culling, doors and all camera profiles need broader review.
- All 28 movies are structurally available in both source references. This does not verify
  playback of every movie or every audio family on every device. Movie skipping remains available.
- Global stable 30 fps and PSP-1000 memory/performance targets remain unfinished.

## Existing reports to recheck on RC21

Please include the device, source, car, mode/track and exact steps. Fixes in code do not
automatically close a person's observation.

- [EA TRAX badge/cap (#1)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/1):
  order, proportions and later presentation work are included; please confirm while playing.
- [Cuts/clicks during Drift (#2)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/2):
  bank lifetime and loop corrections are included; the reported audible symptom still needs listening.
- [Loop during pause (#3)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/3):
  pause/environment cleanup and limiter changes are included; please identify the sound and steps.
- [Braking sound (#4)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/4):
  braking inputs and rival emitters have fixes; target listening across handling paths remains necessary.
- [Music after Drift (#5)](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/5):
  report the exact event/menu transition. Drift/Drag keep their original ambience policy.

Master Volume remains the port's final-output policy; complete original mix parity is not established.
The reverb parser now reads counted FXEDIT modules, but the port does not reproduce the complete
original DSP graph.

## Source and tooling limitations

- US Xbox and PS2 SLUS_210.65 conversion paths are included. The original US PS2 disc image
  itself is not independently verified; retained file-level evidence is distinct from disc-image
  validation. PAL and other revisions are refused. See [source details](tools/PS2_SOURCE.md).
- Windows requires WSL. Setup and conversion are not qualified on every Windows/Linux/macOS setup.
- File identity against the reference verifies the generated package, not gameplay quality.
- This release reused frozen whole-package evidence and rechecked the changed asset producers;
  it did not repeat a complete multi-hour ISO conversion during release packaging.
- Historical PSPLink shutdown problems are separate from the normal XMB installation instructions;
  no general PSPLink shutdown fix is claimed here.

[Instructions](README.md) · [Test checklist](docs/TESTING.md) ·
[Qualification receipt](docs/qualification/RC21-QUALIFICATION.json)
