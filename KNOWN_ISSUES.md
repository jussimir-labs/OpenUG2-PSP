# First closed beta: known issues

Executable `bf7f21e3` restores the 4u configuration, PGO and LTO. The only new
code change restores Press START visibility. File identity is not full approval.

- **Tunnel Construction (drag 4401):** opponents can get stuck and repeatedly
  respawn. Jussimir deferred the fix. Observed on the previous executable;
  not retested here.
- **Performance/devices:** global 30 fps and PSP1000, PSP-2000 and Go support
  remain unapproved. The human PSP-3000 host0 check confirmed the title prompt,
  brief Freemont driving and return to menu. No full race, card installation or
  physical native-save test was performed on this executable.
- **PSPLink:** the automatic stop required manually reopening PSPLink. Game
  exit and session cleanup were then confirmed. The human menu report is
  retained; the later read did not independently confirm that screen.
  The previous executable's Bus error root cause is not proved globally fixed.
- **Visuals:** tone, materials and visibility differences remain under review.
- **PS2:** 179 American inputs were verified using a reconstructed image.
  The original USA image remains unverified; other revisions are refused.
  See [tools/PS2_SOURCE.md](tools/PS2_SOURCE.md).

This executable passed PPSSPP title and 4011-prefix checks, native save/cold
with both data sources using existing fixtures, and a separate new-profile,
career/garage save-dialog and cold-load test. The harness required termination
of its held emulator after the first phase; the technical evidence records it.
Earlier airport/350Z, first 206, first garage and real-win reward proofs belong
to the previous RELEASE03 executable. No complete career is approved.

Reports should identify the exact executable, device, source, track and steps.
For generator errors include relatorio.json and the failing step's log.

## Reports from the current Memory Stick test

These remain open, with examples and evidence in GitHub:

- [EA TRAX logo opacity and outline (#1)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/1).
- [Engine cuts/clicks in drift (#2)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2). The audit found sample-bank lifetime and loop-cursor defects; their exact contribution to the heard symptom still needs isolation.
- [Engine loop while paused (#3)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3). Not reproduced in the directed local pause state check.
- [Apparently missing braking sound (#4)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4). Ordinary braking is absent from one tyre-audio input; the reported case still needs comparison.
- [Music not returning after drift (#5)](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5). Local evidence showed ambience retaining the music stream after returning to the menu.

No gameplay fix or replacement executable is included in this documentation
and browser-generator update. See the [audio audit](docs/audits/audio.md).
