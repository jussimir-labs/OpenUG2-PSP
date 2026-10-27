# NFSU2 PSP — Closed Beta 1 (RC21)

Welcome to the first closed beta of the native PSP port of *Need for Speed Underground 2*.
Thank you for helping test it on real devices.

**Start here:** use the **native generator ZIP for your computer**, extract the entire folder,
and follow the steps below. Native packages are built by the
[Native browser generator workflow](https://github.com/jussimir/pessoal-nfsu2-psp-beta/actions/workflows/native-generator.yml).
Windows x64, Apple Silicon and Intel Mac use separate downloads. The original
[RC21 source kit](https://github.com/jussimir/pessoal-nfsu2-psp-beta/releases/tag/beta-1-rc21)
remains available with its older setup requirements.
Please keep this beta and its generated files within the selected test group.

This kit contains the prebuilt PSP executable and a local game-data generator.
**No ISO, original game assets or saves are included.** Use a dump of your own game disc.
Your ISO stays on your computer.

[Release notes](RELEASE_NOTES.md) · [Known issues](KNOWN_ISSUES.md) ·
[Testing and bug reports](docs/TESTING.md) · [Roadmap](ROADMAP.md)

## Before you begin

- A **US/NTSC-U Xbox ISO**, or the **US PS2 edition SLUS_210.65** of NFS Underground 2.
  PAL and other revisions are not supported. The original US PS2 disc image is not independently
  verified; PS2 source support is based on the retained file-level reference. If verification fails,
  keep the report and contact the organizer; do not bypass it.
- A PSP with custom firmware that can run homebrew. **RC21 is an extended-memory build.**
  The organizer accepted it for this first beta after testing; PSP-1000 compatibility is not
  established. If you only have a PSP-1000, contact the organizer before attempting this build.
  PSP-2000/3000, Go, Street, Vita/Adrenaline and PPSSPP results should be reported separately;
  compatibility with every device and firmware is not guaranteed.
- At least **2.2 GB free on the Memory Stick** and **12 GB free on the computer**, in addition
  to your existing ISO. Back up your saves before replacing game files.
- **Windows 10 version 2004 or later / Windows 11 (x64)**, or **macOS 15 or later**
  with the matching Apple Silicon or Intel package. Windows ARM and Linux native packages
  are not included in this release. The original source kit remains available for Linux.
  **You do not need to install Python, WSL, Homebrew, a compiler, or conversion libraries.**
  Reference conversion times were about 1.5 hours for Xbox and 3.5 hours for PS2; your computer may differ.

## 1. Open the generator

Keep the extracted kit together; do not run a launcher from inside the ZIP.

| Computer | Download | Open |
| --- | --- | --- |
| Windows x64 | `NFSU2-Generator-windows-x64.zip` | Double-click `Generate-game.bat` |
| Mac with Apple Silicon | `NFSU2-Generator-macos-arm64.zip` | Double-click `Generate-game.command` |
| Mac with Intel processor | `NFSU2-Generator-macos-x64.zip` | Double-click `Generate-game.command` |

Keep the terminal window open while using the browser interface. All required conversion tools
are included in the download. The generator verifies its files on startup and does not install
anything on your system. If a file is missing or damaged, extract the complete download again.
If the OS blocks the helper, review the warning and use the normal OS process for opening a
trusted local application. Packages are not commercially signed or Apple-notarized.

## 2. Generate the game from your own ISO

1. The helper opens the local browser interface. **Verify included tools** checks your download if needed; there is no dependency installation step.
2. Choose your ISO and a **new, empty output folder**. Keep source detection on **Automatic**.
   The folder picker creates `NFSU2-PSP` inside the selected folder; a typed path is used as entered.
3. Click **Generate PSP game**. The tool validates the source, extracts it locally, converts the
   assets including all 28 movies, and compares the result against the RC21 reference.
4. Wait for successful identity verification. The game is in `<output>/PSP/GAME/NFSU2VIEW`.
   A failed or mismatching result is diagnostic output; attach `relatorio.json` and the failing
   step log when asking for help. Do not install an output that failed verification.
5. Click **Open generated folder** when complete. The helper does not install the game on your PSP.

To stop generation, use **Cancel process** and wait for the converters to exit. A compatible
interrupted job can resume only with the same ISO, source, executable, tools and options.
When changing kit versions, use a new output folder. Close the local helper with **Close helper**
when no conversion is running; closing the browser tab alone does not stop it.

Advanced CLI: `./nfsu2-generator serve --no-browser` starts the same interface without opening a tab; `./nfsu2-generator doctor` checks the included tools. On Windows, use `nfsu2-generator.exe` instead. Developer/source-only CLI: use your Python environment to run `build_game.py --help`. Some converter logs and CLI option
names are in Portuguese; the browser instructions are in English.

## 3. Install on the PSP

1. Back up `PSP/SAVEDATA`. Connect the PSP by USB and select **Settings → USB Connection**.
2. Confirm the destination is your PSP's card. Copy the generated `PSP/GAME/NFSU2VIEW` folder
   to the card's corresponding folder. Keep the complete RC21 folder together.
   On Windows/Linux, merge the generated `PSP` folder with the card's existing `PSP` folder.
3. On macOS, use the included copy helper to avoid Finder's `._` sidecar files:

   ```sh
   runtime/python/bin/python3 tools/ms_package.py push "/Volumes/YOUR PSP CARD" "/path/to/output"
   ```

   Replace both paths with your actual card and generated output. Do not add `--fresh`.
4. Eject the card safely, leave USB Connection, and launch **NFSU2 PSP** under
   **Game → Memory Stick** in the XMB.

**Updating an older beta:** generate RC21 in a new output folder and install the complete result.
Both `EBOOT.PBP` and `pack.n2pk` changed. Copying only the executable leaves the older physics data.
Never mix Xbox and PS2 output, and do not delete `PSP/SAVEDATA` when updating.

## 4. Test and report

Follow the [first-session checklist](docs/TESTING.md). Leave **Stability Control Off** in RC21;
the On option does not affect this executable. Keep the FPS/build overlays visible in evidence.
This is an unfinished beta, with known gameplay, visual, audio and performance limitations.

Report one problem per [GitHub issue](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues/new?template=bug_report.yml).
Include **Closed Beta 1 — RC21**, device/firmware, game source, car/mode/track, steps, expected
and actual behavior, and a photo or short video when possible. **L + R + SELECT** writes a
technical bug file, not a screenshot. Zip the generated `bug-N.n2bug` before attaching it.
See the [illustrated English guide](docs/bug-reporting-guide-EN.pdf).

## Build identity

| Item | RC21 value |
| --- | --- |
| Embedded build | `cdf93ef2c7` |
| EBOOT SHA-256 | `42d252dfb8a5f5f8d195027ad816ac0fa661fe3194e1de20f72a3ed29f436a95` |
| Xbox data ID | `bfaa` |
| PS2 data ID | `804e` |
| Memory profile | Extended memory (`PSP_LARGE_MEMORY=1`) |

These values identify the kit; you do not need to calculate hashes to report a bug.
See [qualification scope](docs/qualification/RC21-QUALIFICATION.json) and
[third-party notices](THIRD_PARTY.md). Portuguese historical material remains in `ptbr/`;
the English instructions on this page are authoritative for the native packages. The original RC21 ZIP retains its original setup instructions.

See [native packaging and validation](native/README.md) for the exact qualification scope. A successful tools check does not prove a complete ISO conversion on every OS.
