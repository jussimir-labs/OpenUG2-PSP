# Changelog

All notable changes to the NFSU2 PSP closed beta are listed here, newest first.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **0.y.z** are beta versions; **1.0.0** will be the first stable release.
- The **middle number** goes up with a larger change to the game (0.2.0: the first beta
  with PSP-1000 support).
- The **last number** goes up with fixes, and with generator updates that leave the game unchanged.

Each version is a release on this repository, tagged `vX.Y.Z`, and its section below is the
text of that release. The game shows its version on the title screen and above the debug
lines; please include it when you report a problem. Test builds carry a suffix, such as
`0.2.0-rc.1`.

## [Unreleased]

## [0.2.1] - 2026-10-30 — Generator fix for Windows

The game is unchanged: the same 0.2.0 executable and the same game data; only the generator changed.
Updating a 0.2.0 output folder with this generator (same ISO) takes seconds: no conversion
step runs again.

### Fixed

- Generator: on some Windows computers the conversion stopped before its first step, with
  `OSError: [Errno 22] Invalid argument` while writing `.tools/browser-generator.log`. That log
  is only a copy of what the page shows, so the generator now reports the problem once and
  carries on with the conversion.

## [0.2.0] - 2026-10-30 — PSP-1000 support and the first tester fixes

A new game executable and new converted game data: update your 0.1.1/0.1.2 output folder with
the 0.2.0 generator (same ISO) or generate into a new folder, and install the complete result.

### Added

- PSP-1000 support, in testing: on a console without extra memory (a PSP-1000, or a later
  model with extended memory turned off) the game picks a compact memory profile by itself.
  It has been tested on a PSP-3000 limited to the PSP-1000's memory and in an emulator set
  to the PSP-1000, not yet on a physical PSP-1000.
- The game's version on the title screen, above the debug lines, in the XMB's game
  information and in bug files (`bug-N.n2bug`).
- The debug overlay shows memory in use and the memory profile's budget next to the frame rate.

### Changed

- Drift handling with Stability Control Off follows the original game's rotation limit more closely.
- Traffic cars knocked backwards in a collision now stop instead of driving away in reverse.

### Fixed

- Stability Control On now changes the handling as in the original game, Drift included: more
  rear grip when the car slides and a cap on how fast it rotates (and slightly less tyre grip
  outside Drag and Drift). Rivals keep their own handling. ([#12])
- The car lights on the asphalt are no longer a solid white patch in PPSSPP's hardware
  renderers at higher resolutions. ([#13])
- Manual transmission from Options > Player now applies in free roam and in career races,
  not only in Quick Race; Rachel's 350Z no longer shifts by itself. ([#14])
- The car no longer gets stuck on the steep ramp around the island at Hotel Plaza, which
  behaved like an invisible wall. ([#15])
- A sponsor's events no longer disappear from the map and the city after reloading a save
  (EUROLITE Drift events, for example). The one-time freeze on the loading screen in the same
  report was not reproduced; if a load stops advancing for 40 seconds, the loading screen now
  shows a panel of technical details: please photograph it for your report. ([#16])
- The minimap returns to the city after a Street X event instead of keeping the race's map. ([#17])
- Career races are offered in the original game's trigger zones around Bayview, not at the
  start line of the course. ([#19])
- No traffic cars appear in career Street X events, as in the original game. ([#18])
- The Mazda MX-5's exhaust tip points the right way, in the garage and on the rivals' cars;
  the same rule now places every car's exhaust tips. ([#20])
- Rachel's 350Z shows its reverse-light lenses, and other car parts painted with the car's
  own skin show it. ([#21])
- The stock Civic has its rear spoiler again, as do other cars whose stock spoiler is a separate
  part, including the rivals' stock Eclipse, Impreza, Lancer, Skyline and Supra. ([#22])
- The title screen keeps quiet while *Riders on the Storm* plays: other buttons no longer make
  menu sounds, and START plays the accept sound as it opens the main menu, as in the
  original. ([#23])
- Generator: the race-line step can no longer run for hours on some computers (reported with
  an Xbox disc on an Intel Core i5-12400); a stuck calculation gives up after 10 minutes and
  keeps the line it already has.
- PSP-1000 memory profile: a career race started right after saving no longer waits about a
  minute on the loading screen.
- PSP-1000: a Drift event right after another race no longer hangs on the loading screen ([#16]).
  When memory is tight, that Drift uses the previous drift handling and a notice says so.

## [0.1.2] - 2026-10-29 — Faster generator, updates and Linux

The game is unchanged: the same RC21 executable as 0.1.0; only the generator changed.

### Added

- Native Linux package for x86-64 (glibc 2.35 or later), including SteamOS on a Steam Deck or
  Steam Machine in Desktop Mode.
- Update instead of regenerating: choose the same ISO and your previous output folder, and only
  the steps whose converter, options or inputs changed are converted again. The result is
  verified like a complete generation.

### Changed

- Faster complete generation: several conversion steps run at once, and the audio, race-line,
  race and screen-map conversions are faster (PS2 on a Steam Deck running Windows: 1 h 41 min
  instead of 3 h 08 min; Apple M5 Pro Mac: 17 min instead of 55 min). The output is identical
  to 0.1.0 on the same computer.

## [0.1.1] - 2026-10-28 — Native generator

The game is unchanged: the same RC21 executable as 0.1.0; only the generator changed.

### Added

- Native generator packages for Windows (64-bit) and macOS (Apple Silicon and Intel), with
  Python, the conversion libraries and the audio/movie tools included: nothing to install.
- Quick Start guide (PDF).
- Xbox-only players get the port's own drawn button icons in the HUD and garage; no PS2 disc is needed.

### Changed

- The garage and vinyl conversions are much faster (about 20× and 6×, with identical output).
- On Windows and Intel Macs some movie, audio, geometry and race-line files are reported as
  *equivalent* to the reference (the same structure, rounded differently by an x86 processor),
  which is a successful result.

## [0.1.0] - 2026-10-27 — Closed Beta 1 (RC21)

The first closed beta: Rachel, the 350Z and the streets of Bayview on a PSP (build
`cdf93ef2c7`, executable SHA-256 `42d252df…`). An extended-memory build for the PSP-2000, PSP-3000 and
PSP Go; PSP-1000 support, all-device support, full-career completion and stable 30 fps
throughout the game were still pending.

### Added

- Kit with the tested RC21 PSP executable, the source-data converters and a local browser
  generator for your own US Xbox or US PS2 (SLUS_210.65) disc image.
- Source-derived suspension and handbrake data, native NOS/PURGE data and Drift handling
  derived from the original game.
- English setup, generation, installation and bug-reporting instructions, with an illustrated
  bug guide.

### Changed

- Quick Race and URL traffic behaviour, reverb-module parsing and menu focus updated since the
  earlier test kits.

### Fixed

- Sponsor-screen and audio-bank ownership problems found in earlier test kits.

[Unreleased]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases/tag/v0.1.0
[#12]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/12
[#13]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/13
[#14]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/14
[#15]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/15
[#16]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/16
[#17]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/17
[#18]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/18
[#19]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/19
[#20]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/20
[#21]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/21
[#22]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/22
[#23]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/23
