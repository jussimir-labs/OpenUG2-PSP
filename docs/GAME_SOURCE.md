# Build the PSP game

The C source in [`psp-viewer/`](../psp-viewer/) starts from the game shipped in v0.4.0.
The generator and the PSP executable have separate build steps. Follow the
[generator instructions](../README.md) to create a test game folder from your disc.

## Tools and local data

Use GNU Make, Python 3.12 with NumPy, SciPy and Pillow, and
[PSPDEV v20261001](https://github.com/pspdev/pspdev/releases/tag/v20261001)
with its PSP zlib package. The build was verified on Apple Silicon with
PSP GCC 15.2.0. The toolchain must include linker support for
`--section-ordering-file` and the newlib ABI checked by the build.

Run from the root of the repository. Set `PSPDEV` to your toolchain directory.

```sh
export PSPDEV=/path/to/pspdev
export PATH="$PSPDEV/bin:$PATH"
python3 -m venv .venv
. .venv/bin/activate
python -m pip install numpy scipy pillow
```

Use your US PS2 disc (SLUS-21065) for the small set of tables the compiler
needs. If you already have an extracted disc, point the second command at
the folder containing `SLUS_210.65` and `NFSUNDER`. Xbox-generated game
folders can still be used at runtime with this executable.

```sh
python tools/extract_ps2.py "/path/to/your-ps2.iso" --out game/ps2
python tools/prepare_game_source.py game/ps2
make -C psp-viewer -j4 PGO=off LTO=0
```

This makes `psp-viewer/EBOOT.PBP`. The game shows the development version and
the public checkout's commit in its debug overlay. Include the commit and
your diff when reporting a problem you reproduced with local changes. The
source build uses a standard toolchain without a release PGO profile.

The generated headers under `psp-viewer/local-data/`, disc extraction,
profiles and build outputs are local files. Keep them out of commits and PRs.
The optional `ORIGINAL_PATH_CHOICE=1` research build also needs local Parkade
fixtures. Leave that option off for game builds.

## Test locally

1. Generate a v0.4.0 game folder from your own supported disc. Copy that
   generated game folder to a separate PPSSPP Memory Stick directory.
2. Replace its `PSP/GAME/NFSU2VIEW/EBOOT.PBP` with the one you built.
   Use a separate emulator profile for test saves.
3. In PPSSPP, start from the title and enter Quick Race. Then exercise the
   path affected by your change.
4. In the PR, include the disc edition, emulator or PSP model, and the steps
   and results you checked. Follow the [contribution guide](../CONTRIBUTING.md).

Use [the third-party notices](../THIRD_PARTY.md) for upstream source and
toolchain credits.

## License

The original PSP port code in [`psp-viewer/`](../psp-viewer/) and the PSP
build and test support added with it are under the [MIT license](../psp-viewer/LICENSE).
See [the third-party notices](../THIRD_PARTY.md) for the OpenUG2 MIT and
RVO2 Apache 2.0 components. Game discs and data generated from them are
outside the scope of this license.
