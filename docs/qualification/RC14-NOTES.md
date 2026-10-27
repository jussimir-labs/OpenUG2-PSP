# Candidate 14: rival tyre audio

This local candidate contains executable **8c51055e** (SHA256 `6723e0d1155c5ce3a1320eae3e155445d7ec8229d036af9cc97c50eaa1774517`), source `582fe4e1b0ef23aeac9518f98bf10290c8aeb331`. It has not been installed, published or tested on a physical PSP.

Racing opponents and free-roam random-encounter rivals now emit spinning, sliding and braking/locked-tyre sounds. The original AI configuration enables one averaged emitter per rival. Ordinary traffic is excluded. Banks are separate from the player's bank; pause mutes them, owners have separate histories, and reserved voices preserve the original effect slots. The implementation changes sound only. See [source qualification and port adaptations](docs/RIVAL_TYRE_AUDIO.md).

The 4u configuration remains PGO=use, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1, the same 63 profiles and unchanged hot.ld. Generated Xbox/PS2 data are unchanged from RC13; only EBOOT changes. No conversion was repeated.

Directed current tests passed: eight native adapter contracts; controller/mixer ASan/UBSan checks with real Xbox and PS2 samples; the existing braking oracle; PPSSPPHeadless Circuit4011, Drift4312 and free-roam rival fixtures. The game fixtures recorded sound magnitudes and gains above the mixer threshold without bank errors, player/AI resets or NaNs. They use diagnostic scripts; Drift is shortened to one lap. These observations do not prove subjective audibility or original sound parity.

Previous normal-control Drift, fresh-career/reward/native-save/cold proofs belong to RC13 e7ce and remain [historical](docs/qualification/RC13-NOTES.md), with their [original receipt](docs/qualification/RC13-QUALIFICATION.json). They have not been relabelled as new 8c51 runs. Reports #1–#5 remain open for the reporter's checks. PSP models, physical saves, 30 fps across all races, all cars/tracks and visual parity are still unapproved; 4401 rival recovery remains deferred.

[Portable current receipt](docs/qualification/OVERNIGHT-QUALIFICATION.json). [Português](ptbr/NOTAS-NOTURNAS.md).
