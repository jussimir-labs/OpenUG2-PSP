# Historical development notes

Current release: **Closed Beta 1 — RC21**. See [current release notes](RELEASE_NOTES.md)
and [known issues](KNOWN_ISSUES.md). Everything below records an earlier executable;
its checks do not qualify RC21.

---

# RC18 draft: sponsor choice and audio ownership

Executable **da37c112**, source `68566e7bc`. This local draft has completed its current scoped source and headless gates; final packaging remains separate. See the [qualification receipt](docs/qualification/RC18-QUALIFICATION.json).

A failed sponsor-screen load no longer silently chooses or saves the first contract. Retry and explicit selection are retained. Twelve actual-function host cases passed, including load refusal, recovery, choice and retry-clock behavior; independent peer review found no remaining blocker in that narrow delta. This does not approve normal stage2 progression.

Audio bank changes now require a real mixer acknowledgement before old data can be replaced or freed. Refusal preserves the previous bank and consumer state. Three deliberately delayed original readers reproduced invalid freed-memory access before the fix; five positive host cases passed with sanitizers afterward. A separate XA arithmetic correction keeps all16 independently decoded AEMS loops identical,361434 PCM samples. Peer review found no blocking ownership-delta finding. These are confirmed code defects, not proven causes of the human Drift cuts, pause or braking symptom. Reporter listening remains necessary.

On this exact executable, a normal three-lap **Street X4602** win continued the actual first-win native save, followed by native SAVE (`56bf88c129f14940a98bacab6c572229`). Totals became **$500, REP400 and two races**, with kept best events5+6 and **stage1**. A unique fresh process restored all kept state at title and Crib (`95040f74b94649208f28ed78340b6d8b`), without a new SAVE or changes to source/frozen/private native files. A separate private PS2 native SAVE/cold check passed; it is not a new natural PS2 race.

A separate **five-win synthetic native fixture** selected EUROLITE by normal menu controls, saved and restored all kept sponsor state in a unique cold process (`58c97caabdee4a35963214e6fa49aa28`, `abf883f6178c4328bf01c54fb3449065`). It did not earn those five wins or prove normal stage2 progression. The Focus-to-Rachel gate passed in one process (`d3e718b7fe4d4c8da277e7e9a467c4fa`): two short4011 excerpts checked the selected GIN/AEMS/gear families, braking voice state, pause/resume and return to an advancing music/menu state. Source and private native saves were unchanged. These are state/cursor/gain checks, not PCM capture, subjective listening, a full race or every car/audio branch. Historical RC17 8ef4 normal4101/$250/REP200/SAVE/cold and directed NOS/Drift results retain their own binary and scope; no old runtime PASS is relabelled as da37.

Only **EBOOT.PBP** changes versus exact matching RC17 generated data. Xbox remains162 files/753 packed entries/ID9576; PS2 remains159 files/756 entries/ID8729. Packs, SFX and native banks are unchanged. See [identity](docs/qualification/RC18-GENERATOR-IDENTITY.json). Helpers and output default are byte-identical to kit17, both reporting PDFs are preserved, and the kit contains no original game assets or saves. No automatic install, card operation or ZIP finalization was performed.

The 4u flags,63 PGO profiles and hot layout remain unchanged. Human audio/visual confirmation, PSP/card and other models, global30fps, full-career and complete original parity remain unapproved. Master remains the port's final-output policy; other camera mix profiles are unqualified. Drift/Drag retain their original ambience policy. Original US PS2 ISO identification is still unverified and4401 recovery remains deferred. [Known issues](KNOWN_ISSUES.md).
