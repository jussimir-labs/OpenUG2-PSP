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
- [ ] Shorten complete game generation. Measured with the 49fdc46 native packages on 28 October: 57 min (PS2) and 59 min (Xbox) on an Apple M5 Pro Mac, and 3 h 06 min (PS2) on Windows on a Steam Deck. Slowest steps: audio, screens/HUD, race-start scenes and vinyls on the Mac; race lines, city races, city and garage on the Deck. Garage (~20×) and vinyl (~6×) conversions are already faster with identical output. Next: speed up the remaining steps, reuse unchanged work between generations, and re-measure on Mac and Windows with reference verification.
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
