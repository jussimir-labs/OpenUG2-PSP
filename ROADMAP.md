# Closed beta roadmap

**Current release: Closed Beta 1 — RC21.** Jussimir accepted this candidate for the first
test group on 27 October 2026. [Release notes](RELEASE_NOTES.md) · [Known issues](KNOWN_ISSUES.md).

## Available for the first group

- [x] Freeze the tested RC21 executable and match the Xbox/PS2 physics data to it.
- [x] Include the local browser generator, source validation and output identity checks.
- [x] Preserve all 28 movies in the complete generation recipe, with skipping available in play.
- [x] Integrate source-derived suspension and handbrake, Quick Race/URL traffic fixes and reverb parsing.
- [x] Retain sponsor-screen recovery, audio-bank lifetime fixes, native NOS/PURGE and original-derived Drift.
- [x] Provide English installation, first-session testing and illustrated reporting instructions.
- [x] Record the organizer's manual acceptance for this first beta separately from automated evidence.

## Next validation and fixes

- [ ] Ship self-contained native generator packages for Windows x64 and macOS, with bundled Python, libraries and prebuilt conversion tools. Remove manual WSL/Homebrew/compiler setup and validate complete generation on clean systems. Implementation resumed on 27 October at Jussimir’s request. Native packages preserve the RC21 executable and reference validation; complete ISO conversion on every OS remains a separate qualification step.
- [ ] Shorten complete game generation. RC21 from scratch on an Apple M5 Pro Mac: 55 min (PS2) and 60 min (Xbox); 3 h 06 min (PS2) on Windows on a Steam Deck. The next generator converts several steps at once, in the order their files require, and audio, race lines, city/track races and screen maps are faster: on that Mac PS2 now takes 17 min and Xbox 20 min, with output identical to RC21, and on Windows on the Steam Deck PS2 takes 1 h 41 min, with the same reference verification. It can also update a previous generation: only steps whose converter, options or inputs changed are converted again, and a release that only replaces the game executable updates in seconds (6 s on the Mac, 27 s on the Steam Deck). Updating an RC21 generation to this version was verified on both, with output identical to a complete generation; because this version changed the slowest converters, that first update takes almost as long as a complete generation. Next: measure on Intel Macs, give the critical step priority on 4-core computers, and speed up race lines, the remaining critical path.
- [ ] Confirm complete generation and installation on testers' native Windows/macOS setups, and the legacy Linux source kit.
- [ ] Gather current-device visual/audio feedback and recheck existing reports #1–#5.
- [ ] Extend Career, natural rewards, sponsor progression and native save/cold-load coverage.
- [ ] Resolve Drag 4401 rival recovery.
- [ ] Implement and validate the Stability Control On path beyond this RC21 executable.
- [ ] Extend handling comparisons across cars, upgrades, tracks and special branches.
- [ ] Continue tone/materials/UV/visibility/doors and complete audio/movie review.
- [ ] Validate the original US PS2 disc image independently of the retained file-level reference.
- [ ] Establish PSP-1000 compatibility, model-specific memory behavior and stable 30 fps throughout the game.

Completing a packaging or automated check does not close a user report or establish final release quality.
Earlier executable test receipts remain historical and are not relabelled as RC21 results.
