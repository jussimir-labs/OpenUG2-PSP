# NFSU2 PSP — Closed Beta 1 (RC21)

Welcome to the first closed beta of the native PSP port of *Need for Speed Underground 2*.
Thank you for helping test it on real devices.

**Start here:** use the **native generator ZIP for your computer**, extract the entire folder,
and follow the steps below. The packages are on the
[latest native generator release](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases/tag/v0.1.2).
Windows x64, Linux x86-64, Apple Silicon and Intel Mac use separate downloads. The original
[RC21 source kit](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases/tag/v0.1.0)
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
- **Windows 10 version 2004 or later / Windows 11 (x64)**, **Linux x86-64** with glibc 2.35
  or later (SteamOS 3 on a Steam Deck or Steam Machine, Ubuntu 22.04+, Debian 12+, Fedora 36+,
  Arch and similar), or **macOS 15 or later** with the matching Apple Silicon or Intel package.
  Windows ARM and ARM Linux packages are not included. On Linux, use the native Linux package,
  not the Windows one under Proton/Wine (not tested).
  **You do not need to install Python, WSL, Homebrew, a compiler, or conversion libraries.**
  A complete PS2 conversion took about 3 hours on a Steam Deck running Windows with RC21, and
  about 55 minutes on an Apple M5 Pro Mac; the next generator converts several steps at once
  and takes about 1 hour 40 minutes on that Steam Deck (about 1 hour 30 minutes with the Linux
  package on SteamOS), and about 17 minutes (PS2) and 20 minutes (Xbox) on that Mac, with the same
  verified output.
- **Not yet tested with the 29 October generator:** the Intel Mac package, an Xbox conversion
  on Windows or Linux, Linux distributions other than SteamOS, and updating a previous output
  folder from the browser page (the update was verified on Mac, Windows and SteamOS with the same
  command the page runs). If you use one of these, please report how it went, even when it works.

## 1. Open the generator

Keep the extracted kit together; do not run a launcher from inside the ZIP.

| Computer | Download | Open |
| --- | --- | --- |
| Windows x64 | `NFSU2-Generator-windows-x64.zip` | Double-click `Generate-game.bat` |
| Mac with Apple Silicon | `NFSU2-Generator-macos-arm64.zip` | Double-click `Generate-game.command` |
| Mac with Intel processor | `NFSU2-Generator-macos-x64.zip` | Double-click `Generate-game.command` |
| Linux x86-64, including SteamOS | `NFSU2-Generator-linux-x64.zip` | Open `Generate-game.sh` (see below) |

Keep the terminal window open while using the browser interface. All required conversion tools
are included in the download. The generator verifies its files on startup and does not install
anything on your system. If a file is missing or damaged, extract the complete download again.
Packages are not commercially signed or Apple-notarized, so the first launch needs one approval:

- **Windows:** if *Windows protected your PC* appears, click **More info → Run anyway**.
  If Windows asks whether to run a file from the internet, choose **Run**.
- **Mac:** the first double-click shows *"Generate-game.command" Not Opened*. Click **Done**,
  open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** next to
  `Generate-game.command`, then confirm. The launcher then clears the download flag on its own
  folder, so macOS does not ask again for the included tools.
- **Linux:** the package keeps its execute permissions when extracted with the desktop's archive
  tool (Ark, GNOME File Roller) or `unzip`. If a double-click on `Generate-game.sh` opens it as
  text, open **Properties → Permissions**, enable **Is executable** (GNOME: *Executable as
  Program*), and open it again; or run it from a terminal as shown below. Either way the launcher
  restores the permission of the included tools by itself.

### Linux and SteamOS (Steam Deck, Steam Machine)

Use **Desktop Mode** on SteamOS: press the Steam button → **Power → Switch to Desktop**.
The generator does not run from Game Mode.

1. Download `NFSU2-Generator-linux-x64.zip` with a desktop browser into **Downloads**.
2. In the **Dolphin** file manager, right-click the ZIP → **Extract → Extract here**.
3. Open the extracted `NFSU2-Generator-linux-x64` folder and double-click `Generate-game.sh`.
   If Dolphin asks what to do, choose **Execute** (not *Open*). A terminal window opens with the
   helper; keep it open. The generator page then opens in your default browser.
   **Close helper**, at the bottom of the page, ends the helper and closes that window.
4. If no browser opens, copy the `Local interface: http://127.0.0.1:…` line from the terminal
   into a browser. The address works only on this computer.

From a terminal (Konsole on SteamOS), the same thing is:

```sh
cd ~/Downloads/NFSU2-Generator-linux-x64
sh Generate-game.sh
```

Choose an output folder on a drive with 12 GB free, such as `~/NFSU2-PSP` in your home folder.
A microSD card used by Steam also works if it has the space. The ISO and output pickers use the
desktop's file dialog (KDE or GNOME); without one, paste full paths into the fields.

