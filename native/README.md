# Native browser generator

The Rust executable verifies the download, starts the included private CPython runtime,
opens the local browser interface, and owns the helper process tree. Conversion uses the
frozen RC21 recipe with precompiled native audio/movie tools. Nothing is installed on
the tester's system. No original game data is included or uploaded.

## Usage

Extract the matching ZIP into a writable local folder. Open `Generate-game.bat` on Windows,
`Generate-game.command` on Mac, or `Generate-game.sh` on Linux (it opens a terminal window when
started from a file manager). Keep the terminal open. Select your ISO and a new empty
output folder in the browser. Use **Generate PSP game** and wait for reference verification.
The helper does not copy to the PSP. Copy instructions and bug reporting are in the root README.

CLI: `nfsu2-generator[.exe] serve`, `serve --no-browser`, `serve --port 4173`, or `doctor`.
Use `./nfsu2-generator` on Mac and Linux. Ctrl+C stops the helper and conversion. Closing only the tab
does not stop a job. **Close helper** is available once a conversion has ended/cancelled.

Windows x64 requires Windows 10 2004+ or 11. Mac packages target macOS 15+ and are separate
for Apple Silicon and Intel. Linux x86-64 needs glibc 2.35+ (built on Ubuntu 22.04); only glibc
and the loader come from the system, everything else is inside, and the browser opens through
`xdg-open`. Windows ARM, ARM Linux, commercial code signing and notarization are outside this
implementation. Normal OS trust prompts can still appear.

## Maintainer build

Packages are built and tested locally on each platform (Windows x64, Linux x86-64, Apple Silicon
and Intel Macs). The content policy also runs on hosted CI for pushes and pull requests. Build tools are needed **only on the maintainer machine**: Rust,
Python, uv, C/C++ compiler, CMake, Ninja, Make, patch, tar and curl. Windows uses MinGW-w64 and the Windows SDK manifest tool.
Linux builds in an Ubuntu 22.04 environment (clang for LAME, GCC for the rest, binutils for the
audit); the builder pins `manylinux_2_28` wheels and fails if any ELF file needs a library outside
the package other than glibc, the loader, `libgcc_s` and the `libstdc++`/`libz` that the
manylinux wheels leave to the system (`runtime/linux-library-audit.json`).

```sh
python -m pip install -r native/release-requirements.txt
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
are release assets. Never put game ISOs, game data, access keys or private sign-up data in a release.

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

## Required publication check

Install the maintainer-only dependency before building archives:

```sh
python -m pip install -r native/release-requirements.txt
python -m unittest discover -s native/tests
```

Every build is checked after sealing. `test_bundle.py --archive` checks both
its input folder and the completed ZIP before replacing the previous archive
or writing a checksum. The final upload must use:

```sh
python native/publish.py vX.Y.Z native/dist/*.zip native/dist/*.sha256 --check-only
python native/publish.py vX.Y.Z native/dist/*.zip native/dist/*.sha256
```

Add every supplementary JSON or checksum attachment to the same command.
The uploader validates every attachment and requires matching ZIP checksums
before it reads credentials or makes a request. It selects the maintainer
account explicitly and never creates a tag or a release. Publishing still
requires the owner's approval. Use `--clobber` only for an authorized replacement.
Hosted workflows must invoke the same content check immediately before upload.

For an independent check, run
`python native/release_guard.py PACKAGE.zip --report report.json`. The check inspects project text, decoded source/JSON
strings, image metadata, PDF text/metadata/attachments, binary strings, and
nested ZIP/TAR/compressed streams. Missing PDF support, unreadable or encrypted
content, unsafe archive paths, and inspection limits block publication.
Checks use explicit vocabulary and context signatures; they are not a proof
that every possible sentence is English. Review new prose and screenshots as
well: image pixels are preserved, not transcribed by this check.

Project documentation, comments, messages and interface text must be English.
Upstream notices and fixtures that need
their original text require a narrow reviewed exception; new runtime or vendor
text has no directory exemption. Restricted development attributions are
checked in legal notices too. Reviewed names and
historical quotations use exact file hashes, paths and token signatures in
`release_policy.json`. Any edit invalidates the exception. Do not add directory
exemptions or disable the check to resolve a finding. Review the reported file,
fix unintended text, and update a narrow exception only when the original
credit, fixture or quotation must be preserved.

The policy stores fingerprints so it does not reproduce the restricted words
in distributed files. `python native/release_guard.py --fingerprint TOKEN`
prints a signature for policy maintenance. Keep policy changes and regression
fixtures together. Existing historical tags and their automatic source archives
are outside this publication check.

The source CI job checks maintained text and metadata. The mandatory package
check also reads executable strings. The current game executable contains
legacy localized messages and is intentionally blocked for future packages
until an English executable is rebuilt and validated. No exception is made
for it; historical releases are not republished by this change.
