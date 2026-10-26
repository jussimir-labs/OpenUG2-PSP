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
