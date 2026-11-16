# Third parties and rights

This repository contains no game data. Everything the game displays and
plays (city, cars, textures, sounds, music, movies, screens) is converted
by the tool from the player's own ISO, on their own computer.
*Need for Speed* and *Need for Speed Underground 2* are Electronic Arts
trademarks. This is a fan project with no affiliation with EA or Sony.

## What is in this repository

| Item | Origin | License | Redistribution |
|---|---|---|---|
| Original PSP port code and its build support | OpenUG2-PSP | MIT | yes, with the notice ([`psp-viewer/LICENSE`](psp-viewer/LICENSE)) |
| `eboot/EBOOT.PBP` | the compiled port | project code, plus the libraries listed below | yes, with the notices in `licenses/` |
| in the PSP source and EBOOT: physics ported from [OpenUG2](https://github.com/whoismept/OpenUG2) (`physics_ug2.c`) | OpenUG2 contributors | MIT | yes, with the notice (`licenses/OpenUG2.txt`) |
| in the PSP source and EBOOT: ORCA ported from [RVO2](https://github.com/snape/RVO2) | Univ. of North Carolina | Apache 2.0 | yes, with the license (`licenses/RVO2-Apache-2.0.txt`) |
| inside the EBOOT: [PSPSDK](https://github.com/pspdev/pspsdk) (libpspgu, libpspaudio, libpspmp3, libpspjpeg…) | pspdev | BSD | yes, with the notice (`licenses/PSPSDK.txt`) |
| inside the EBOOT: newlib (libc, libm) | various | BSD and similar | yes, with the notices (`licenses/newlib.txt`) |
| inside the EBOOT: zlib | Jean-loup Gailly, Mark Adler | zlib | yes (`licenses/zlib.txt`) |
| `tools/`, `build_game.py`, `sources/`, `presets/` | the project | project license | at the author's discretion |
| `third_party/xdvdfs/xdvdfs.py` (Xbox disc reader) | xboxrecomp, by sp00nz, in [nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw) | MIT | yes, with the accompanying license |
| `tools/vendor/utk/` | Andrew D’Addesio, vgmstream adaptation pinned in `PROVENANCE.json` | public domain/Unlicense and the permissive vgmstream notice preserved alongside it | yes, with both notices; offline conversion only |
| `art/reference/psp-controls/psp-isolated-v1.png` | project image of a PSP-3000 for the controls screen | project license | yes |
| `manifests/*.json`, `sources/xbox-files.json` | SHA-256 hashes of disc and package files | hashes only | yes |
| `tools/race_reference.json` | race segments measured while playing the original | project measurement data | yes |

The game file formats were read with the help of public notes from
OpenUG2, [nfsu2-re](https://github.com/yugecin/nfsu2-re), and
[FEngLib](https://github.com/NFSTools/FEngLib); none of their code is
included in the converters.

## Tools included in native generator downloads

| Item | Purpose | License |
|---|---|---|
| Python 3.12, numpy, scipy, Pillow, matplotlib | the converters | PSF, BSD, HPND |
| [trajectory_planning_helpers](https://github.com/TUMFTM/trajectory_planning_helpers) (TUM) | rival racing lines; `tools/min_curv.py` is a modified copy of its `opt_min_curv` (same results, fewer dense matrix products) | LGPL-3.0 |
| quadprog | dependency of the above | GPL-2.0 or later |
| ffmpeg, lame | audio and movies | LGPL/GPL |
| [jpegli](https://github.com/google/jpegli) (`cjpegli`, precompiled in the native package) | movie frames | BSD-3 |

These tools run on the player's computer from the native download's private `runtime/`
folder. Only converted game files go onto the memory card. Runtime binaries are release
assets, not Git objects. Licenses bundled with CPython and Python wheel distributions
are retained; FFmpeg, LAME and jpegli sources, build instructions and notices accompany
the native-tools distribution in `runtime/sources/` and `runtime/licenses/`.
The original RC21 source-only ZIP downloads/builds these dependencies during setup.

The Rust supervisor uses serde/serde_json (MIT/Apache-2.0), sha2 (MIT/Apache-2.0),
ctrlc (MIT/Apache-2.0), libc (MIT/Apache-2.0) and windows-sys (MIT/Apache-2.0).
Exact crate versions are locked in `native/Cargo.lock`; source and license notices
are retained in the source distribution. The supervisor does not link game data.
