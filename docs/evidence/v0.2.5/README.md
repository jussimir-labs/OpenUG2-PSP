# v0.2.5 issue evidence

Release: [v0.2.5](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases/tag/v0.2.5).
Published executable build: `5dd3cecb06`; EBOOT SHA-256:
`ed457f83f03237614f5f467233555846d541bbc5dba923b01afe581304678f89`.

## Scope of the images

These are the original implementation comparisons retained while developing the
fixes included in v0.2.5. They were not recaptured on the published executable.
Development version/build labels in the frames therefore differ from the release.
The contact sheets were made with `tools/contact_sheet.py`, with captions outside
the game frames. Original captures and their records remain preserved privately.

- **64:** Sprint 4101 start (PS2 original / PSP before / PSP after), and finish
  before/after at frames 8436 and 8441. Both runs crossed at frame 8439.
  The runtime comparison is the forward course; reverse-direction capture is not included.
- **66:** Xbox-data Paint menu before/after, the roof paint grid and the original
  refusal for Engine Accents without the required upgrades. Engine accent colour
  is saved and rated but is not yet rendered; the issue remains open.
- **75:** The same directed highway impact in the emulator, old versus corrected
  collision. This is an Xbox-data diagnosis of the reported location, not a PS2
  hardware retest. Tree disappearance was not reproduced; the issue remains open.
- **76:** Spanish pause and World Map hints before/after, using Xbox data.
- **78:** Spanish SMS reader and mailbox before/after, using Xbox data.
- **80:** Spanish race offer before/after, using Xbox data. The prologue offer is
  shown; sponsor, URL and SUV offer selection also had converter checks, but are
  not shown in this image.

## Behaviour checks without a before/after gameplay video

The JSON files retain measured diagnostic results, not substitute gameplay images.

### Issues 72 and 74: outrun rivals

**Partial correction; both issues remain open.** The maintainer retested v0.2.5
on PSP and still found differences from the original game. The checks below
cover specific reproduced cases and do not establish full handling or AI parity.
A new attributed in-game comparison is still required before closing these reports.

Same host scenario with eight traffic seeds, old versus corrected behaviour:

| Observation | Before | After |
| --- | ---: | ---: |
| Frames overlapping traffic by at least 0.3 m | 269 | 1 |
| Longest consecutive traffic overlap | 84 frames | 1 frame |
| Frames inside a wall near the player | 428 | 0 |
| Frames overlapping the player or another rival | 831 | 0 |
| Abrupt turns greater than one radian in one frame | 10 | 0 |

The one remaining traffic overlap was isolated and 194 m from the player.
The retained PPSSPP outrun check recorded 10,362 rival frames with zero traffic
overlap, zero wall penetration and zero abrupt turns, with nine avoidance passes.
These are implementation-stage host/emulator checks, not physical PSP measurements,
not a replay of the tester's old executable, and not an exhaustive guarantee.
No matched before/after gameplay video was retained for these issues.

### Issue 68: saved shop speech history

The emulator test visited the visual and performance shops, saved, closed the
process and resumed the same private career in a fresh process. None of the shop
screens that spoke in the first pass repeated in the resumed pass. The two screens
that spoke on resumption had not spoken on the first pass. A separate older-save
check confirmed migration: one final introduction, then no repeat after saving.
This is an audio/state check; a still image would not demonstrate repetition.

### Issue 86: generator verification

The retained Linux PS2 incremental update passed: all 162 output files were either
identical (100) or equivalent (62), with reference verification true. This is a
cross-processor generator check, not a Windows run. The Windows tester independently
[confirmed the v0.2.5 fix](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/86#issuecomment-6555020624).
