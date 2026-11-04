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

## [0.2.6] - 2026-11-04 — Rush Hour at the Rail Yard

EA Trax: *Rush Hour*, Christopher Lawrence ([official channel](https://www.youtube.com/djcltv)).

The rail yard's drags are yours to steer again, finished racers finally hit the brakes, Rachel's
magazine shoots wait for you to stop and say yes, and her 350Z rolls out with the aero kit it
always had.

A new game executable and new converted game data: update your 0.2.0 to 0.2.5 output folder
with the 0.2.6 generator (same ISO) or generate into a new folder, and install the complete
result. Updating from 0.2.5 converts 71 of the 81 steps again, so it takes nearly as long as a
complete generation.

### Fixed

- The body shop's parts now come in the original game's order, from the ones you can buy
  early in your career up to the ones that unlock last. Before, bumpers, side skirts, hoods,
  spoilers, lights, roof scoops and exhaust tips were listed by their model number, so a
  level 3 kit such as Wolfpack sat right after Snyper and Ace, ahead of Rohan and Orion. The
  parts already on your cars stay as they are ([#77]).
- Speedometer Units now switches every distance in the game, as in the original, not only the
  speedometer. With KM/H chosen, the gap to your rival in an outrun reads in meters, and you
  win it 300 m ahead; race lengths on the Quick Race track list, the career's event dialog and
  the World Map read in km; and the dyno and the performance parts show torque in Nm and power
  in kW. Each unit uses your language's own word ("Metros", "Mètres"...). With MPH nothing
  changes: feet, miles, ft-lbs and bhp. On a new memory stick, the game starts in KM/H when it
  plays in French, German, Italian, Spanish or Dutch, as the original does ([#79]).
- Menu sounds stay clear of the street's reverb, preventing delayed echoes
  while navigating the pause menu ([#109]).

- Accelerator and brake controls no longer remain blocked after saving a profile
  and entering Career while a button is held ([#100]).

- Rear light lenses keep their original transparent artwork and are no longer
  treated as headlights in garage previews ([#108]).

- The Civic's trunk lid uses its original hinge and rotation keys, correcting
  its position when previewing trunk accessories ([#108]).

- Garage preview doors, hood and trunk close when returning to driving, including
  after leaving the parts menu quickly. The Hydraulics category displays its own
  icon in the customization carousel ([#108]).
- The speedometer's unit label now follows changes to Speedometer Units while
  driving, so selecting KM/H no longer leaves MPH beside the converted speed
  ([#104]).
- GPS guidance keeps following your road when streets overlap at different heights,
  instead of switching to the road above or below ([#106]).

- The Save, Start New Career and Jump to Garage menu labels, and the career
  summary below the car, use the selected language's original disc text ([#99]).

- Special-event stars and active photo-shoot destinations appear on the World Map.
  The minimap keeps the selected star visible while GPS guidance is active ([#97]).

- Adjusted the quiet-sound filter for player contacts with breakable objects, including
  the green house's glass doors. Glass volume now follows the original PS2 collision mix;
  confirmation of the reported Corolla/Career case is pending ([#98]).
- Drag: after the finish, your car rechecks its path against stopped rivals and
  nearby traffic, using the original lane-change restrictions before choosing
  another lane instead of turning toward the side of the road ([#94]).
- Race participants brake to a stop after finishing instead of continuing around the course.
  Finished rivals stay stopped while you complete your race. After a city race, choosing
  Explore returns your car to the race line instead of leaving it wherever it rolled to
  during the results ([#92]).
- The driver silhouette uses the cabin's shading instead of standing out as a dark cutout
  through the windows, and is hidden when viewing the car in menus ([#93]).
- Sprint starts with two rows of cars keep the positions shown in the intro,
  instead of moving the rear cars into the spectators during the countdown ([#91]).
- Drift races use the original HUD. The standings show your own total beside the
  rivals' totals and alternate with the drivers' names. A 30-second Hurry countdown
  starts when the first rival finishes and ends the race when it expires ([#90]).
- Pressing the accelerator during the garage exit animation no longer launches the car
  into the street at extreme speed; it accelerates normally after the skip ([#95]).
- Special events, such as Rachel's Magazine Cover Opportunity, no longer start on their own as
  you drive by. Stop at the event's marker, now the yellow special-event icon as in the original
  game, and press Up to read her message and accept or decline it ([#124]).
- Reaching the photographer stops the special event's clock, as in the original game, and the
  event now stays on if you drive past him: turn back, stop at his marker and press Up for the
  photo shoot. The clock now sits where your Bank cash is shown, clear of the GPS arrow ([#125]).
- While a special event is on, its destination is the only place on the street, the minimap
  and the World Map, as in the original game: races, outruns, shops, tips and cash pickups wait until
  the photo is taken or the event runs out of time ([#126]).
- Sprints now register the finish line after you leave the course and join it further
  along, for example through a side street in the Beacon Hill sprint. Before, the race lost
  track of you until you drove back to the spot where you left it: the progress counter
  stopped, the wrong-way sign kept flashing on the right road, and crossing the line did
  nothing. Now the race finds you again when you rejoin the course, using the original
  game's rule: a short detour counts, but a cut that skips much more road than you drive is
  still not followed ([#85]).

- On a new career, once Rachel calls, the minimap shows the prologue's Circuit and Sprint
  while you drive her 350Z, as in the original game, instead of the Car Lot alone. The secret
  circuit in El Norte now stays off the minimap and the World Map until you find it, and until
  you pick your own car the World Map lists only Circuit, Sprint and Car Lot ([#111]).

- The Body Shop's Carbon Fiber menu now sells only the carbon trunk and the carbon body kit,
  as in the original game, and they open in stage 5 (after 8 and 14 race wins) instead of
  being on sale from the start. The carbon doors and the carbon hood of that menu, which the
  original never sold there, are gone; if you already fitted one, it stays listed until you
  take it off ([#112]).

- You can now engage races, shops and your garage while driving through their markers,
  at any speed, as on the PS2: press Up while the event icon shows. Before, the car had to
  be nearly stopped, so the icon vanished as soon as it rolled ([#121]).

- Bayview's street chimes are back, from the original sound bank: the event icon's chime
  when you drive into a race, shop or garage marker, the new-message chime when an "i" tip
  sends you an SMS, the bank cash pickup sound, and the chime of the messages that open on
  their own. As in the original, only the career's first tip opens its SMS by itself; the
  other "i" tips go to your mailbox, with the envelope on the HUD. The menus now use the
  original's sound for each button too: up, down, left, right, accept, back and Start each
  have their own click, where before most of them shared the wrong one ([#120]).

- Drag races in the rail yard (Coastal Express, Switching Yard and Central Station) play like
  the original. Your car stays in its lane until you change it, so dodging the trains is up to you
  again, with no more lane changes you did not ask for. A tap of left or right moves exactly one
  lane. Holding the button only goes as far as the open lanes allow, and the car never cuts across
  a wall or heads for a lane a train has closed. The yard's fences, crates, barrels and signs fly
  aside when you hit them instead of letting you drive through. Traffic only shows up where the
  original puts it, in the second half of the yard, never over the first half of the strip ([#110]).

- Browsing parts and paints in Customize Car no longer drags the frame rate down to 9 fps.
  Each step through a list rebuilt the whole car from the Memory Stick. Now the car on show
  keeps what it already has loaded and reads only the new part, and while you hold the D-pad
  to scroll, the car updates as soon as you stop on a part ([#114]).


- After a closed-course career race, Return to Explore Mode restores your position
  and direction on the city street. Race transitions now stop safely if unloading
  or loading cannot complete, instead of continuing into an invalid world ([#92]).

- Rachel's 350Z now carries the aero kit of its preset, as in the original game: more downforce,
  so it turns harder in fast corners, closer to the PS2 at 110 to 140 km/h (70 to 90 mph) ([#45]).
- Pressing the accelerator while reversing now applies the brakes before driving
  forward. Holding the handbrake and accelerator together no longer cuts drive
  power ([#87]).
- Performance parts now give the power they give in the original game. Engine, ECU and turbo
  packages used to add only a sliver of torque (the turbo's boost never reached the wheels), and
  the ECU's higher revs and the transmission's own gearbox never made it into the car. Now each
  package builds the engine and gearbox the way the console versions do, for your car and for
  the rivals matched to it. Rachel's 350Z, flat out down Airport Road, now gets from 70 to
  166 km/h (45 to 103 mph) in 6.6 s, against 150 km/h (93 mph) before and 176 km/h (109 mph) on
  the PS2, and shifts later and quicker. In her car the rev counter now follows her preset
  engine, not a stock 350Z's ([#88]).
- Glancing off a wall feels like the original again. Brush a barrier at a shallow angle, on a
  straight or round a bend like the freeway tunnels', and the car now slides along it, losing a
  little speed, instead of being shoved square off the wall or stopped dead. Three things added
  up: where the road ends at a barrier's foot (raised tunnels, viaducts), the collision map
  stepped out in front of the barrier in one-metre blocks, and the corner of a block met head on
  counted as a crash; a car leaning on a wall lost 1.5% of its speed every frame it touched it,
  a quarter of its speed in a quarter of a second; and a touch that wasn't a crash still took
  the steering away for a second. On the PS2's own walls the slide now matches the original
  within a few mph ([#83]).
- Car Specialties and the other shops: your car keeps its paint while you try parts on it. On a
  PSP-1000, trying some headlights (EYELIDS on the Civic) could turn its metallic blue a lighter
  blue and show the stock lights, then back with the next part: when the console had no room
  for the car with that part, the game fell back to the stock car and drew it without your
  paint's finish. Now that car keeps your paint, vinyls and window tint, and the shop tells you
  there is not enough memory to show the parts on it ([#82]).
- The startup movies keep their original 4:3 picture: the EA GAMES "Challenge Everything" logo
  now shows whole, with the "EA GAMES is an ELECTRONIC ARTS brand" line under it, instead of
  zoomed in with its top and bottom cut off. The 4:3 movies (the logos, the opening warning and
  the story scenes with Rachel) play at their own size with black sides, as the PC version
  shows them; the widescreen attract movie still fills the width, as only its own black bars
  are left out. Skipping a movie with START works as before ([#89]).
- The title screen's dark band now comes in with "Press START button", as on the PS2. Before,
  the band sat empty under the logo from the first moment, during the boot's loading too; now
  the space stays clear until the prompt fades in, and the band fades in with it ([#89]).

## [0.2.5] - 2026-10-31 — Crashing Foreign Cars, Fluently

EA Trax: *Crashing Foreign Cars*, Helmet ([Spotify](https://open.spotify.com/artist/0qB0cTENhSUc0feov5qbg0)).

The outrun crews finally learn that Bayview's traffic is solid, the freeway stops hiding a wall
that belongs to the street below, every race gets its chequered lines back, and Rachel texts you
in your own language.

A new game executable and new converted game data: update your 0.2.0 to 0.2.4 output folder
with the 0.2.5 generator (same ISO) or generate into a new folder, and install the complete
result.

### Fixed

- Generator: a game made from the PS2 disc on Windows passes its final check again. The racing
  line through a few shortcuts is smoothed by a calculation that lands a little differently on
  each kind of processor, so one shortcut came out two points shorter than on the developers'
  Mac, and the check stopped the whole folder with "NOT verified" (`pack.n2pk`
  different) although the game itself was fine. The check now confirms that every shortcut is
  the same one, with the same signs, and lets its smoothed line differ by a point or two ([#86]).
- The street racers who challenge you to an outrun in free roam drive like cars again, not like
  ghosts on rails. Before, a rival rode its way across Bayview ignoring everything in it: it
  could run into a civilian car, carry it along merged into its own body and take both of them
  into a wall, slide through walls and the other rival, stop dead whenever you stopped, and
  sometimes spin round on the spot at a dead end and drive off the wrong way. Now it goes round
  a slower car when the road has room, or slows down behind it; when something does hit it near
  you (a traffic car, a wall or your own bumper) it takes the knock like any other car before
  getting back on its way; it keeps cruising at 60 mph or more beside you, as in the original
  game, instead of parking next to you; and it steers clear of dead-end streets ([#72], [#74]).
- Every race now has its chequered start and finish lines painted across the road, as in
  the original game. Run a course the other way in Quick Race and you get that way's own
  lines ([#64]).
- Rachel no longer walks you through every shop again each time you resume your career. Your
  save now remembers when she last talked about each shop screen, as in the original game: she
  explains it once, and only brings it up again after an hour or so of play. A save from an
  earlier version hears each shop's line one last time ([#68]).
- Paint in the Graphics Shop now lists every part the original game lets you paint: Base Paint,
  Spoiler, Roof Scoop, Mirrors, Rims, Spinners, Brakes, Engine Accents and Muffler Tip. The roof
  scoop and the mirrors take the body's Gloss, Metallic and Pearlescent paints; the spinners,
  engine accents and muffler tips have their own colours, and each one counts toward your Visual
  Rating. A part you can't paint yet stays on the list and tells you why when you pick it, as in
  the original: stock parts need an aftermarket part first, carbon fibre and aluminium pieces
  can't be painted, spinner colours need Spinners on the car, and engine accents need level 3
  Engine, ECU and Turbo upgrades. Engine accent colours are kept on your car, but the engine bay
  doesn't show them yet. Your saved cars keep their paint ([#66]).
- No more invisible wall at the edge of a freeway that runs over a street. Where the freeway's
  barrier and the wall of the street 14 m below passed through the same square metre of the
  city's collision map, the two were merged into one block, and a car on the freeway could hit
  the street's wall up to a metre short of the barrier it saw: it stopped dead in the lane
  instead of scraping along. Each level now keeps its own walls. The generator converts the
  city's walls again ([#75]).
- Playing in Spanish, French, German, Italian or Dutch, the button hints no longer slip back
  into English: "Back" in the pause menu and in Pause Options is now "Atrás" in Spanish, and
  the World Map's Full Map, Hide on World Map, Activate GPS and Back follow your language too,
  all with the original game's own words ([#76]).
- Rachel's texts now reach your phone in your language. The whole SMS section was in English
  whatever the PSP's language: the From and Subject lines, every message, the magazine and
  unique parts news, the mailbox list and its Go to Mailboxes and Return to Gameplay hints. They
  now use the original game's translations, the magazine's name filled in as the original does
  ([#78]).
- The race offer you get when you drive into a race marker now reads as in the original
  game, in your language: the text comes from one of the racers, the subject names the
  kind of race (Sponsored Sprint, URL...), and the message is the trash talk the original
  picks for that kind of race, or the prologue racers' own lines when Rachel's 350Z is
  still the car you drive ([#80]).

## [0.2.4] - 2026-10-31 — Give It All to Bayview

EA Trax: *Give it All*, Rise Against ([official channel](https://www.youtube.com/user/RiseAgainst)).

Fast, loud and in a hurry, like a release that ships everything that was ready: your garage stops
pretending to be a dealership, your own builds roll into Quick Race, the sponsors get their real
contract screen back, the "i" markers finally pay, and Xbox players can verify their games again.

A new game executable and new converted game data: update your 0.2.0 to 0.2.3 output folder
with the 0.2.4 generator (same ISO) or generate into a new folder, and install the complete
result. Players of the Xbox disc: this generator also fixes the final check that failed every
Xbox generation in 0.2.3, so updating an Xbox folder made by 0.2.2 or 0.2.3 now works.

### Added

- If the game stops responding for 2 seconds outside a loading screen, it now shows a panel
  with what it was waiting for. Please take a photo of it for your report: it tells us where
  the game got stuck ([#44]).

### Fixed

- Generator: a game made from the Xbox disc passes its final check again. In 0.2.3 the
  reference the generator checks against had been built with the PS2 controller icons
  (Cross, Circle, L, R, START) in the menus, which only the developers' machine had, so a
  complete Xbox generation, new or updated from 0.2.2, ended with "NOT verified" (`parts.n2ui`
  and `pack.n2pk` different) although the game itself was fine. Your prompts keep the icons
  drawn for this port, and an Xbox folder made by 0.2.3 is now recognized as such when you
  update it.
- Your garage is a garage again, not a dealership. In the career, its menu no longer lets you
  buy cars, parts, paint or performance upgrades: as in the original game, those are sold at
  the Car Lot and at Bayview's body, performance, graphics and car specialties shops, so you
  have to drive out and find them. The garage keeps Resume Career, Save and Main Menu, and
  now offers Car Select, to switch between the cars you already own for free, and Performance
  Tuning / Dyno, to tune the parts already fitted ([#67]).
- Quick Race: the cars you build in Customize Car are now in the car list, ready to race, as in
  the original game. All Cars shows the stock cars followed by your My Cars, and the list opens
  on the car you last customized or raced, so its paint, parts and performance go into the race
  with it. Before, All Cars held only the stock cars and your tuned ride sat behind the My Cars
  tab, so picking it from the list put you in a stock car. L and R switch between All Cars,
  Stock Cars and My Cars, in the original's order ([#61]).
- Sponsorships: the screen where you sign a sponsor at the start of a stage now looks and works
  like the original. Left and Right flip from one sponsor to the next, with its name in the bar
  and its logo on the left; the contract terms fill their own panel (Up and Down scroll them),
  and the signing bonus, each URL victory, the sponsor race purse and the sponsor's races sit
  in their boxes on the right. Before, the sponsors were a vertical list with the details
  written over the panels in the wrong places ([#63]).
- Dawn no longer turns the whole city pink. The red sunrise that suits the
  airport now follows each neighbourhood's own morning light: on the Resort
  Loop's grid the sky, the asphalt and the silver cars keep the grey dawn they
  have on the PS2.
- Rachel's crew calls at the start of the prologue now come again each time you resume the
  career while you're still in her 350Z, as in the original game, and no longer reach you once
  you've driven off the Car Lot in your own ride. The tips about races in other neighbourhoods
  work as in the original again: cross into another neighbourhood at least 5 minutes after the
  last tip, and a friend tells you where one of the hidden races is waiting ([#57]).
- The World Map in the pause now shows each place in its own colour, matching the minimap and
  the original game: the Circuit races in violet and your garage in purple, as on Rachel's first
  drive in the 350Z. Before, most rows borrowed another kind's icon, so the garage came up yellow,
  the Circuit races orange, and Drag, Street X, U.R.L. and the shops traded colours too ([#53]).
- The "i" info markers on the street now pay out, as in the original game: the first time you
  drive through one, its message from Rachel comes with cash in your bank, $25 for most of them
  and $50 for a few. Before, the message came but the money never did. Each marker pays once; the
  ones you already drove through in an earlier version keep their message but don't pay again
  ([#62]).

## [0.2.3] - 2026-10-31 — Rocket Ride

A new game executable and new converted game data: generate into a new, empty folder with the
0.2.3 generator and install the complete result (updating a 0.2.2 folder fails its final check in
this release; the generator then keeps the old folder as it was). The cars and their rims, brakes and mirrors, the neon underglow, the shops'
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

[Unreleased]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.6...HEAD
[0.2.6]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.5...v0.2.6
[0.2.4]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.3...v0.2.4
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
[#61]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/61
[#66]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/66
[#58]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/58
[#53]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/53
[#57]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/57
[#60]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/60
[#56]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/56
[#59]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/59
[#44]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/44
[#62]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/62
[#63]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/63
[#67]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/67
[#68]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/68
[#64]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/64
[#72]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/72
[#74]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/74
[#75]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/75
[#76]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/76
[#77]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/77
[#78]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/78
[#79]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/79
[#80]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/80
[#82]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/82
[#83]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/83
[#85]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/85
[#87]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/87
[#88]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/88
[#89]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/89
[0.2.5]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/compare/v0.2.4...v0.2.5
[#86]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/86
[#95]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/95
[#90]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/90
[#91]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/91
[#93]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/93
[#106]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/106
[#114]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/114

[#92]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/92
[#94]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/94
[#97]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/97
[#98]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/98
[#99]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/99
[#108]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/108
[#104]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/104
[#100]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/100
[#106]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/106
[#109]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/109

[#94]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/94

[#124]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/124
[#125]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/125
[#126]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/126
[#111]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/111
[#112]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/112
[#120]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/120

[#121]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/121
[#110]: https://github.com/jussimir-labs/pessoal-nfsu2-psp-beta/issues/110
