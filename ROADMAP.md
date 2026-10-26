# Closed beta roadmap

The reported issues remain open while the reporter verifies the new executable.
A checked source change is not a claim that every device or symptom is approved.
See [known issues](KNOWN_ISSUES.md) and [nightly notes](NIGHTLY_NOTES.md).

- [x] Correct EA TRAX badge draw order and circular-cap proportions; local visual comparison passed. [Report #1](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1).
- [x] Separate turbo/tyre sample storage, confirm mixer acknowledgement before replacement, and support the complete long drift loops. Native memory/cursor tests passed. [Report #2](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2).
- [x] Release street ambience on return to Main Menu; local menu music recovery passed. [Report #5](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5).
- [x] Stop environment loops on pause and cancel pending limiter ramps so they cannot restore stale targets after resume. Native regression passed. [Report #3](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3).
- [x] Correct the skid controller's acceleration units, tyre-frame motion and longitudinal grip demand; retain original brake-release smoothing. [Report #4](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4).
- [x] Feed angular acceleration to the original-derived drift steering controller, instead of angular velocity.
- [ ] Confirm the exact hardware audio reports with the new executable; retain family-specific evidence for GIN, AEMS, shifts, tyres and environment.
- [ ] Complete the original vehicle-force model comparison for drift. The native mathematical investigation is separate from the protected candidate; no full handling parity is claimed.
- [ ] Validate the new executable on hardware within a separately authorized scope; do not replace or modify physical saves automatically.
- [ ] Fix Tunnel Construction 4401 opponent recovery loops; this work remains deliberately deferred.
- [ ] Continue PSP-1000/other-device, global 30 fps, complete-career, visibility/material and original PS2 USA image validation.

Free Roam rivals already receive and render neon using the original upgrade-level
probability table. At level 0, 90% can have no neon. PRNG, car-pack selection and PSP
rendering contain port adaptations; this is not a new visual parity approval.

Português: [ROADMAP.md](ptbr/ROADMAP.md).
