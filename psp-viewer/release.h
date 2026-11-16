/* make RELEASE=1: the players' build (the alpha's package, tools/ms_package.py
 * make --release). No HostCmd thread, no diagnostic
 * files nor telemetry from a player's run (boot.txt, viewer-stats.json,
 * race-hold.jsonl, .movie-playback.log) and no extra globals kept solely for
 * PSPLink. A smoke-test.txt still runs its test (the PPSSPP's validation of
 * this same binary); a player's card has none, and with none no test code
 * runs. The existing frame-time/build/position display is controlled
 * independently by developer_overlay (main.c), currently on for development.
 * RELEASE=0 (the default): every build as before. */
#ifndef RELEASE
#define RELEASE 0
#endif