## 2. Generate the game from your own ISO

1. The helper opens the local browser interface. **Verify included tools** checks your download if needed; there is no dependency installation step.
2. Choose your ISO and a **new, empty output folder**. Keep source detection on **Automatic**.
   The folder picker creates `NFSU2-PSP` inside the selected folder; a typed path is used as entered.
3. Click **Generate PSP game**. The tool validates the source, extracts it locally, converts the
   assets including all 28 movies, and compares the result against the RC21 reference.
4. Wait for successful identity verification. The game is in `<output>/PSP/GAME/NFSU2VIEW`.
   On Windows, Linux and Intel Macs some movies, audio, geometry and racing-line files are reported as
   **equivalent**: the same structure, with bytes rounded differently by an x86 processor. That
   is a successful result.
   A failed or mismatching result is diagnostic output; attach `relatorio.json` and the failing
   step log when asking for help. Do not install an output that failed verification.
5. Click **Open generated folder** when complete. The helper does not install the game on your PSP.

To stop generation, use **Cancel process** and wait for the converters to exit. A compatible
interrupted job can resume only with the same ISO, source, executable, tools and options.
Close the local helper with **Close helper** when no conversion is running; closing the browser
tab alone does not stop it.

### Updating to a newer generator

When a new generator version is released, you do not need to generate everything again.
Extract the new download, open it, and choose **the same ISO** and **the output folder of your
previous generation** (the folder that contains `PSP`). The generator then updates that folder:

- it re-converts only the steps whose converter, options or inputs changed since the version
  that made your folder, and extracts your ISO again only if one of those steps reads it;
  every other file is reused from your folder. A release that only changes the game's
  `EBOOT.PBP` updates in seconds; one that changes converters takes as long as those steps.
  The first update from RC21 is the exception: that release changed the slowest converters,
  so it takes about as long as a complete generation;
- it installs the new `EBOOT.PBP` and verifies the complete result against the new reference,
  exactly as a full generation does;
- your previous `PSP` folder is kept (in `.anterior` inside the output folder) until the new one
  passes verification, and is restored if it does not.

The update falls back to a complete generation, and says why in the log, when the previous
output cannot be reused: a different ISO or source, a modified or incomplete `PSP` folder, a
generator version it does not know, or different movie options. Nothing derived from the game is
downloaded: every converted file still comes from your own ISO, on your computer. After updating,
copy the complete `PSP/GAME/NFSU2VIEW` folder to the card again (section 3).

Advanced CLI: `./nfsu2-generator serve --no-browser` starts the same interface without opening a tab; `./nfsu2-generator doctor` checks the included tools. On Windows, use `nfsu2-generator.exe` instead; on Linux and Mac, run these from the extracted folder. Developer/source-only CLI: use your Python environment to run `build_game.py --help`. Some converter logs and CLI option
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

**Updating an older beta:** update your previous output folder (above) or generate in a new one, and install the complete result.
Both `EBOOT.PBP` and `pack.n2pk` changed. Copying only the executable leaves the older physics data.
Never mix Xbox and PS2 output, and do not delete `PSP/SAVEDATA` when updating.

## 4. Test and report

Follow the [first-session checklist](docs/TESTING.md). Leave **Stability Control Off** in RC21;
the On option does not affect this executable. Keep the FPS/build overlays visible in evidence.
This is an unfinished beta, with known gameplay, visual, audio and performance limitations.

Report one problem per [GitHub issue](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/new?template=bug_report.yml).
Include **Closed Beta 1 — RC21**, device/firmware, game source, car/mode/track, steps, expected
and actual behavior, and a photo or short video when possible. **L + R + SELECT** writes a
technical bug file, not a screenshot. Zip the generated `bug-N.n2bug` before attaching it.
See the [illustrated English guide](docs/bug-reporting-guide-EN.pdf).

## Build identity

| Item | RC21 value |
| --- | --- |
| Embedded build | `cdf93ef2c7` |
| EBOOT SHA-256 | `ff35c0a14190edf67de263259bfb9fa3831a75363ee2c7b7ee332196bb29df39` |
| Xbox data ID | `bfaa` |
| PS2 data ID | `804e` |
| Memory profile | Extended memory (`PSP_LARGE_MEMORY=1`) |

These values identify the kit; you do not need to calculate hashes to report a bug.
See [qualification scope](docs/qualification/RC21-QUALIFICATION.json) and
[third-party notices](THIRD_PARTY.md). Portuguese historical material remains in `ptbr/`;
the English instructions on this page are authoritative for the native packages. The original RC21 ZIP retains its original setup instructions.

See [native packaging and validation](native/README.md) for the exact qualification scope. A successful tools check does not prove a complete ISO conversion on every OS.
