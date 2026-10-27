# Closed Beta 1 — RC21

**27 October 2026 · build `cdf93ef2c7` · extended-memory beta**

Thank you for joining the first test group. Jussimir tested RC21 and accepted it as good
enough to begin this closed beta. Please read [the instructions](README.md) before generating
and installing the game, and check [known issues](KNOWN_ISSUES.md) before reporting a problem.

## Included in this kit

- The exact tested RC21 prebuilt PSP executable; it has not been recompiled for this release.
- The matching source-data converters, manifests and local browser generator for US Xbox
  and US PS2 SLUS_210.65 input.
- Source-derived suspension and handbrake data, updated Quick Race/URL traffic behavior,
  corrected reverb-module parsing and menu focus work accumulated since the earlier kit.
- Previous sponsor-screen and audio-bank ownership corrections, native NOS/PURGE data and
  original-derived human Drift support, with their existing limitations retained.
- English setup, generation, installation and reporting instructions, plus the illustrated bug guide.

No ISO, original game-data package or save is distributed. Generate the data locally from your own disc.

## Important for this beta

Use the full kit and a new output folder. **Both `EBOOT.PBP` and `pack.n2pk` change** from
earlier candidates. An executable-only update is insufficient. Back up saves and keep source
versions separate. Leave **Stability Control Off**. Keep debug overlays visible for reports.

RC21 uses extended memory. PSP-1000 compatibility, all-device support, full-career completion
and global stable 30 fps remain pending. This release is a pre-release for the selected group,
not a final quality approval.

## Verification and evidence

The retained RC21 evidence includes five host tests / 3,043 assertions and six bounded headless
scenarios: drive controls with old/new physics, Xbox/PS2 source data, an Outer Ring excerpt,
normal-control Drift4312 and URL4701 restart/menu paths. There are 28 structurally available
movies per source. These are not six complete races or a full save/performance qualification.
Jussimir's later manual beta acceptance is recorded separately from those automated checks.

Package SHA-256 values are provided in `SHA256SUMS.txt` on the release. The executable hash is
`ff35c0a14190edf67de263259bfb9fa3831a75363ee2c7b7ee332196bb29df39`.
Xbox data ID: `bfaa`; PS2 data ID: `804e`.

Please report one problem per issue using **Closed Beta 1 — RC21** as the version.
