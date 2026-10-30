# Your first RC21 test session

Start with the complete [generation and installation instructions](../README.md).
Back up your saves. Use the RC21 kit and a new output folder; do not combine candidates
or Xbox/PS2 data. Keep **Stability Control Off** and the build/FPS overlays visible.

## First-session checklist

1. **Generate:** confirm generation finishes with a matching reference. Note your computer OS
   and game source. If it fails, stop before installation and attach `relatorio.json` plus the failing log.
2. **Start:** launch from the XMB, reach the title and open the main menu. Note your PSP model,
   firmware/CFW and card. Report PPSSPP or Vita/Adrenaline separately if using them.
3. **Drive:** try a Quick Race with a stock car. Check steering, braking, handbrake, camera,
   sound, pause/resume and return to the menu. Report the car, event, transmission and settings.
4. **Progress/save:** if comfortable testing Career with backed-up saves, record a short play
   session, save through the normal menu, exit normally, reopen and check your progress.
   Describe what was saved and what returned. Do not interrupt the game while saving.
5. **Report:** file one issue per problem. Search existing reports first; add your reproduction
   to an existing report when it describes the same problem.

You do not have to complete all checks in one session. A short report with repeatable steps
is more useful than a long description without the device, location or actions.

## What to include

- **Version:** the version on the title screen or above the debug lines (for example `0.2.0-rc.1`). Closed Beta 1 — RC21 shows none: it is `0.1.0` (build `cdf93ef2c7`). See the [changelog](../CHANGELOG.md).
- **Environment:** device, firmware/CFW or emulator version, card and game source (US Xbox or PS2).
- **Location:** mode, track/screen, car, transmission, upgrades and relevant settings.
- **Steps:** start from launching the game and list the actions in order.
- **Result:** what happened, what you expected and whether it repeats.
- **Evidence:** a photo or short video with debug overlays visible, plus logs when relevant.

**L + R + SELECT** writes a technical `bug-N.n2bug` file in the game folder.
It does not take a screenshot. Zip that file before attaching it to GitHub.
Do not include your ISO, entire generated game folder, passwords or personal files.

[Open the English bug form](https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/new?template=bug_report.yml)
or read the [illustrated guide](bug-reporting-guide-EN.pdf).

If the game freezes, note the last visible screen/action and whether sound continues.
If no technical file was written, say so; a photo and reproduction steps are still useful.
