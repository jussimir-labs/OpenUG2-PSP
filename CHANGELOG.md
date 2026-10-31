# Changelog

All notable changes to the NFSU2 PSP closed beta are listed here, newest first.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the
version numbers follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html):

- **0.y.z** are beta versions; **1.0.0** will be the first stable release.
- The **middle number** goes up with a larger change to the game (0.2.0: the first beta
  with PSP-1000 support).
- The **last number** goes up with fixes, and with generator updates that leave the game unchanged.

Each version is a release on this repository, tagged `vX.Y.Z`, and its section below is the
text of that release. The game shows its version on the title screen and above the debug
lines; please include it when you report a problem. Test builds carry a suffix, such as
`0.2.0-rc.1`.

## [Unreleased]

## [0.2.3] - 2026-10-31 — Rocket Ride

A new game executable and new converted game data: update your 0.2.0, 0.2.1 or 0.2.2 output
folder with the 0.2.3 generator (same ISO) or generate into a new folder, and install the
complete result. The cars and their rims, brakes and mirrors, the neon underglow, the shops'
Visual Rating, the city, the tracks and their walls are converted again; copying only the
executable is not enough, and the 0.2.2 executable cannot read the new cars and rims.

### Added

- Customization: the Visual Rating meter is back at the right of the shop screens, as in the
  original game: the stars of the car as you browse, with two decimals, and a bar that fills
  toward the next star. It turns green when the part, paint, rims, vinyl or decal you are
  looking at would raise your rating and orange when it would lower it, so you can see what
  will get you onto a magazine cover before you pay. It shows in the parts, paint, rims,
  spinners, vinyls and decals browsers, in the career's shops and in Customize Car ([#58]).

### Changed

