## Bug reports

Read [the illustrated reporting guide](docs/manual-reportar-bugs.pdf) and open [an issue](https://github.com/jussimir/pessoal-nfsu2-psp-beta/issues).

# NFSU2 PSP: closed beta

*(Português: [LEIA-ME.md](LEIA-ME.md))*

A fan-made native port of *Need for Speed Underground 2* to the PSP. This
repository holds **no game data**: you build the game from **your own
copy** with the tool in here. The PSP executable (`eboot/EBOOT.PBP`) comes
prebuilt.

Please do not share this repository or the folder it builds: access is
personal.

## What you need

- **The Xbox ISO** of *Need for Speed Underground 2* (US, NTSC-U), dumped
  from your disc. The tool checks the ISO's hash or, for an image in
  another layout (a full dump, a rebuilt xiso), the game's 185 files one
  by one. The American **PS2 NTSC-U disc (SLUS_210.65)** is also integrated for validation; PAL/European revisions are refused. See `tools/PS2_SOURCE.md` for source differences. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for this candidate’s validation scope and limitations.
- **A PSP with custom firmware** (ARK-4/ARK-5 or similar) that runs
  homebrew from the Memory Stick. Development uses a **PSP-3000**. This
  executable passed directed tests in extended-memory PPSSPP and a human
  PSP-3000 host0 title/driving/menu check. PSPLink cleanup required manual
  recovery and is complete; card installation, physical saves, PSP-2000, Go and PSP-1000 remain
  unqualified. See [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
- **A Memory Stick** (or a microSD in an adapter) with **2.2 GB free**.
- **A Mac** (Apple Silicon or Intel), or a PC with **Linux**, or with
  **Windows and WSL**. It needs **12 GB free** on disk and about
  **1h30 for Xbox or 3h30 for PS2** on the reference Mac, depending on
  the computer and processing queue.

## 1. Build the game

### macOS

1. Get the repository (*Code > Download ZIP*, or `git clone`).
2. Double-click **`Gerar-jogo.command`**. If macOS refuses to open it,
   right-click it, choose *Open* and confirm.
3. The first time, it sets the computer up (`setup.sh`): it asks for
   [Homebrew](https://brew.sh) and Apple's tools (`xcode-select --install`)
   if missing, asks before installing programs (ffmpeg, lame, cmake…) and
   builds the movies' encoder. Its Python modules go to `.venv/`, inside
   this folder.
4. Drag the ISO onto the window and press Enter.

### Linux

```sh
sh ./gerar-jogo.sh /path/to/your.iso
```

The first time it runs `setup.sh`, which uses `apt` (Debian, Ubuntu); on other distributions it prints
the packages to install.

### Windows

Windows goes through WSL (the Linux that comes with it). Once, open
PowerShell as administrator, run `wsl --install` and restart. Then drag
the ISO onto **`Gerar-jogo.bat`** (or double-click it and drag the ISO
onto the window).

### What happens

The tool prints each step with a percentage. It:

1. checks the ISO;
2. extracts the disc's files;
3. converts the city, tracks, cars, screens, audio and movies (about 80
   steps);
4. assembles the card's folder in **`saida/PSP/GAME/NFSU2VIEW`**;
5. checks each generated file against the reference for the same source and
   executable. Missing references or mismatches return an error and keep
   diagnostic files. This checks output identity; the game’s known limitations
   remain listed in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

It writes only inside `saida/`. If it stops halfway (out of space, the
computer turned off), run it again: finished steps are reused only with the same ISO, source, executable, converter sources and options. A changed recipe requires a new output folder or `--do-zero`.
`--do-zero` starts over. Each step's log is in `saida/registros/`; send
those with your report if something fails.

More options: `.venv/bin/python build_game.py --help`.

## 2. Install on the PSP

1. Connect the PSP over USB and, in the XMB, open
   *Settings > USB Connection*.
2. Copy the folder:
   - **macOS:** do not copy with the Finder (it leaves a `._` file beside
     each one, and the game gets slower on the card). In Terminal, in this
     folder, run:
     ```sh
     .venv/bin/python tools/ms_package.py push /Volumes/<card name> saida --fresh
     ```
   - **Windows and Linux:** copy `saida/PSP` to the card's root, merging
     it with the `PSP` folder already there.
3. Eject the card and leave USB Connection (the O button).
4. In the XMB, under *Game > Memory Stick*, open **NFSU2 PSP**.

Saves live in the XMB's *Saved Data* and survive a new copy.

## Credits and licences

See [THIRD_PARTY.md](THIRD_PARTY.md) and the `licenses/` folder.
