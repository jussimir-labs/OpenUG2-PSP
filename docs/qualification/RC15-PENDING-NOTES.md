# Candidate 15: career UI recovery and Drift starting positions

This local candidate contains executable **adb5c869** (SHA256 `adb5c8690f630b074ddc4b99898d22d42b9692cd6c8517fba4571bee0690d061`), source `5535dd695987ceddca637789d4bc528aa3ff9840`. **Current runtime gates are pending.** It has not been published, installed or tested on a physical PSP.

Two changes are included: career pages now recover from the confirmed UI allocation failure before page dispatch, with explicit handling if recovery fails; Drift starts keep the position delivered by the intro rather than moving rear cars back into the crowd's flank. Other race modes and the existing scene 15 exception keep their previous handoff policy. The crowd's animations and lifetime are unchanged.

The current source passed 73 directed host UI checks and 84 handoff assertions with address/undefined sanitizers. The build completed incrementally: six units rebuilt, 76 objects reused. These host checks do not establish visual or full-game readiness; the independent current-executable UI/career and Drift runs must finish first.

The 4u configuration remains PGO=use, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1, the same 63 profiles, hot.ld and compiler flags. Xbox/PS2 generated data, packs, IDs and 28 movies per source are unchanged; only EBOOT changes in the source reference maps. No conversion was repeated.

Rival tyre audio from RC14 is retained. Its directed audio fixtures belong to 8c51, while earlier normal-control Drift/career/reward/nativeSAVE/cold evidence belongs toRC13 e7ce. These results remain [historical](docs/qualification/RC14-NOTES.md), not new adb5 runs. Drift/Drag race music uses the original ambience policy; this candidate does not promise new race music. Nitro timbre remains unresolved. Audio and EA TRAX reports remain open for reporter confirmation.

No physical PSP/MemoryStick, all-model, global 30 fps, full-career or original visual/handling parity approval is implied.4401 rival recovery remains deferred. [Current receipt](docs/qualification/OVERNIGHT-QUALIFICATION.json). [Português](ptbr/NOTAS-NOTURNAS.md).
