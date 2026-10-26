# Closed beta roadmap

Open work items. This list does not represent completed fixes.

- [ ] **Engine sound loops while paused ([#3](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3)).** The hardware report remains open. Engine gain reached zero when paused in the local run; this neither reproduces nor invalidates the reported symptom on the device.
- [ ] **Braking sound appears to be missing ([#4](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4)).** Compare the regular brake and handbrake, wheel state and original game. The port's slip input does not represent regular braking; the cause of the user's case is still unproved.

- [ ] **Drift: engine crackles/cuts ([#2](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2)) and music fails to return ([#5](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5)).**
  Reported on October 25, 2026 while testing the beta from the Memory Stick:
  the engine sound cut/crackled and background music also became stuck after
  a drift. Both issues remain open. Ambience persisting on return to the menu
  was reproduced locally; the engine crackles still have no demonstrated
  cause. Investigate the mixer/output and the transition between EA TRAX and
  ambience, distinguishing intentional muting during drift from music that
  remains stuck afterward.
  Acceptance: reproduce with an identified car, track and executable,
  identify the subsystem, and validate the engine and music recovery across
  repeated transitions.

- [ ] **Drift: audit handling equivalence with the original.**
  The port uses PlayerSteering and mode tuning derived from the original,
  but also includes drift_grip, an adjustment of up to 10% after sustained
  steering, calibrated against PS2 measurements, and an adapted physics model.
  Do not claim full parity. Compare PlayerSteering,
  RidePhysicsInfo/CompleatCarPhysicsInfo::TuneForDrift and the original data;
  document reproduced rules, approximations and fields not implemented.
  See the [source comparison audit](docs/audits/original-rules.md).

- [ ] **EA TRAX card: EA Games logo opacity and circular outline ([#1](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1)).**
  Jussimir reported on October 25, 2026 that the logo looks dark/transparent
  and the left edge is too elliptical. Compare against the supplied visual
  reference: preserve the circular logo and its correct opacity, with the
  card's outline following the circle. This visual issue remains open,
  without a fix in this executable or an attributed technical cause.
  Close only after comparing the same elements at equivalent scale and
  proportions and validating the logo, alpha and edge without regressing the
  text or the rest of the card.
  Evidence: [port image](docs/bug-evidence/ea-trax-port.png) and
  [reference](docs/bug-evidence/ea-trax-referencia.png) in this beta repository.

- [ ] **Tunnel Construction (4401):** fix stuck opponents and repeated recovery. This known limitation was deferred to the roadmap.
- [ ] Continue validating performance, devices, visibility and the original PS2 source, within the scope described in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

Português: [ROADMAP.md](ptbr/ROADMAP.md).
