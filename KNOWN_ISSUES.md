# Closed beta: scope and known issues

Candidate 14 (`8c51055e`) has local PPSSPP and native host evidence, detailed in [nightly notes](NIGHTLY_NOTES.md). Matching a generated-file reference verifies file identity; it does not approve all gameplay, devices or performance.

## Reports still open

Please recheck these on the new executable and report the device, source, car, track and steps. Code corrections do not close a reporter's observation automatically.

- [EA TRAX badge/cap (#1)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1): draw order and circular proportions were corrected and reviewed locally. Confirm the result in play.
- [Cuts/clicks during Drift (#2)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2): separate bank ownership and complete long-loop cursors repair confirmed defects. The exact audible symptom still needs reporter listening; its hardware cause is not fully isolated.
- [Loop during pause (#3)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3): local pause tests muted engine requests; environment cleanup and pending limiter ramps were corrected. These findings do not identify every sound in the original report.
- [Braking sound (#4)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4): acceleration and tyre-frame inputs are corrected. The human Drift path now supplies native wheel/body state; other handling paths retain port estimates. Racing and free-roam rival emitters are now implemented and passed directed headless checks. Listening and all-branch sound parity remain pending.
- [Music after Drift (#5)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5): return-to-menu recovery passed in current bounded runs. Please verify it on your device.

## Handling and progress

- **Human Drift:** the original-derived Core is integrated for the supported 29 US source car records, performance parts and nitrous. Local driving covered short stock-206 excerpts on 4312 automatic/manual and 4174. All-car, all-upgrade and whole-track original parity are not approved. Historical yaw comparisons from older candidates do not measure this new executable.
- **Custom dyno and special presets:** these select the previous handling model for the entire event before it starts. Five legacy sliders are not guessed into the original tuning format. There is no mid-race fallback from a refused Core step.
- **Contacts and special behavior:** horizontal collisions remain a 2D port adaptation. Wild/extreme, hydraulics and other unqualified branches remain limits.
- **Career/SAVE:** current private tests include existing earned fixtures from Xbox and PS2, plus one naturally won full Xbox 4101 race, its $250/REP 300 reward, native SAVE and one cold process restoring the new state. This is not a complete-career, all-race or physical-save approval. Your existing saves are not automatically replaced by the generator.
- **4401:** rivals may get stuck and repeatedly recover in Tunnel Construction/drag. This remains deliberately deferred.

## Devices, source and presentation

- **Hardware:** no run or installation of 8c51 on a PSP or Memory Stick. Earlier bf7f PSP-3000/card observations remain historical; RC13 emulated career/save proofs belong to e7ce. PSP-1000, PSP-2000, Go and physical save behavior remain unqualified.
- **Performance:** one calibrated Drift scene is recorded separately; global 30 fps and all-device memory/performance are not approved.
- **Visuals:** tone, materials, UVs, visibility/culling and doors still need review. Free Roam rivals already receive neon through the original upgrade-level probability table; some cars correctly have no neon. PRNG, pack choice and rendering retain adaptations.
- **Movies/audio:** 28 movies are structurally present per source. Full playback, every audio family and target-device listening are not approved by this check. Skip remains available.
- **PS2 USA image:** SLUS_210.65 source support and retained 179-file checks do not verify the original disc image; that image remains unknown. PAL/other revisions are refused. See [source differences](tools/PS2_SOURCE.md).
- **PSPLink:** historical bf7f stopping required human recovery. No general PSPLink shutdown/older Bus error fix is claimed.

For generator failures, attach `relatorio.json` and the failing step log. The [illustrated reporting guide](docs/bug-reporting-guide-EN.pdf) is unchanged. [Português](ptbr/LIMITACOES.md).

A final single run on the same executable completed all three laps of automatic Stadium Drift 2 (4312) through normal controls in 123.4318 simulation seconds, banking 1,880 points across seven drifts. The player finished fourth: the genuine loss is preserved. No player/AI reset, NaN or Core error was recorded. Pause/resume, released event assets and return to Main with advancing music passed. Native files stayed unchanged. This qualifies that complete event and its score, not a Drift win, all-car handling parity or physical audio.
