# Native browser generator

The Rust executable verifies the download, starts the included private CPython runtime,
opens the local browser interface, and owns the helper process tree. Conversion uses the
frozen RC21 recipe with precompiled native audio/movie tools. Nothing is installed on
the tester's system. No original game data is included or uploaded.

## Usage

Extract the matching ZIP into a writable local folder. Open `Generate-game.bat` on Windows,
or `Generate-game.command` on Mac. Keep the terminal open. Select your ISO and a new empty
output folder in the browser. Use **Generate PSP game** and wait for reference verification.
The helper does not copy to the PSP. Copy instructions and bug reporting are in the root README.

CLI: `nfsu2-generator[.exe] serve`, `serve --no-browser`, `serve --port 4173`, or `doctor`.
Use `./nfsu2-generator` on Mac. Ctrl+C stops the helper and conversion. Closing only the tab
does not stop a job. **Close helper** is available once a conversion has ended/cancelled.

Windows x64 requires Windows 10 2004+ or 11. Mac packages target macOS 15+ and are separate
for Apple Silicon and Intel. Windows ARM, Linux native packaging, commercial code signing,
and notarization are outside this implementation. Normal OS trust prompts can still appear.

## Maintainer build

GitHub Actions builds and tests on Windows x64 and both Mac architectures. Build tools
are needed **only on the maintainer/CI machine**: Rust, Python, uv, C/C++ compiler, CMake,
Ninja, Make, patch, tar and curl. Windows uses MinGW-w64 and the Windows SDK manifest tool.

```sh
bash native/build_tools.sh native/tools native/.cache/tools
cargo --config native/.cargo/config.toml test --locked --manifest-path native/Cargo.toml
cargo --config native/.cargo/config.toml build --locked --release --manifest-path native/Cargo.toml
python native/build_bundle.py --output native/dist/NFSU2-Generator-macos-arm64 \
  --tools native/tools --helper native/target/release/nfsu2-generator
python native/test_bundle.py native/dist/NFSU2-Generator-macos-arm64 --archive
```

Use the platform name and `.exe` helper on Windows. Builds pin CPython archives by SHA-256,
Python library versions, FFmpeg/LAME source archives by SHA-256 and jpegli by Git commit.
The builder preserves third-party licenses and corresponding native-tool sources, audits
library dependencies, and records file hashes in `bundle.json`. Output ZIPs and SHA-256 files
are release assets. Never put game ISOs, game data, access keys or private sign-up data in CI.

## Validation scope

`native-qa.json` records the checks actually run for each package:

- Startup after moving into a folder containing spaces/accents, with restricted PATH and
  invalid global Python environment settings. The included runtime must still pass `doctor`.
- Native FFmpeg resampling, LAME MP3, jpegli image encoding, EA-XA decoding and encoding,
  movie-audio decoding, and rejection of a truncated MicroTalk stream using synthetic fixtures.
- Local API authentication/origin checks, checking included tools, single-instance behavior,
  shutdown, and refusal to start when a converter is missing or modified.
- Separate Python tests cover protected user folders, resume identity, verified-report success,
  diagnostic failures and cancellation. Windows tests verify the descendant process exits.

These checks do not perform a complete ISO conversion. The full generated Xbox/PS2 output
must still match the existing RC21 manifests: a mismatch is a diagnostic failure, never a
successful package. Complete ISO qualification on each OS is recorded separately; synthetic
fixtures and compilation alone must not be described as end-to-end validation.
The PSP executable is unchanged (`42d252df…436a95`), as are RC21's known game/hardware limits.

The LAME frontend has a small portability patch: its encoding-dispatched ID3 input
accepts a generic pointer for either UTF-8 or UTF-16. The audio encoder is unchanged.
The original source archive, patch and build script accompany each native package.
