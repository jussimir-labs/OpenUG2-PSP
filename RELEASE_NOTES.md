# Closed Beta 1 — RC21

## Generator update — 29 October 2026

The game is unchanged: the same RC21 executable and the same verified output. Only the
generator on your computer changed.

- **Faster complete generation.** Several conversion steps now run at once, in the order their
  files require, and the audio, race-line, race and screen-map conversions are faster. A
  complete PS2 conversion took 1 h 41 min instead of 3 h 08 min on a Steam Deck running Windows,
  and 17 min instead of 55 min on an Apple M5 Pro Mac (Xbox: 20 min instead of 60 min).
  The output is identical to RC21 on the same computer and passes the same reference verification.
- **Update instead of regenerating.** Choose the same ISO and your previous output folder:
  only the steps whose converter, options or inputs changed are converted again. A release
  that only replaces the game executable updates in seconds. Updating an RC21 folder to this
  generator takes about as long as a complete generation, because this release changed the
  slowest converters; the result was verified identical to a complete generation on Mac and Windows.
- **Native Linux package.** `NFSU2-Generator-linux-x64.zip` runs on Linux x86-64 with glibc 2.35
  or later: SteamOS on a Steam Deck or Steam Machine, Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch
  and similar. Like the other packages it includes Python and every converter. On SteamOS, use
  Desktop Mode and open `Generate-game.sh`; see [the instructions](README.md). Use it instead of
  the Windows package under Proton. On a Steam Deck with SteamOS 3.9 a complete PS2 generation
  from the browser page took 1 h 32 min and matched the RC21 reference (97 identical and 62
  equivalent files), and an update of the same folder with no changes took 20 seconds.
- **Not yet tested:** the Intel Mac package, an Xbox conversion on Windows or Linux, Linux
  distributions other than SteamOS, the Plasma Wayland session (Desktop Mode from the Steam
  button uses X11, which was tested), the Steam Machine itself, and updating an RC21 folder from
  the browser page (verified with the command the page runs).


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
