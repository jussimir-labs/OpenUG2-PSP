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

CLI: `nfsu2-generator[.exe] serve`, `serve --no-browser`, `serve --port 4173`, `doctor`,
`update`, `login` or `logout` (see [Updates](#updates)).
Use `./nfsu2-generator` on Mac and Linux. Ctrl+C stops the helper and conversion. Closing only the tab
does not stop a job. **Close helper** is available once a conversion has ended/cancelled.

Windows x64 requires Windows 10 2004+ or 11. Mac packages target macOS 15+ and are separate
for Apple Silicon and Intel. Linux x86-64 needs glibc 2.35+ (built on Ubuntu 22.04); only glibc
and the loader come from the system, everything else is inside, and the browser opens through
`xdg-open`. Windows ARM, ARM Linux, commercial code signing and notarization are outside this
implementation. Normal OS trust prompts can still appear.

## Updates

The executable is also the launcher. At each `serve`, after checking its own package, it asks
GitHub for the newest release of the repository in `native/launcher.json` (pre-releases included,
tags `vX.Y.Z` and `vX.Y.Z-pre`), reads all release pages, compares it with this package's version (`generator_version` in
`bundle.json`) and asks `vX.Y.Z available: update? [Y/n]`. `update` checks on request,
`--no-update` skips the check, `--yes` accepts without asking. Without a terminal it only says that
a newer version exists. Package QA sets `NFSU2_UPDATES=0`, which turns the check off.

What it downloads and installs:

- only `NFSU2-Generator-<platform>.zip` and its `.zip.sha256` from that release, both uploaded by
  the publishing account (`publisher` in `native/launcher.json`, the account `publish.py` uses);
  drafts, other tags and other uploaders are ignored;
- the ZIP must have the published size and the SHA-256 written in the `.sha256`, or nothing is
  installed;
- extraction refuses links, absolute paths, `..` and anything outside the package folder; the
  result must then pass the same check as every start (platform, every hash in `bundle.json`, no
  unlisted file) and its version must be the release tag.

Layout beside the extracted package, which itself is never changed:

```
NFSU2-Generator-<platform>/           the folder the tester extracted
  nfsu2-generator[.exe]               starts the version named in current.json
  output/                             default output folder, kept across updates
  .updates/current.json               {"version", "state": pending|confirmed, "previous", "from"}
  .updates/<version>/                 an installed package
  .updates/last-generation.json       the last verified game (ISO, output, version)
```

A new version is unpacked into `.updates/.staging-<version>`, checked, renamed into
`.updates/<version>`, and selected by replacing `current.json` (write, flush, rename): that rename
is the atomic switch. The previous version stays until the new one opens (its authenticated local
`/api/state` answers, or `doctor` passes; `update` also probes before confirming).
A new version that exits before opening goes back to the
previous one in the same launch, and so does one that has not opened after two launches. The
extracted executable runs an installed version only when its SHA-256 matches that version's
`bundle.json`; it never runs anything else. Removing an old version deletes only the files its
`bundle.json` lists. Extracting a newer package over the folder makes that package win.

After an update the page offers to update the game generated before (update mode, only changed
steps run again). If that update ends without a verified game, the page says to generate from
scratch in a new, empty output folder; the old game folder is kept.

These names are the contract between the extracted launcher and later versions: the
`NFSU2_LAUNCHER_ROOT` variable, exit code 75 (a version installed an update, start the new one),
`current.json` schema 1 and the `.updates/<version>` folders. Packages before the first launcher
do not check for updates: testers download that one by hand once. The launch scripts at the top of
the extracted folder stay those of the first package.

Updated launchers also accept `NFSU2_SESSION_STDIN=1` only when delegated inside this layout:
stdin holds one token JSON object (at most 16 KiB), then EOF. Consume and remove the flag before
starting the conversion runtime; never copy the credential to an environment variable or file.

### Release page and sign-in

During the closed beta the launcher reads `jussimir-labs/cache-2`: a public repository with
a neutral name, no description, README, topics or website, holding only the generator packages
under neutral file names (`asset_prefix` in `native/launcher.json`: `pack-<platform>.zip` and
`pack-<platform>.zip.sha256`; the folder inside each ZIP keeps its usual name). It reads it with
no sign-in, so testers are never asked for one. The beta repository keeps the release notes and
issues. For each version, create the release there by hand (title `vX.Y.Z`, no notes), then upload
the same checked files to both repositories with `publish.py vX.Y.Z ... --updates`.

A public repository appears in GitHub's search and may be indexed by search engines; there is no
unlisted repository. When the public beta starts, publish one last release there whose
`native/launcher.json` names the new release page, wait until testers have updated, then delete
`cache-2`.

The launcher always tries the release page without a sign-in first. Only when the page is private
(GitHub answers "not found" to anonymous readers) does it sign in, with GitHub's device flow. That
path is kept for a private release page: it prints a
short code and opens `https://github.com/login/device`, where testers enter it with their own
GitHub account that can read the repository. Nothing secret is inside the program: the client ID is
public. The token is kept only in the system password store (macOS Keychain, Windows Credential
Manager, the Secret Service used by libsecret on Linux: GNOME Keyring, KWallet's Secret Service,
KeePassXC). Without a store (Steam Deck Game Mode, an SSH session) the sign-in lasts until the
window closes, including a switch to a new version: an anonymous stdin pipe carries it only
between the verified launcher processes. It never enters argv, environment variables or a file,
and the conversion runtime does not receive it. `nfsu2-generator login` signs in and checks access;
it returns an error if that account cannot read the beta. `logout` removes the token.
Declining the sign-in turns the check off until `login`. On macOS, the Keychain may ask whether a
newly installed version may read the stored sign-in: *Always Allow*, or deny and sign in again.

Setup, once, by the organization owner (Jussimir):

1. On GitHub, open **jussimir-labs → Settings → Developer settings → GitHub Apps → New GitHub App**.
2. Name it, for example, *NFSU2 PSP Launcher*. Homepage URL: the beta repository. Leave the
   callback URL empty and *Request user authorization during installation* unchecked.
3. Check **Enable Device Flow**. Under *Webhook*, uncheck *Active*.
4. Permissions: **Repository → Contents: Read-only** (Metadata read-only is added by GitHub).
   No other repository, organization or account permission.
5. *Where can this GitHub App be installed?* **Only on this account.** Create the app.
6. Keep the default **user-to-server token expiration enabled**. Access tokens expire after
   8 hours. The launcher renews them with the refresh token; GitHub does not require a client
   secret to refresh a token obtained through Device Flow. If renewal is refused or the refresh
   token expires, the launcher asks the user to sign in again through Device Flow. See
   [GitHub's refresh documentation](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/refreshing-user-access-tokens).
7. **Install App → jussimir-labs → Only select repositories → `pessoal-nfsu2-psp-beta`.**
8. Copy the **Client ID** (not a client secret: none is needed, never add one) into `client_id` of
   `native/launcher.json` and commit it. Packages built from then on check for updates; with an
   empty `client_id` the check is silently off.

A tester's token can read only what both the app (Contents, this repository) and the tester can
read, as described in [GitHub's user-token documentation](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/generating-a-user-access-token-for-a-github-app).
Keep `scope` empty: do not substitute a personal token or the broad OAuth `repo` scope.
The checked-in public Client ID is `Iv23liOsr0uvsTwiZDZG` for **NFSU2 PSP Launcher**,
approved and installed on the single beta repository. No client secret, private key or personal
access token is used. On 3 November 2026 a temporary prerelease on the beta repository was found,
downloaded (132 MB), checked and installed by a macOS arm64 package through this app's token from
the Keychain, then deleted. A first device-code sign-in against real GitHub and the other systems
against real GitHub are still unqualified. The launcher never creates an app.

### Launcher tests

```sh
cargo test --locked --manifest-path native/Cargo.toml
cargo build --manifest-path native/Cargo.toml && cargo build --manifest-path native/Cargo.toml --example stub_runtime
python native/tests/test_launcher.py --helper native/target/debug/nfsu2-generator --stub native/target/debug/examples/stub_runtime
```

`test_launcher.py` builds small fake packages around the real launcher and a stand-in for the
bundled Python (`examples/stub_runtime.rs`), serves them from a local fake GitHub
(`tests/fake_github.py`: device sign-in, release list, assets through a storage redirect) and runs
the cycle: nothing newer, sign-in and update, start through the extracted launcher, refusal of a
wrong checksum, a changed file and another uploader, a new version that does not open, sign-out. It
also checks pagination, wrong size, declined/denied sign-in, disconnected API, confirmation
from a real terminal, unchanged ISO/save bytes, a zero-exit runtime whose API never answers,
and a failed update probe. It checks that the sign-in is never sent to the storage address.
Use `--report FILE.json` for a receipt. These are synthetic local checks; the stub is not the
real CPython/converter runtime. Fake GitHub checks on Apple Silicon do not qualify real GitHub
authentication or Windows, Linux or Intel Mac execution.
`--ssh HOST [--windows]
--remote-dir DIR` runs the same cycle on another computer through a reverse tunnel to the fake
server. The test sign-in uses a separate password-store entry, removed at the end;
`--session-vault` keeps it out of the store. `NFSU2_UPDATE_API`/`NFSU2_UPDATE_WEB` accept only
`http://127.0.0.1:PORT`.

To build a package with a new launcher over an earlier package's runtime, pass both
`--runtime-from` and `--helper` to `build_bundle.py`.

## Maintainer build

Packages are built and tested locally on each platform (Windows x64, Linux x86-64, Apple Silicon
and Intel Macs). A source-policy workflow is configured for pushes and pull requests when GitHub Actions is enabled. Build tools are needed **only on the maintainer machine**: Rust,
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
The FFmpeg archive is shipped as published. The shipped LAME archive is the published one
with a single file re-encoded: `ACM/tinyxml/readme.txt` (TinyXML's zlib license text) from
Windows-1252 to UTF-8, so the content check can read it; `build_tools.sh` still verifies the
upstream archive's SHA-256 before building. The jpegli archive is a tar of its pinned checkout
without its test data and fuzz/crash fixtures (`testdata`, libpng `contrib/testpngs`, skcms
test profiles), which are not sources of the built tool.
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
python native/publish.py vX.Y.Z native/dist/*.zip native/dist/*.sha256 --updates
```

`--updates` also uploads the ZIP and checksum files, after the same checks, to the release
`vX.Y.Z` of `jussimir-labs/cache-2` under the neutral names, the page the launcher reads (see
[Release page and sign-in](#release-page-and-sign-in)); that release must exist.

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

GitHub Actions is currently disabled for this repository. Local build, archive
and upload checks remain mandatory. The native workflow is guarded if Actions
is enabled later. Existing upstream source archives may also report optional
documentation and deliberately malformed test fixtures; review those findings
before approving a future runtime. A source-only CI pass never approves a ZIP.


### Update and conversion ownership

One OS lease at `.updates/launcher.lock` protects the whole folder, including downloads,
staging, pointer changes, rollback, sign-in changes and cleanup. A second launcher exits with
code 76 while an update or generation is active. The lock file stays in place: never delete it
as a way to unlock a running generation. The OS releases ownership after the last real holder
exits, including a converter whose ancestor was stopped unexpectedly.

The launcher passes the same open-file object to verified delegates and to the included Python.
Unix passes explicit descriptors; Windows duplicates the file handle directly into the child.
A delegate receives it through an anonymous bootstrap pipe, together with the session sign-in
when there is no password store; credentials travel only to another verified launcher and never
to conversion programs. Windows console input is reopened through `CONIN$` in the inherited
console; ordinary redirected input is duplicated and restored.

The included Python only inherits the object and keeps it open while it runs: it has no lease
code of its own, and the conversion programs it starts do not inherit it. The folder therefore
stays locked while the generator runs, even if a launcher above it is stopped. The conversion
recipe (`build_game.py`, `generator.py`, the converters) is unchanged by the launcher. Opening the
browser or the output folder is not part of the lease.

Installed updates require `launcher_protocol: 2` in the sealed bundle manifest. This prevents
an update from switching to an older helper that cannot adopt the ownership handoff. Launch
installed versions through the executable in the extracted package's root. Active ancestor
versions are preserved; if Windows refuses to delete an executable, its `bundle.json` removal
list is retained for a later attempt. Unlisted user files are preserved.

Before the first protected update, close all older launchers and direct Python generators.
The new launcher checks their existing per-bundle lock at admission, but an old binary started
after that check does not implement this protocol. No PID/environment assertion is an ownership
grant. These guarantees require the corrected binaries and actual-OS tests; earlier successful
platform receipts remain evidence only for their own recorded revisions.

The directed checks are in `native/tests/test_update_lease.py`: a pending version is not
confirmed by `login`, a second launcher is refused (code 76, no request, no change) during a
download and while the runtime runs, and a saved sign-in is renewed or replaced as GitHub answers.
`python native/tests/test_update_lease.py --helper H --stub S --platform P` runs them with the
system password store. They use synthetic files and the fake GitHub, and do not approve ISO
conversion or real GitHub distribution.
