# Overnight candidate: 26 October 2026

Local candidate, not published or physically approved.

- Executable: `c25f117241edea5fd66bb090303b23b8579cbda245df9cd7fc95234957434274`.
- Source: `8f7ec4df13f1f54cf086cfbb8ed9316498eea43d`.
- Configuration: 4u PGO=use, LTO=1, RELEASE=0, PSP_LARGE_MEMORY=1;
  the same 63 training profiles and hot.ld are preserved.
- Data: no reconversion; the 161-file Xbox and 158-file PS2 references change
  only EBOOT. Old packages and executables remain preserved separately.

## Included corrections

EA TRAX draws its opaque EA Games badge above the dark panel and keeps the
left cap circular. Turbo and tyre sample banks have separate ownership and
confirmed mixer acknowledgement before replacement; long drift loops keep
their complete sample lengths. Returning from the street releases ambience
before requesting menu music. Inactive updates stop environment loops.

Tyre audio uses body linear acceleration, tyre-frame contact velocity and
longitudinal grip demand. Original-derived braking gates and release smoothing
are retained. Grip demand remains a port estimate, not a full wheel solver.
Drift steering now receives angular acceleration rather than angular velocity.
Pending engine-limiter ramps are canceled on pause so they cannot restore stale
limiter targets after resuming at low RPM.

## Exact-current-executable evidence

One directed PPSSPP run used normal menu/buttons, stock 206 and Freemont 4011.
Straight braking reduced speed from 14.9835 to 0.38615 m/s in 1.533 s with zero
steering. A valid skid_lock sample had positive gain and an advancing cursor.
Pause froze the race clock and silenced GIN/AEMS and tyre requests; resume
restored engine output. Return to Main Menu selected track 34 and music blocks
advanced from 167 to 187. Press START and the corrected EA badge were reviewed
in the title/menu captures.

The harness initially treated mus_want=-1 as a failure. That field is a consumed
pending request; mus_shown=34 identifies the playing track. Source review and
advancing blocks reconciled this assertion offline. The raw failure is retained;
there was no rerun. The automatic transmission did not enter the limiter, so
its specific pause/resume regression remains a native host proof only.

## Immediately preceding executable: separate evidence

Executable 73972acf, before the two-flag limiter correction, passed normal drift
4312, braking, pause/resume, return to Main Menu and recovery from active street
ambience. It also passed native SAVE and first new-process load with private
existing earned fixtures from both data sources: Xbox money250/rep200/races1,
PS2 money250/rep300/races1. Original seeds remained unchanged. A separate host
fixture-injection assertion was reconciled without repeating SAVE or driving.

Three calibrated mixer diagnostics on 73972acf acknowledged 300 buffers each,
including complete 113376- and 69120-sample loops. Conservative full-rate totals
were 2.430/2.496/2.427 ms, with no over-buffer or over-10-ms count. late=2 in each
case remains recorded. These are not whole-game 30 fps or zero-audible-gap proofs.

Binary comparison confines the current change to engsnd_player, its jump table
and embedded build identifier. Save, car, mixer and movie code bytes are unchanged.
This scopes reuse of prior evidence; it does not invent exact-current-executable
SAVE, calibrated-performance or hardware passes. Native audio regressions,
movie/audio host checks and both sources' 28-movie structural checks passed.

## Still open

Full drift vehicle physics remains adapted. For the matched 1.2 s maneuver,
original yaw was 79.431 degrees, the older port 68.147, and protected 73972acf
70.025. The isolated original-state steering oracle improved from 2.6998 to
0.4774 degrees RMS; this does not prove whole-car parity. The original wheel,
clutch and body model is being investigated natively and is not integrated.

No new executable was installed on or run on the PSP. Earlier human PSP-3000
and Memory Stick evidence applies only to bf7f. Complete races, natural rewards,
a complete new career, PSP-1000/other devices, global 30 fps, full visual parity
and the original PS2 USA image remain unapproved. The 4401 opponent issue stays
deferred. Hardware audio reports remain open for reporter validation.

Português: [notas da candidata](ptbr/NOTAS-NOTURNAS.md).
