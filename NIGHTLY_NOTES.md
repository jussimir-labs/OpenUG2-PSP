# Candidate 13: 26 October 2026

This kit is staged locally for closed-beta testing. It has not been published, installed on a PSP, or approved on physical hardware.

- Executable SHA256: `e7ce75f8b2e35949fdd04525baf9d6a114c177d822cfbe2e4189f20f7e68fa63` (`e7ce75f8`).
- Build source: `5a3e4729d57e728f620da7adca1b4f1b97ae6b69`.
- Configuration: 4u PGO=use, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1. The same 63 PGO profiles and `hot.ld` are retained; there was no new training.
- Generated packages: Xbox 162 files, data ID `78eb`; PS2 159 files, data ID `4111`. The original physics library and two road sidecars are now produced from your game files. Existing converted assets were reused for local packaging; no full reconversion was run during this qualification.

## What changed

Human-controlled Drift now uses an original-derived vehicle model with source wheel, suspension, engine, clutch, drivetrain, steering and body rules. Its configuration comes from the supported 29 US car records, performance parts and nitrous data. This describes the supported configuration domain; it does not mean that all 29 cars and every upgrade combination have been driven and approved.

Custom dyno sliders and special preset cars use the previous handling model for the whole event. The game chooses that before starting the event; it does not switch handling models in the middle of a failed physics step. Horizontal contacts remain a 2D port adaptation. Wild/extreme and hydraulics behavior is not fully qualified. This is not full original-game parity.

The EA TRAX badge/circular cap, separate turbo/tyre audio banks, complete long-loop cursors, menu-music recovery, braking-audio inputs and pause/limiter fixes are retained. Reports #1–#5 remain open for the reporter to check on this executable, especially by listening on the target device.

## Checks on this exact executable

Three short PPSSPP runs used normal menus and controls: Stadium Drift 2 (4312) with automatic and manual transmission, and city Drift 4174. They demonstrated acceleration, finite body state, pause/resume and return to the menu with music. The manual run completed a first-to-second shift (native gear indices 2→3). These are bounded excerpts, not complete Drift races or comparisons with every original trajectory.

The raw reports retain a failed cleanup assertion that required a positive private step counter. Repeated cleanup cleared that diagnostic counter; observed ownership was disabled on return to Main. Manual and city runs also recorded cleared asset pointers. The functional observations were reconciled offline, without rerunning or changing the game to hide the failed assertion. An earlier boot attempt used the wrong emulated filesystem prefix for packed data; the qualified runs used the normal `ms0:/PSP/GAME/NFSU2VIEW` layout.

Native SAVE followed by the first load in a new process passed with private existing earned fixtures from both data sources: Xbox $250/REP 200/one race and PS2 $250/REP 300/one race. The original fixtures were preserved.

A separate Xbox career run completed the full 4101 sprint naturally in 139.1676 simulation seconds, finishing first with order [0,1,3,2] and no recorded player or AI resets. Money rose from $0 to $250, REP from 0 to 300, and completed races from 0 to 1. Native SAVE was followed by one fresh process, which restored that exact newly earned state. No forced win, shortened race, or game-state injection was used. This validates that race/reward/save sequence; it does not approve a complete new career or all race modes.

All 28 movies are structurally present for each source. Skipping movies remains available. This is not a complete playback or physical audio check. One calibrated 1200-frame Drift diagnostic recorded 99.42% of frames within two vblanks, with seven slower frames/hitches; it is a single scene, not global 30 fps or hardware approval.

## Still to verify

No e7ce hardware or Memory Stick run has been performed. Historical PSP-3000/card observations belong to their earlier executable. PSP-1000, PSP-2000, Go, physical saves, full visual parity, all-car/all-track handling, a complete career and global performance remain unapproved. The original PS2 USA disc image remains unverified; the retained 179-input comparison used reconstructed-source evidence. Tunnel Construction/drag 4401 rival recovery remains deferred. See [known issues](KNOWN_ISSUES.md).

The portable [qualification receipt](docs/qualification/OVERNIGHT-QUALIFICATION.json) separates current evidence from historical and untested scope. [Português](ptbr/NOTAS-NOTURNAS.md).

A final single run on the same executable completed all three laps of automatic Stadium Drift 2 (4312) through normal controls in 123.4318 simulation seconds, banking 1,880 points across seven drifts. The player finished fourth: the genuine loss is preserved. No player/AI reset, NaN or Core error was recorded. Pause/resume, released event assets and return to Main with advancing music passed. Native files stayed unchanged. This qualifies that complete event and its score, not a Drift win, all-car handling parity or physical audio.
