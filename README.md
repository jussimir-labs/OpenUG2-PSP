## Bug reports

Read [the illustrated English guide](docs/bug-reporting-guide-EN.pdf) ([Português](ptbr/manual-reportar-bugs.pdf)), review the [roadmap](ROADMAP.md), and open [an issue](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues).

# NFSU2 PSP: closed beta

*(Português: [LEIA-ME.md](ptbr/LEIA-ME.md))*

A fan-made native port of *Need for Speed Underground 2* to the PSP. This
repository holds **no game data**: you build the game from **your own
copy** with the tool in here. The PSP executable (`eboot/EBOOT.PBP`) comes
prebuilt.

Please do not share this repository or the folder it builds: access is
personal.

## Candidate 13 at a glance

This kit contains executable **e7ce75f8**. Human Drift now uses original-derived vehicle rules for the supported source configurations; custom dyno and special preset cars keep the previous model for the whole event. Local evidence includes three short Drift excerpts, private native SAVE/new-process checks from both sources, and one full naturally won Xbox 4101 race with its reward saved and restored in a fresh process.

These are scoped local checks. The new executable has no PSP/Memory Stick approval, complete-career or global-30-fps approval, and the audio/EA TRAX reports remain open for tester confirmation. Read [nightly notes](NIGHTLY_NOTES.md) and [known issues](KNOWN_ISSUES.md) before testing. The interface defaults to English; select Português for Portuguese. English files are in the root and Portuguese documentation/launchers in `ptbr/`.

## What you need

- **The Xbox ISO** of *Need for Speed Underground 2* (US, NTSC-U), dumped
  from your disc. The tool checks the ISO's hash or, for an image in
  another layout (a full dump, a rebuilt xiso), the game's 185 files one
  by one. The American **PS2 NTSC-U disc (SLUS_210.65)** is also integrated for validation; PAL/European revisions are refused. See `tools/PS2_SOURCE.md` for source differences. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for this candidate’s validation scope and limitations.
- **A PSP with custom firmware** (ARK-4/ARK-5 or similar) that runs
  homebrew from the Memory Stick. Development uses a **PSP-3000**. This
  overnight executable passed directed tests in extended-memory PPSSPP;
  it has not been installed or tested on hardware. Earlier human PSP-3000
  and Memory Stick results apply only to the previous executable. PSP-2000,
  Go, PSP-1000 and physical-save behavior remain unqualified. See
  [NIGHTLY_NOTES.md](NIGHTLY_NOTES.md) and [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
- **A Memory Stick** (or a microSD in an adapter) with **2.2 GB free**.
- **A Mac** (Apple Silicon or Intel), or a PC with **Linux**, or with
  **Windows and WSL**. It needs **12 GB free** on disk and about
  **1h30 for Xbox or 3h30 for PS2** on the reference Mac, depending on
  the computer and processing queue.

## 1. Build the game

Download and extract the complete repository (*Code > Download ZIP*, or
`git clone`). Open the launcher for your system; it starts a local helper
and opens the generator in your browser.

### macOS

Double-click [Generate-game.command](Generate-game.command) in the repository
root. If macOS blocks it, right-click, choose *Open* and confirm.

Python 3.9 or newer is needed to open the interface. Conversion requires
Python 3.12 or newer and its dependencies; the setup script uses Python
3.12/3.13 or `uv` to create `.venv/`. Install [Homebrew](https://brew.sh) and
Apple's command-line tools (`xcode-select --install`) before preparing the
converters. The launcher does not bundle these system requirements.

### Linux

From the repository root, run:

```sh
sh ./generate-game.sh
```

Python 3.9+ opens the interface; conversion needs Python 3.12+ and the
converter dependencies. On Debian/Ubuntu, setup uses `apt-get` and may
require administrator access. On other distributions, install the equivalent
packages listed by setup. The optional `zenity` or `kdialog` package enables
the native file picker; otherwise paste the full paths into the form.

### Windows

Install and initialize WSL with a Linux distribution first. Then double-click
[Generate-game.bat](Generate-game.bat) in the repository root. It opens the
same browser interface through WSL; the Linux Python and dependency
requirements above apply inside that distribution. Python, WSL and system
packages are not bundled, and this does not establish validation on every
Windows configuration.

### Generate from the browser

1. If prerequisites are missing, click **Prepare this computer**. The local
   helper downloads and installs converter dependencies and builds the movie
   encoder. Python modules go in `.venv/` and local tools in `.tools/`.
   System packages may require separate installation or administrator access;
   follow the requirements shown if preparation stops.
2. Choose your ISO and output folder, or paste their full paths. The folder
   picker creates `NFSU2-PSP` inside the selected folder; a typed output path
   is used as entered. Use a new folder with at least 12 GB free. Leave source
   detection on **Automatic** unless you need to select Xbox or PS2 explicitly.
3. Click **Generate PSP game**. The helper identifies the source, checks the
   ISO and original files, then converts the assets and assembles
   `<output>/PSP/GAME/NFSU2VIEW`. The browser workflow includes all **28
   movies** and has no option to omit them. Your ISO stays on your computer;
   it is not uploaded.
4. Follow the stages and logs. A successful result requires matching the
   reference for the same source and executable. A mismatch or missing
   reference remains a diagnostic result, with files preserved. This checks
   file identity; the beta's [known issues](KNOWN_ISSUES.md) remain valid.
5. Use **Open generated folder** after success. The helper does not erase
   existing user folders or saves, and does not copy anything to the PSP.
   Follow the installation section below separately. Its examples use the
   default `output` output; substitute your chosen output folder if different.

Use **Download logs** when reporting a problem, and keep the step logs in
`<output>/registros/`. To interrupt, click **Cancel process** and wait for the
converters to stop. Compatible partial stages can be resumed only with the
same ISO, source, executable, converters and options. If the recipe changes,
choose a new output folder; the browser does not erase an old output to start
over.

Closing the browser tab does not stop the local helper or an active job.
When no job is running, click **Close helper** to shut it down; reopen
the launcher to use it again.

Advanced command-line options remain available from the repository root:
`.venv/bin/python build_game.py --help`.

## 2. Install on the PSP

1. Connect the PSP over USB and, in the XMB, open
   *Settings > USB Connection*.
2. Copy the folder manually. Check the destination card volume and back up
   `PSP/SAVEDATA` first. The macOS command below updates changed files; do not
   add `--fresh` automatically.
   - **macOS:** do not copy with the Finder (it leaves a `._` file beside
     each one, and the game gets slower on the card). In Terminal, in this
     folder, run:
     ```sh
     .venv/bin/python tools/ms_package.py push /Volumes/<card name> output
     ```
   - **Windows and Linux:** copy `output/PSP` to the card's root, merging
     it with the `PSP` folder already there.
3. Eject the card and leave USB Connection (the O button).
4. In the XMB, under *Game > Memory Stick*, open **NFSU2 PSP**.

Saves live in the XMB's *Saved Data* and survive a new copy.

## Credits and licences

See [THIRD_PARTY.md](THIRD_PARTY.md) and the `licenses/` folder.

Current completion evidence also includes one full three-lap Drift4312 event: 1,880 points, seven drifts and fourth place (a real loss), with pause/resume and menu/music recovery. See the nightly notes for scope.