- Underglow: the glow your neon throws on the asphalt is now each car's own light pattern
  from the original game, laid on the road under the car the way the console versions lay
  it: a bright arc under the front bumper, a line along each skirt and one under the rear,
  following the street's slopes and crowns. It fades as the car leaves the ground over a
  jump and is a little stronger right under the bumper than before. The tubes themselves
  are unchanged ([#47]).

### Fixed

- No more invisible bumps along walls and highway barriers: where one stretch of a wall ended
  exactly on the edge of the game's collision grid, a 1 m stub of invisible wall stuck out into
  the road. Along the diagonal highway walls there was one every metre, so the car rode on
  their tips more than half a metre from the wall you could see, bumping as it went. Wall rides follow
  the real wall again, as in the original game ([#32]).
- The matte black trim on your car no longer catches a bright white glint on the street. Since
  the trim started using the car's own skin art in 0.2.0, the strip across the 206's rear bumper
  and other matte parts facing the sky shone like chrome under the streetlights; they stay dark
  now, as on the PS2 ([#21]).
- Your crew no longer phones you as Rachel once her 350Z is back in her hands. The two calls
  that open with "Yo, Rachel!" and "Hey Rachel, what up girl?" belong to the prologue, while
  you cruise Bayview in her car: they now come 15 and 40 seconds into that first free roam, as
  in the original game, and never after you've picked your own ride. Before, they kept coming
  in every session later in the career. The tips about races in other neighbourhoods also no
  longer pick the Fort Union call meant for Rachel. And as in the original, the prologue's
  races only show up on the map once that first call comes in, and they all close once the
  last of them is run, won or not, leaving the Car Lot as the next stop ([#57]).
- Car Specialties: when the trunk opens for Trunk Audio or Trunk Neon, everything mounted on
  the lid now lifts with it, as in the original game: the spoiler, the tail light pieces built
  into the lid and, on cars such as the Civic, the 206 and the Focus, the license plate. The
  lid's inner panel goes up too, so the trunk's interior and its audio layout show instead of
  a dark panel left behind, and on hatchbacks a rear window decal rises with the hatch ([#42]).
- Free roam: the rivals cruising the city, the cars you can challenge to an outrun, now sit on
  the road on slopes and banked corners, tilted with it like the rest of the traffic. Before,
  they stayed level and seemed to float along the road at an angle ([#46]).
- The skid sound no longer loops over the movie when you first pull into The Garage to hand
  Rachel's 350Z back. Before any movie the street goes quiet (tyres, wind, traffic, turbo, the
  plane overhead), as in the original game; before, whatever the car was doing on the last frame
  kept sounding until the movie ended ([#41]).
- Drag: lane changes are quicker to settle and smoother, as in the original game. The car heads
  for the new lane at the original's rate and settles in it, instead of swinging a couple of
  metres past it and weaving back. A short tap moves one lane, and steering into a rival
  alongside is called off instead of shoving the rival out of its lane ([#43]).
- Nitrous flames now shoot from the exhaust every time you hit the nitrous, at any speed and
  with any car. Before, each flame was drawn a frame ahead of the car, so once you were moving
  it ended up hidden inside the bodywork and only showed when the nitrous was fired almost from
  a standstill. They also came from a fixed spot high on the rear, near the tail lights, and
  inside the rear bumper on long cars such as the Escalade. Now your car's flames come out of
  its own exhaust tips, as in the original game: one flame on cars with a single tip, and the
  tips you fit at the shop move them too. The race intros and endings show them the same way
  ([#49]).
- Underglow neon: its light now lies on the road under the car wherever you drive, on crowned
  streets, slopes and bumps and while the car leans in a turn. Before, parts of the glow sank
  under the asphalt and vanished from under the car ([#47]).
- Suspending the PSP during play no longer cuts the music: after you switch it back on, the EA
  Trax song carries on from where it was, instead of playing a second of it and starting over
  from the top ([#48]).
- Side mirrors from the Body Shop now sit as a matching pair. The right one was flipped and
  pointed back into the cabin, on every car that takes them, from the Civic to the Hummer
  ([#50]).
- Rachel's 350Z now has the parts the original game gives it: every performance package up to
  the second level, as on the PS2 and PC, not only the turbo and the nitrous. It pulls harder
  and runs faster, as in the prologue of the console versions ([#45]).
- Cars turn harder in fast corners, as in the original game, which helps with the understeer
  some of you reported. The tyres now grip more as the downforce presses the car down at speed,
  so from about 80 km/h (50 mph) the car follows the stick into a bend instead of running wide
  and asking for the handbrake: about a quarter more turn at 145 km/h (90 mph), checked against
  Rachel's 350Z on the PS2. Slow corners are unchanged ([#45], [#55]).
- Shops: the floor of the City Core Graphics Shop, by the casino, no longer glitches while your
  car drives in. With the game generated from the PS2 disc, the pale paving of the plaza outside,
  which runs on under the shop, broke through the shop's dark floor in jagged, flickering
  stripes. The shops' floors now cover it, as in the original game. The fix is in the converted
  city, so it takes effect once the game is generated again ([#60]).
- Body Shop: fitting new side mirrors now takes the car's own off. On ten cars, among them the
  Corolla, the Tiburon, the Eclipse and the Skyline, the stock mirror stayed on next to the new
  one; on the Navigator, the Escalade, the Mustang GT, the Lancer and the Impreza WRX STi the
  shop had no mirrors to sell at all. Now they all do ([#56]).
- Rims now fit the car they go on, as in the original game. The shop offers each car only the
  sizes made for its tyres (16 to 18 inches on the Corolla, 18 to 20 on the 350Z), and the rims
  keep their real size on all four wheels. Before, every car got every size, and on the Corolla
  the rear rims came out about a quarter bigger than the front ones ([#56]).
- Brake upgrades now show on the car, as in the original game. The Street, Pro and Extreme
  brakes from the Performance Shop each fit their own discs and calipers behind the front
  wheels, and Rachel's 350Z wears her Pro brakes. With upgraded brakes on, Graphics Shop >
  Paint > Brakes paints their calipers; the stock brakes keep their own look, so that option
  appears once the brakes are upgraded ([#59]).

## [0.2.2] - 2026-10-30 — Riders on the Storm

A new game executable and new converted game data: update your 0.2.0 or 0.2.1 output folder with
the 0.2.2 generator (same ISO) or generate into a new folder, and install the complete result.
The traffic routes, the cars and their lights, the menus' and shops' scenes, the rain, the
city's walls and the street's markers are converted again; copying only the executable is not
enough.

### Added

- In the menus the analog stick turns your car on its turntable, as the right stick does in the
  console versions; the car stays where you leave it, and the D-pad still moves through the
  menus ([#29]).

### Fixed

- Menus: holding a direction on the D-pad now keeps moving through the options after a short
  pause, as in the original game. The menu icons are back to the original size and shape (the
  selected one full size, its neighbours a little smaller) instead of small and squashed, the
  dot on the bar above them follows the selected option, and the thin lines at both ends of
  the icon panel are gone ([#29]).
- The turntable in the menus no longer shows a second striped rim under its own: the extra ring
  belongs only to the floor's reflection in the original game. The car on it now has its
  headlights and tail lights off, as in the original, and the EA Trax card slides in at the
  left edge of the screen instead of covering the car ([#29]).
- Every shop's door is marked on the street again, as in the original game: the shop's icon
  over a turning ring of light on the ground, with the glow that pours down the beam from the
  roof, in the shop's own colour (green at the Body Shop). Hidden shops have theirs too, and in
  the prologue only the Car Lot and, once you have your own car, The Garage show it. Standing
  right in the glow as you leave a shop, it fades away near the camera to keep the frame rate
  ([#34]).
- Performance packages now cost less than their parts bought one by one, as in the original
  game: a package with two or more parts still to fit takes 10% off, and Megalow Parts, the
  hidden shop, takes 20%. The package screen shows the discount above the Total Cost ([#35]).
- Every shop now has its own room, as in the original game: the Car Lot, the performance
  shops, the Body Shops, the Graphics shops, the Car Specialties shops and The Garage each show
  their own interior around the car, instead of the main menu's street ([#36]).
- The camera stays on the scene during the career's opening at the airport until Rachel hands
  over the 350Z, as in the original game: the change-camera and look-back buttons wait for it,
  and with the Bumper or Hood camera chosen in Options the opening's shots are no longer
  replaced by the view from the car. The same applies to the scene on the grid before a race. ([#25])
- Career: you can no longer drive away from the Car Lot in Rachel's 350Z without picking your
  first car. Backing out now keeps you in the lot with Rachel's reminder that she needs her
  car back, as in the original game, from the street's Car Lot and from The Garage alike; the
  first car's question no longer offers to trade in a car that is Rachel's ([#26]).
- Traffic on the on-ramp that climbs to the overpass near the first car lot no longer drops to the
  street under the overpass and drives across it; the cars stay on the ramp, as in the original
  game ([#27]).
- Drafting now shows while it happens: tuck in behind a rival and "DRAFTING" appears above
  the nitrous gauge with its points counting up, as in the console versions. Before, the word
  only flashed after you left the slipstream and the points never showed. Power slides, hang
  time and the other bonuses that last show the same way, and every bonus shows its points
  in a car with nitrous ([#24]).
- Pushing against a wall with the throttle held no longer brings a high-pitched buzz every two
  seconds or so. The engine sound held at its lowest revs ran past the end of its recording
  and played noise; it now keeps a steady engine note, in every car ([#28]).
- The decals on Rachel's 350Z (the HKS and Sparco on its rear quarters) and every decal from the
  Paint Shop now sit on the paint. Before, they hovered a few centimetres off the body and showed
  as dark slivers beside the rear fenders when seen from behind ([#30]).
- Wheels sit where the original game puts them, their outer face just inside the fender. The
  Civic's stood out of its arches as if on spacers, and other cars' sat too deep (the 350Z, the
  Miata, the Tiburon). Rims from the shop and wide body kits follow the same rule ([#31]).
- No more rain inside tunnels and under covered roads: as in the original game, the drops stop
  where the road is covered and start again as you drive out into the open, while a road on
  top of the tunnel still gets the rain ([#33]).
- The "i" tips, the cash pickups and the other street markers no longer float above the road:
  a marker that comes into range before its patch of street has finished loading now waits for
  the ground instead of taking your car's height at that moment ([#38]).
- The chase camera no longer slips through tall walls: back the car up against a building or a
  high concrete wall and the camera now stops short of it and rises over your car, as in the
  original game, instead of ending up inside the wall with a dark screen ([#37]).
- Car Specialties: in Custom Gauges the tachometer you are trying out is drawn whole again. Its
  scale, needle and dial no longer drift apart from the blue ring, the boost gauge sits right
  against it, and the dark backing fills the frame, as in the console versions ([#40]).
- SUVs in traffic now light both tail lights, as in the original game; before, only the left one
  glowed. One of the Peugeot 206's aftermarket tail lights had the same problem and now lights
  on both sides too ([#39]).
- A Drift no longer sits on its loading screen for good when its original handling data cannot
  be read: it starts with the previous drift handling and says so ("No drift data: previous
  handling"), as it already did when memory ran short ([#16]).

## [0.2.1] - 2026-10-30 — Generator fix for Windows

The game is unchanged: the same 0.2.0 executable and the same game data; only the generator changed.
Updating a 0.2.0 output folder with this generator (same ISO) takes seconds: no conversion
step runs again.

### Fixed

- Generator: on some Windows computers the conversion stopped before its first step, with
  `OSError: [Errno 22] Invalid argument` while writing `.tools/browser-generator.log`. That log
  is only a copy of what the page shows, so the generator now reports the problem once and
  carries on with the conversion.

## [0.2.0] - 2026-10-30 — PSP-1000 support and the first tester fixes

A new game executable and new converted game data: update your 0.1.1/0.1.2 output folder with
the 0.2.0 generator (same ISO) or generate into a new folder, and install the complete result.

### Added

- PSP-1000 support, in testing: on a console without extra memory (a PSP-1000, or a later
  model with extended memory turned off) the game picks a compact memory profile by itself.
  It has been tested on a PSP-3000 limited to the PSP-1000's memory and in an emulator set
  to the PSP-1000, not yet on a physical PSP-1000.
- The game's version on the title screen, above the debug lines, in the XMB's game
  information and in bug files (`bug-N.n2bug`).
- The debug overlay shows memory in use and the memory profile's budget next to the frame rate.

### Changed

- Drift handling with Stability Control Off follows the original game's rotation limit more closely.
- Traffic cars knocked backwards in a collision now stop instead of driving away in reverse.

### Fixed

- Stability Control On now changes the handling as in the original game, Drift included: more
  rear grip when the car slides and a cap on how fast it rotates (and slightly less tyre grip
  outside Drag and Drift). Rivals keep their own handling. ([#12])
- The car lights on the asphalt are no longer a solid white patch in PPSSPP's hardware
  renderers at higher resolutions. ([#13])
- Manual transmission from Options > Player now applies in free roam and in career races,
  not only in Quick Race; Rachel's 350Z no longer shifts by itself. ([#14])
- The car no longer gets stuck on the steep ramp around the island at Hotel Plaza, which
  behaved like an invisible wall. ([#15])
- A sponsor's events no longer disappear from the map and the city after reloading a save
  (EUROLITE Drift events, for example). The one-time freeze on the loading screen in the same
  report was not reproduced; if a load stops advancing for 40 seconds, the loading screen now
  shows a panel of technical details: please photograph it for your report. ([#16])
- The minimap returns to the city after a Street X event instead of keeping the race's map. ([#17])
- Career races are offered in the original game's trigger zones around Bayview, not at the
  start line of the course. ([#19])
- No traffic cars appear in career Street X events, as in the original game. ([#18])
- The Mazda MX-5's exhaust tip points the right way, in the garage and on the rivals' cars;
  the same rule now places every car's exhaust tips. ([#20])
- Rachel's 350Z shows its reverse-light lenses, and other car parts painted with the car's
  own skin show it. ([#21])
- The stock Civic has its rear spoiler again, as do other cars whose stock spoiler is a separate
  part, including the rivals' stock Eclipse, Impreza, Lancer, Skyline and Supra. ([#22])
- The title screen keeps quiet while *Riders on the Storm* plays: other buttons no longer make
  menu sounds, and START plays the accept sound as it opens the main menu, as in the
  original. ([#23])
- Generator: the race-line step can no longer run for hours on some computers (reported with
  an Xbox disc on an Intel Core i5-12400); a stuck calculation gives up after 10 minutes and
  keeps the line it already has.
- PSP-1000 memory profile: a career race started right after saving no longer waits about a
  minute on the loading screen.
- PSP-1000: a Drift event right after another race no longer hangs on the loading screen ([#16]).
  When memory is tight, that Drift uses the previous drift handling and a notice says so.

## [0.1.2] - 2026-10-29 — Faster generator, updates and Linux

The game is unchanged: the same RC21 executable as 0.1.0; only the generator changed.

### Added

- Native Linux package for x86-64 (glibc 2.35 or later), including SteamOS on a Steam Deck or
  Steam Machine in Desktop Mode.
- Update instead of regenerating: choose the same ISO and your previous output folder, and only
  the steps whose converter, options or inputs changed are converted again. The result is
  verified like a complete generation.

### Changed

- Faster complete generation: several conversion steps run at once, and the audio, race-line,
  race and screen-map conversions are faster (PS2 on a Steam Deck running Windows: 1 h 41 min
  instead of 3 h 08 min; Apple M5 Pro Mac: 17 min instead of 55 min). The output is identical
  to 0.1.0 on the same computer.

## [0.1.1] - 2026-10-28 — Native generator

The game is unchanged: the same RC21 executable as 0.1.0; only the generator changed.

### Added

- Native generator packages for Windows (64-bit) and macOS (Apple Silicon and Intel), with
  Python, the conversion libraries and the audio/movie tools included: nothing to install.
- Quick Start guide (PDF).
- Xbox-only players get the port's own drawn button icons in the HUD and garage; no PS2 disc is needed.

### Changed

- The garage and vinyl conversions are much faster (about 20× and 6×, with identical output).
- On Windows and Intel Macs some movie, audio, geometry and race-line files are reported as
  *equivalent* to the reference (the same structure, rounded differently by an x86 processor),
  which is a successful result.

## [0.1.0] - 2026-10-27 — Closed Beta 1 (RC21)

The first closed beta: Rachel, the 350Z and the streets of Bayview on a PSP (build
`cdf93ef2c7`, executable SHA-256 `42d252df…`). An extended-memory build for the PSP-2000, PSP-3000 and
PSP Go; PSP-1000 support, all-device support, full-career completion and stable 30 fps
throughout the game were still pending.

### Added

- Kit with the tested RC21 PSP executable, the source-data converters and a local browser
  generator for your own US Xbox or US PS2 (SLUS_210.65) disc image.
- Source-derived suspension and handbrake data, native NOS/PURGE data and Drift handling
  derived from the original game.
- English setup, generation, installation and bug-reporting instructions, with an illustrated
  bug guide.

### Changed

- Quick Race and URL traffic behaviour, reverb-module parsing and menu focus updated since the
  earlier test kits.

### Fixed

- Sponsor-screen and audio-bank ownership problems found in earlier test kits.

[Unreleased]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.3...HEAD
[0.2.3]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/releases/tag/v0.1.0
[#12]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/12
[#13]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/13
[#14]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/14
[#15]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/15
[#16]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/16
[#17]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/17
[#18]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/18
[#19]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/19
[#20]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/20
[#21]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/21
[#22]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/22
[#23]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/23
[#24]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/24
[#25]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/25
[#26]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/26
[#27]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/27
[#28]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/28
[#29]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/29
[#30]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/30
[#31]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/31
[#32]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/32
[#33]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/33
[#34]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/34
[#35]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/35
[#36]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/36
[#37]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/37
[#38]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/38
[#39]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/39
[#40]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/40
[#41]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/41
[#42]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/42
[#43]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/43
[#45]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/45
[#46]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/46
[#47]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/47
[#48]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/48
[#49]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/49
[#50]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/50
[#55]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/55
[#58]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/58
[#57]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/57
[#60]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/60
[#56]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/56
[#59]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/59
