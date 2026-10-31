# NFSU2 PSP closed beta — release notes

## 0.2.3 — Rocket Ride (31 October 2026)

A new game executable and new game data: update your 0.2.0, 0.2.1 or 0.2.2 output folder with this
generator (same ISO) or generate into a new folder, and install the complete `PSP/GAME/NFSU2VIEW` folder.

- **Added:** the Visual Rating meter in the shops (#58).
- **Changed:** the underglow's light on the road is each car's own pattern from the original game (#47).
- **Fixed:** the white glint on matte trim (#21), invisible stubs along walls and highway barriers (#32),
  the skid sound over the first Garage movie (#41), what lifts with the trunk lid (#42), drag lane
  changes (#43), Rachel's 350Z parts and the turn in fast corners (#45, in part; #55), the free-roam
  rivals on slopes (#46), the neon's light under the asphalt (#47), the music after suspending the PSP
  (#48), nitrous flames from the car's own exhaust tips (#49), the side mirrors (#50, #56), rim sizes per
  car (#56), Rachel's calls after the prologue (#57), brake upgrades on the car (#59) and the City Core
  Graphics Shop floor (#60).

The complete list is in [CHANGELOG.md](CHANGELOG.md).

## 0.2.2 — Riders on the Storm (30 October 2026)

A new game executable and new game data: update your 0.2.0 or 0.2.1 output folder with this generator
(same ISO) or generate into a new folder, and install the complete `PSP/GAME/NFSU2VIEW` folder.

- **Fixed:** drafting and other running bonuses shown while they happen (#24), the camera held on the
  career's opening and grid scenes (#25), leaving the first Car Lot in Rachel's 350Z (#26), traffic
  dropping off the overpass ramp (#27), the high-pitched buzz against a wall (#28), the menus' held
  directions, icons, turntable and lights (#29, in part), decals off the paint (#30), wheel positions
  (#31), rain inside tunnels (#33), shop markers on the street (#34), package discounts (#35), each
  shop's own room (#36), the chase camera inside tall walls (#37), floating street markers (#38), the
  SUVs' tail lights (#39) and the Custom Gauges tachometer (#40).
- **Added:** the analog stick turns your car in the menus (#29).

The complete list is in [CHANGELOG.md](CHANGELOG.md).

## 0.2.0 — PSP-1000 support and the first tester fixes (30 October 2026)

A new game executable and new game data: update your 0.1.x output folder with this generator (same
ISO) or generate into a new folder, and install the complete `PSP/GAME/NFSU2VIEW` folder.

- **PSP-1000 support, in testing.** One download for every model: at boot the game measures the memory
  it gets and picks the extended profile (32 MB or more) or the compact one (PSP-1000, or extended memory
  off). Tested on a PSP-3000 limited to the PSP-1000's memory and in PPSSPP; not yet on a physical PSP-1000.
- **Fixed:** Stability Control On (#12), car lights on the asphalt in PPSSPP (#13), manual transmission in
  free roam and career (#14), the Hotel Plaza ramp (#15), sponsor events after loading a save (#16), the
  minimap after Street X (#17), traffic in career Street X (#18), career race trigger zones (#19), exhaust
  tips (#20), the 350Z's reverse lights (#21), stock spoilers (#22), title-screen menu sounds (#23), and a
  race-line calculation that could run forever on some processors with the Xbox disc.
- **Changed:** a loading screen that stops progressing for 40 seconds shows a technical panel to
  photograph for your report; the game version is on the title screen and above the debug lines.

The complete list is in [CHANGELOG.md](CHANGELOG.md).

## Closed Beta 1 — RC21 (0.1.x)

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
