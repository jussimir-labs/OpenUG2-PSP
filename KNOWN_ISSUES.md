# Closed beta: validation scope and known issues

The overnight candidate keeps the 4u PGO/LTO configuration and includes directed
fixes for EA TRAX, audio lifetime and long loops, music recovery, braking audio
inputs, pause/resume state and the drift steering acceleration input. See
[NIGHTLY_NOTES.md](NIGHTLY_NOTES.md) for the exact executable and evidence.
Matching a generated-file manifest checks identity; it is not full game approval.

## Reports under verification

The GitHub examples stay open for reporter validation on the new executable:

- [EA TRAX badge and outline (#1)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1): corrected draw order and circular cap; before/after and integrated menu pixels reviewed locally.
- [Cuts/clicks during drift (#2)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2): separate turbo/tyre bank ownership and complete long-loop cursors fix confirmed defects. Their contribution to the reported engine symptom has not been isolated on hardware.
- [Loop while paused (#3)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3): directed local tests observed zero engine gain during pause. Environment loops are stopped on inactive updates. A separate limiter-state defect after resume is corrected. These findings do not identify every sound heard in the original hardware report.
- [Braking sound (#4)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4): the original-derived controller now receives linear acceleration, tyre-frame movement and longitudinal grip demand, with brake-release smoothing retained. Grip demand remains a port estimate; full per-wheel physics parity is not established.
- [Music after drift (#5)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5): street ambience ownership is released before requesting menu music; local return-to-menu recovery was demonstrated.

## Remaining limitations

- **Drift handling:** the steering unit error is corrected, but the vehicle force
  model remains adapted. In the matched 1.2 s maneuver, original yaw was 79.431°,
  the old port 68.147°, and the protected overnight candidate 70.025°.
  Steering-controller agreement alone does not prove whole-car parity.
- **Tunnel Construction/drag 4401:** opponents can get stuck and repeatedly
  recover. This was deliberately deferred; no fix is claimed.
- **Hardware and performance:** the new executable has no physical runtime or
  Memory Stick approval. The earlier human PSP-3000 title/brief Freemont/menu
  check and card installation apply only to executable bf7f. PSP-1000, PSP-2000,
  Go and global 30 fps remain unapproved.
- **PSPLink:** the earlier bf7f stop required human recovery. No general cause
  for the older RELEASE03 Bus error or PSPLink shutdown has been proved fixed.
- **Visuals:** tone, materials and visibility still require review.
- **PS2 source:** 179 USA input files were checked through a reconstructed image;
  the original USA disc image remains unverified. Other revisions are refused.
  See [source differences](tools/PS2_SOURCE.md).

Directed SAVE and first new-process load proofs use private copies of existing
native career fixtures from both sources. Historical airport/350Z, initial 206,
first garage, full races and natural-win reward proofs belong to their recorded
executables. This candidate does not inherit a complete-career approval.

Every report should identify the executable, device, source, car, track and exact
steps. For generator failures, attach relatorio.json and the failing step log.
