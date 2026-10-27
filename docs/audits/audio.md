> Historical audit of executable bf7f. For the overnight corrections and their exact qualification, read [NIGHTLY_NOTES.md](../../NIGHTLY_NOTES.md).

# Audio audit: closed beta build 06

Audit date: 25 October 2026. Related reports: [#2 drift audio](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/2), [#3 pause audio](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/3), [#4 braking audio](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/4), and [#5 music after drift](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/5).

This audit covers the installed build with EBOOT SHA-256
`c34edd611f60021496f88106e87b616bae5df6c6712ba32dea85762cf923df48`.
It used source inspection, asset headers and PCM inspection, and an
existing local PPSSPP observation of normal controls through drift,
pause, resume, and return to the main menu. No patch, new build,
conversion, or change to the installed game was made for this audit.

## Confirmed defects and their limits

### Shared ownership of turbo and tyre sample banks

`audio_sfx_swap` retains only one allocation for all replacement sample
banks. Its callers replace two disjoint groups:

- `turbo_bank`: turbo spool, blow-off, and hiss samples.
- `tyres_bank`: skid, wheelspin, and wheel-lock samples.

Loading a drift tyre bank frees the previous turbo bank while turbo
sample entries still point into that allocation. Only voices matching
the newly loaded tyre names are stopped. A later turbo bank change can
likewise free storage still referenced by tyre samples.

This is a confirmed memory lifetime defect in the installed source.
It can produce invalid sample reads when an affected source is audible.
It has not yet been established as the cause of the user's reported
symptom. The GIN engine sweep and AEMS engine layers use separate
storage; this finding does not show that their buffers were freed.

The proposed fix is independent ownership for each bank, with validation
before publication and confirmed mixer quiescence before releasing the
replaced allocation. Removing the free without replacing the ownership
model would introduce a leak.

### Drift loops exceed the voice cursor's range

The sample voice uses a 32-bit cursor with 16 fractional bits. Its
computed loop end wraps for sample counts of 65,536 or more. Two
installed drift samples exceed that range:

| Bank / sample | Actual samples | Effective loop length after overflow | Duration at pitch 1 |
|---|---:|---:|---|
| `SKIDS_DRIFT_MB` / `skid` | 113,376 | 47,840 | 4.726 s becomes 1.994 s |
| `SKIDS_DRIFT2_MB` / `skid_lock` | 69,120 | 3,584 | 2.880 s becomes 0.149 s |

The mixer therefore repeats at an internal boundary instead of the
prepared loop ending. For the second sample, the nominal jump at that
boundary is 23,919 PCM units, compared with 2,693 at the intended ending.
This establishes an incorrect loop boundary and a risk of clicks or
buzzing. It is not a perceptual test or proof that this was the sound
reported as an engine cut.

The proposed fix is a cursor representation that supports the full
sample length across every mixing path, with explicit size validation.
The CPU cost of wider arithmetic on the PSP must be checked. Simply
truncating the asset would not preserve its prepared loop boundary.

### Ambience retains the music stream after returning to the menu

The reused local run showed ambience still selected after leaving drift
for the main menu. A menu music request remained pending because
`mus_thread` only accepts song requests while no ambience owns the stream.
Consumed music blocks increased from 180 to 196 over the next three
seconds, demonstrating continued stream progress during that interval.

`return_to_front` silences street sources and requests the menu playlist
but does not release ambience ownership. This is a confirmed transition
failure; the observation does not demonstrate a frozen MP3 decoder.
The proposed fix is to release both the ambience request and its
environment state when leaving the street, before requesting menu music.
Pause and resume should retain their separate hold behavior.

## Braking audio

The original wheel sound controller reads per-wheel movement from the
physics state. Its skid controller conditions wheel-lock sound on
negative longitudinal slip, lateral movement, and a brake input above
0.15.

The port's `skids_wheel_motion` supplies negative longitudinal slip only
for rear wheels held by the handbrake. Ordinary braking is not
represented by that input. This is a confirmed limitation of the signal
provided to tyre audio, but it does not yet establish why braking seemed
silent in the reported case.

In particular, straight braking can have little lateral movement, so
the original formula does not justify playing a skid sound whenever the
brake button is pressed. Per-wheel physics and sound inputs need a
direct comparison before changing braking behavior.

## Pause report was not reproduced

In the reused drift run, the observed engine gain and RPM became zero
both upon entering pause and after two seconds. They returned to positive
values after resume. `sound_update` calls
`engsnd_player(active = 0)` before its inactive early return, and that
function already disables the GIN and AEMS engine sources.

The early return is therefore not evidence of a skipped engine shutdown.
The physical pause report remains open: this inspection did not capture
every audible source or reproduce every possible pause condition.

## Original behavior and next checks

The comparison consulted the original `CARSFX_PlayerEngine`,
`SFXCTL_HybridMotor`, `SFXCTL_Wheel`, `SFXCTL_Skid`, and
`EAXSound::EnterPauseMenu` / `ExitPauseMenu` behavior. The port already
implements the general GIN/AEMS hybrid structure, load and RPM
smoothing, and shift and limiter states. No decompiled source is
included here.

Before attributing a reported click to the engine, isolate tyre, turbo,
and engine output; check live sample ownership and complete loop
durations; and correlate captured discontinuities with the existing
mixer lateness counters. A targeted drift → pause → resume → main menu
check should verify ambience release and actual menu music playback.

These findings do not prove a physical PSP performance problem.
Uncalibrated PPSSPP observations establish behavior and continuity,
not PSP CPU budget. No new emulator run or physical hardware test was
performed during this audit.
