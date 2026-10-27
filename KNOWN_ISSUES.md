# Closed Beta 1 — RC21: known issues

RC21 was accepted by Jussimir for the first closed beta after his manual test on 27 October 2026.
This acceptance is limited to starting the beta. It does not establish full-game completion,
all-device compatibility, PSP-1000 support or stable 30 fps throughout the game.

## Before testing

- **Extended memory:** this executable uses `PSP_LARGE_MEMORY=1`. PSP-1000 compatibility is
  not established. Contact the organizer if that is your only device. Other models, firmware,
  Vita/Adrenaline and PPSSPP combinations need their own reports.
- **Stability Control:** leave it **Off**. The On option is present but has no implemented
  effect in this exact RC21 executable.
- **Updates:** use a new generator output and install the complete result. RC21 changes both
  `EBOOT.PBP` and `pack.n2pk`; an executable-only update keeps stale physics.
- Back up your saves. Broad physical save/reload and full-career coverage remain pending.

## Gameplay and presentation

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

- [EA TRAX badge/cap (#1)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1):
  order, proportions and later presentation work are included; please confirm while playing.
- [Cuts/clicks during Drift (#2)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2):
  bank lifetime and loop corrections are included; the reported audible symptom still needs listening.
- [Loop during pause (#3)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3):
  pause/environment cleanup and limiter changes are included; please identify the sound and steps.
- [Braking sound (#4)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4):
  braking inputs and rival emitters have fixes; target listening across handling paths remains necessary.
- [Music after Drift (#5)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5):
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
