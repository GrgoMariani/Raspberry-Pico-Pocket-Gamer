# Raspberry Pico Pocket Gamer

A Raspberry Pico __RP2040__ and __ILI9341__ based pocket console.

[Featured on MagPi Issue #122](https://magpi.raspberrypi.com/articles/pico-pocket-gamer)

> Watch the video by clicking on the image

[![Raspberry Pico Pocket Gamer Video](https://img.youtube.com/vi/271OoiJQUyk/0.jpg)](https://www.youtube.com/watch?v=271OoiJQUyk "Raspberry Pico Pocket Gamer Video")


## Description

Since the Pico came out I wanted to create a project for it that I can show my son to, so I decided to make him a Pocket Console. The game repertoire consists of fifty different games, six of which are rendered with the built in software 3d engine. Eleven of them are educational games for ages 4-7 (Number Train, Count It!, ABC Match, Shape Hunt, Add It Up, Big or Small, Subtract It, Pattern Play, What Time?, Spot the Diff and Wagon Order) and work well in a kindergarten or classroom setting.

## Game list
|               |               |                |
|:-------------:|:-------------:|:--------------:|
| TicTacToe 1P  | Labirinth     | Snake          |
| TicTacToe 2P  | Connect4 1P   | Calculator     |
| Lights Out    | Connect4 2P   | Pong           |
| Math          | 2048          | Breakout       |
| Tetramino     | Paint.NOT     | Q*bert (3d)    |
| Mission Ctrl (3d) | Asteroids (3d) | Frogger (3d) |
| Tunnel Rush (3d) | Missile Def  | PacMan         |
| Invaders      | Turbo Drive (3d) | Minesweeper  |
| Simon         | Flappy Bird   | Memory         |
| Sokoban       | Whack-a-mole  | Doodle Jump    |
| Solitaire     | Pinball       | 15 Puzzle      |
| Flood-It      | Reversi       | Hangman        |
| Bomberman     | Tank Fight    | Dino Run       |
| Number Train  | Count It!     | ABC Match      |
| Shape Hunt    | Add It Up     | Big or Small   |
| Subtract It   | Pattern Play  | What Time?     |
| Spot the Diff | Wagon Order (3d) |             |

## Graphics & gameplay

Every game is drawn with a small set of shared, RP2040-friendly effects
(`src/gpu.c`): twinkling starfields, soft radial glows, 1-bit bitmap sprites,
stateless explosion bursts and HUD progress bars. They are deliberately cheap
(a few dozen to a few hundred pixels each) so the software renderer keeps its
30 fps frame budget. On top of that each game has its own polish - neon glows,
motion trails, drop animations, parallax backgrounds, screen-edge speed streaks
and particle bursts.

Gameplay was deepened across the board, for example: a ghost-piece landing
preview and hard drop in Tetramino, wall kicks and level speed-up; undo in 2048;
falling power-ups in Breakout; a golden bonus fruit in Snake; bonus fruit in
PacMan; a bonus fly in Frogger; shield power-up in Invaders; perfect-wave and
perfect-level bonuses in Asteroids and Q*bert; near-miss bonuses in Tunnel Rush
and Turbo Drive; a landing streak in Mission Control; a session win/loss record
in TicTacToe; and a fork-aware TicTacToe AI. The seven added games (Minesweeper,
Simon, Flappy Bird, Memory, Sokoban, Whack-a-mole, Doodle Jump) all work with
both the switches and the touchscreen.

A later pass focused on readability and feel: Tunnel Rush's obstacle walls are
now filled solid colour (with a bright rim on the edge facing the player) instead
of a hard-to-read wireframe, so the gap is obvious at a glance. Turbo Drive gained
a chargeable nitro boost (UP+DOWN, or the on-screen NITRO button), an overtake
combo, roadside posts and trees for a stronger sense of speed, a horizon sun and
a nitro/speed HUD. Missile Defense's explosions were fixed: the radius used to
oscillate at its peak forever, so blasts never cleared and the next wave could
never start - they now grow, shrink and expire properly, and waves advance.

The newest pass polished the shared visuals and deepened a few more games:

* **Balls look like balls.** A new shared `GPU_DrawBall()` sprite shades every
  ball with a dark rim, a lit body and a specular highlight (Pong, Breakout,
  Connect4, Lights Out), and the 3d engine's ball mesh grew from a chunky
  8-face octahedron into a proper 32-face low-poly sphere. The shared filled
  circle itself was fixed too: it seeded a vertical line and never emitted the
  middle row, leaving a one-pixel horizontal gap through the centre of every
  disc - now every row is solid (regression-tested for radii 1..24).
* **Labyrinth** gained touchscreen steering (sides of the view turn, the middle
  walks), six collectible gems rendered as depth-tested billboards in the 3d
  view, distance fog and a best-time record.
* **Memory** had a timing bug: its logic ran once per main loop iteration instead
  of once per game tick, so a mismatched pair flipped back after a few
  milliseconds and the cursor raced across the board. It now runs on the same
  20 Hz tick as every other game, so mismatches stay visible for a second.
* **Turbo Drive** got more modern traffic: cars indicate and change lanes, and
  drafting behind a car (slipstream) charges the nitro faster and pulls you
  along - draft, then swing out and overtake.
* **Tunnel Rush** controls were mirrored: the ship now rides at the bottom of
  the tunnel and LEFT/RIGHT steer in the direction you see on screen.
* **Breakout** gained multiball and extra-life power-ups, three player lives,
  and endless levels with harder brick patterns instead of a single board.
* **Calculator** became a scientific calculator: a second keypad page (sqrt,
  x², x^y, 1/x, sin/cos/tan, ln/log, pi, e, %, +/-, backspace), a decimal point,
  a DEG/RAD switch and proper error handling - all formatted without printf
  float support, which the RP2040 SDK does not carry.
* **Solitaire** (Klondike, draw one) joined the menu: full rules, undo, an
  AUTO button that sweeps cards into the foundations, move counter, timer and
  best time. It works with the touchscreen (tap card, tap destination, tap a
  selection again to send it home) and with the switches (LEFT/RIGHT pick a
  pile, UP the card, DOWN selects/places). It was then made much easier to
  learn: cards carry real suit symbols (red hearts/diamonds, black
  spades/clubs) with a big pip in the centre, the stock shows how many cards
  are left and a recycle arrow when empty, every legal destination is outlined
  in green while a card is selected, and a HOW TO PLAY overlay behind a HELP
  button explains the rules on demand (it no longer pops up before the game).
  Any card may start an empty tableau column, not just kings.
* **Pinball** joined the menu: a portrait table with a plunger launch lane,
  three big bumpers, two mini bumpers, a bank of three drop targets (clearing
  it pays a bonus), two lane posts, three top rollovers that raise the score
  multiplier when all lit, and a centre kickout saucer that throws the ball
  back up the table. Two flippers guard the drain and there are three balls
  per game. It is tuned to be forgiving - long flippers over a narrow drain, a
  strong flip impulse, gentle gravity and a few seconds of ball save after
  every launch. The physics runs at 60 Hz with sub-stepped collision so a fast
  ball never tunnels through a wall or flipper. LEFT/RIGHT (or the left/right
  half of the table on touch) work the flippers; DOWN/UP or the LAUNCH button
  charges the plunger and releases the ball.

## Bug hunt & four more games

A systematic pass over every game looked for logic and timing bugs. The most
important class of bug: several games decayed animation/feedback counters once
per **main loop iteration** instead of once per game tick or per unit of real
time. The main loop spins far faster than the 30 fps frame rate, so these
"timers" expired in a few milliseconds and the effect was invisible. Fixed:

* **Math** - the CORRECT/WRONG feedback (and the key highlight) now stays on
  screen for a full second instead of vanishing instantly.
* **Calculator** - the pressed-key highlight is now time based.
* **Connect4 (1P & 2P)** - the stone drop animation is now time based (~300 ms)
  so you actually see the stone fall; the 1P AI also ignores input while it is
  thinking so a stray press cannot leak into your next turn.
* **Simon** - the pad flash on input now lasts a quarter of a second.
* **Sokoban** - the "SOLVED!" banner is shown for ~1.5 s of real time.
* **Doodle Jump** - the game-over screen waits ~0.7 s before a tap restarts, so
  you can read your score instead of instantly restarting by accident.

Other logic fixes:

* **Sokoban** - a blocked push used to leave a phantom undo entry, so pressing
  UNDO afterwards silently decremented the move counter. A blocked move no
  longer creates an undo (regression-tested).
* **Whack-a-mole** - the spawn interval underflowed at score ≥ 2400, so no mole
  ever appeared again; it is now clamped.
* **Frogger** - the car/log arrays were too small, leaving the last road lane
  empty and the last river lane with a single log; both are now fully populated.
* **Tunnel Rush** - crashing no longer wipes the distance part of your score.
* **Breakout** - multiball clones now always fan out to opposite sides.
* **Q*bert** - Coaly no longer parks forever on the bottom row; he hops off and
  a new one spawns, like the arcade.
* **2048** - the best score is kept outside the shared memory (it used to be
  wiped by visiting another game) and the win banner resets on a new game.
* **Mission Control** - the landing streak resets on a new game.
* **Snake** - quick successive turns are buffered (a fast "up then left" used to
  lose the first turn and send you into the wall), and the record is saved as
  soon as you grow, not only on death.
* **Solitaire** - dismissing the HELP overlay no longer leaks the tap into the
  board as an unintended move.
* **GPU** - `GPU_ShadeColor()` now actually brightens for levels > 256 as
  documented, so the lit top/left edge of every bevelled button/tile shows up.

The touch-only games **TicTacToe**, **Lights Out** and **Math** also gained
switch support: the arrows move a cursor and UP+DOWN together acts (the same
convention Whack-a-mole uses), so every game now works with both the switches
and the touchscreen.

Four new games joined the menu, all switch + touch:

* **15 Puzzle** - the classic sliding tiles. Scrambled by random legal moves so
  it is always solvable; arrows slide a tile, tapping a row/column slides the
  whole segment. Move counter, timer and best score.
* **Flood-It** - flood the board from the top-left corner with one colour within
  a move limit. LEFT/RIGHT pick a colour, UP/DOWN flood, or tap the palette.
  Each level gets a little harder.
* **Reversi** - outflank the computer on an 8x8 board. Legal moves are hinted,
  passes are handled, and the AI weighs corners/edges against the trap squares
  next to an empty corner. Session win/loss record.
* **Hangman** - guess the word (with a category hint) before the figure is
  complete. A 50+ word kid-friendly list, an alphabet grid, and a win/loss
  record.

## Bomberman & Tank Fight

Two action games joined the menu, both switch + touch. Because the console only
has four direction switches plus MENU, the action button is **UP+DOWN pressed
together** (the same convention the grid games use); on the touchscreen you steer
by holding a finger on the board and tap the on-screen **BOMB** / **FIRE** button.

* **Bomberman** - a 15x11 maze of solid pillars and blastable blocks, with
  **smooth free movement**: you glide pixel-by-pixel (any position between the
  blocks) and slide around corners, not hop tile-to-tile. Drop bombs that
  explode into a cross of flame after a short fuse; flames destroy blocks,
  chain-detonate other bombs and kill anything caught in them. The enemies are
  **rival bombermen who lay their own bombs** - they hunt you and blast blocks to
  dig through. They are smart about survival: each one only drops a bomb when it
  can path-find to a safe tile before the fuse burns down, then flees along that
  route and centres itself clear of the blast, and it never willingly walks into
  a flame. **Friendly fire is on**, so rivals can still catch each other (and
  occasionally themselves) in a blast. You are hurt **only by flame**, never by
  touching them. Blocks hide a varied set of power-ups: extra bomb (B), bigger
  flame (F), speed (S), a one-hit shield vest (V), an extra life (+) and piercing
  flames that run through blocks (P). **Eliminate every rival to clear the
  level.** Three lives, rising enemy count/speed, score and best score.
* **Tank Fight** - a Battle City-style arena of destructible brick and solid
  steel. Drive your tank, shoot the enemy tanks that spawn in waves, and use the
  bricks as cover (your shots break them, steel stops everything). **Each enemy
  colour has its own AI**: the red *hunter* is fast and chases hard, firing the
  moment it has a clear shot; the magenta *ambusher* keeps its distance and
  snaps onto an open line for accurate aimed shots; the orange *brute* is slow
  but sprays forward constantly, chewing through brick walls to open a path.
  Clear every wave to advance; three lives, score and best score.

Both games run on a fixed 30 Hz tick with a catch-up clamp, and both were found
to share a subtle bug during testing: they read only the *held* keys while
playing, so the key-*press* latch piled up and, on game over, a stale press
instantly restarted the game. The latch is now drained every tick.

## Educational games for ages 4-7

Eleven games target preschool and early-school kids and are built for an educational environment:
big touch targets, bright pictures instead of reading, gentle difficulty ramps,
count-along feedback, stars for every success - and **no timer, no lives and no
game over**. A wrong answer just flashes the card and says TRY AGAIN, the same
question stays, so kids can explore without frustration. All of them work with
the touchscreen (tap a card) and with the switches (LEFT/RIGHT to move the
yellow cursor, UP to pick).

* **Count It!** - count the apples, balloons or stars (1..3 at first, then up
  to 10) and tap the matching big number card.
* **ABC Match** - a big capital letter is shown; first find the same capital,
  later find its small letter ("A" -> "a"), the classic pre-school letter game.
* **Shape Hunt** - a white silhouette (circle, square, triangle, star, heart,
  diamond) or a colour swatch is shown with its name; tap the matching card.
  The pictures make it playable before kids can read.
* **Add It Up** - two groups of apples with a big plus sign: count them
  together and tap the total. Sums start within 5 and grow to 10, the pictures
  let kids count their way to the answer.
* **Number Train** - the locomotive game, rebuilt as a counting game (below).

The first five are aimed at 5-year-olds; the others stretch the same ideas
to ages 5-7 (Spot the Diff already works for 4-year-olds):

* **Big or Small** - two number cards with counting dots: tap the bigger (or
  smaller) number, as the question says. Within 10 at first, then up to 20.
* **Subtract It** - a group of apples with some crossed out: tap how many are
  left over. Everything stays within 10, the pictures carry the meaning.
* **Pattern Play** - a row of coloured shapes repeats A B A B, A A B B, A B B
  or A B C; tap the piece that comes next. Two-element patterns first, three
  later.
* **What Time?** - an analog clock shows an o'clock time (half past joins in
  later); tap the matching digital time. Short hand = hour, long hand =
  minutes.
* **Spot the Diff** (ages 4-7) - two pictures look almost the same, but a
  colour, a shape, a size or a whole object differs. Tap a difference in
  either picture to circle it in both; one difference at first, then two and
  three. After three wrong taps a gentle hint pulses around one of the
  remaining differences.
* **Wagon Order** - the second train game, a fully 3d puzzle train: stop at
  each station and solve one of eight rotating puzzles (counting, addition,
  subtraction, patterns, odd-one-out, missing numbers, bigger/smaller, letters)
  to load a cargo wagon and deliver (below).

Every correct answer earns a star with a burst and a rotating GREAT!/WELL
DONE!/SUPER!/YES!, and the best "in a row" streak is kept. The difficulty
follows the number of answered questions, so a session naturally grows with
the child. The host test suite taps every card of every quiz game to prove
that a star is earned, that wrong taps keep the question, and that no input
can ever end the game.

The two endless runners were also made **slightly harder** in the same pass:
Flappy Bird's gap shrank from 74 to 66 px, the pipes move at 2.6 instead of
2.3 px/tick and sit 142 px apart instead of 150. Dino Run starts at 5.0 instead
of 4.6 px/tick, ramps 0.55 per hundred points (cap 9.6), the obstacle gaps are
a little tighter and the pterodactyls appear from score 100 at a 50% share
instead of 150/40%. The minimum gap still leaves room to land and jump again
at top speed.

## Wagon Order (the fully 3d puzzle train)

The console's second train game is a **fully 3d** educational game for ages
5-7 built around **solving a puzzle at every station**. The whole world is
rendered with the software 3d engine - the ground plane, both tracks with
their sleepers and rails, the trees, telegraph poles, mountains, the stations,
the coal lumps, the coal tender and the detailed steam locomotive - under an
orthographic quarter view, with only the sky as a painted backdrop. The loco
has rotating spoked wheels, a moving coupling rod and a crosshead rod that
follows the wheel pin. The far siding is scenery and **scrolls slower than the
main line** for a proper parallax depth cue.

The loop: **drive RIGHT to a station and stop in its green zone**. Friends
wait on the platform (little 3d people who wave as you arrive); solving the
station's puzzle loads a cargo wagon of goods (apples, crates or milk cans,
drawn in 3d on the wagon) and the friends board. Fill the train (3-4 wagons)
to deliver, hear a happy whistle toot, and roll on to the next station. The
puzzles are the challenge; driving is the fun in between. Stopping is made
easy for small hands: the green zone is wide, and a **stop assist** glides the
train to a halt right at the station whenever you let go of the throttle
nearby. Eight puzzle types rotate so one session covers a whole early-years
curriculum, and the difficulty ramps with the number solved:

* **COUNT THEM** - count the apples / balloons / stars and tap the number.
* **ADD THEM** - picture addition (two groups of apples, tap the total).
* **TAKE AWAY** - picture subtraction (some apples crossed out, tap the rest).
* **WHAT'S NEXT?** - complete a shape-and-colour pattern (AB, AABB, ABB, ABC).
* **ODD ONE OUT** - tap the one shape that differs from the other three.
* **MISSING NUMBER** - fill the gap in a number sequence (1,3,5,?,9).
* **BIG OR SMALL** - tap the bigger (or smaller) of two numbers.
* **LETTERS** - match a capital, find its small letter, or pick the letter a
  word starts with (_IG -> P).

Every puzzle is touch-first (tap a big answer card) and also works with the
switches (LEFT/RIGHT to move the yellow cursor, UP to choose). There is **no
timer and no fail state**: a wrong answer just flashes and says TRY AGAIN and
the same puzzle stays, so it stays encouraging in a classroom. A correct
answer earns a star with a burst and a rotating GREAT! / WELL DONE! / SUPER! /
YES!.

RIGHT powers the loco, LEFT brakes and then **reverses**, DOWN toots the
whistle. A guidance line and a pulsing off-screen arrow show how far the
station is. **A missed station is never lost**: it stays put and the child can
reverse back to it and still solve its puzzle (a gentle "OOPS! REVERSE BACK"
nudge, no penalty, no skipped station). **Steam & coal** add a driving layer:
the throttle spends steam, coasting builds it back up, a low boiler only slows
the loco (it never stalls), and glowing 3d coal lumps on the track refill the
tender while the coal pile visibly shrinks and grows and a needle gauge shows
the pressure.

The locomotive is the most detailed model of the fleet: riveted boiler with
bands on a cylinder body, smokebox door, chimney with cap, brass steam dome,
sandbox and whistle, running board and handrail, cab with a lit window at
night, flickering firebox glow, cowcatcher, buffer beam, headlight with a
night glow and piston cylinder. The world scrolls through day, sunset and
night palettes, and chimney smoke chuffs faster under throttle and drifts with
the train's wind - also when reversing.

The host logic tests validate every puzzle type (the answer really is the
count / sum / remainder / pattern continuation / odd one / gap / bigger /
letter, choices are distinct, the odd-one-out has exactly one minority, the
word is a real word from the list), then drive closed-loop to a station, open
the puzzle, refuse a wrong answer without penalty, solve it to load a wagon,
fill the train to deliver, miss a station and reverse back to it (it stays
put and still opens), let the stop assist glide to a halt inside the zone,
drain and refill steam, toot the whistle, and run a long drive-stop-solve soak
that keeps delivering and never ends the game. The integration suite also creeps up to a
station, confirms the puzzle panel opens, and taps the answer cards.

## Number Train (the counting locomotive)

The former Train Stop precision-driving game became a counting game for young
kids. The driving stays: hold **RIGHT** to open the throttle, **LEFT** to brake
(touch: right half powers, left half brakes - the keys match the touch halves), and halt the loco with its front
coupler inside the green stop zone at each station - the zone is generous now
(88 px wide, never below 60). The closer to the flag you stop, the more points:
**PERFECT** (±10 px) 300, **GREAT** (±24 px) 150, **GOOD** 75, plus a level
bonus.

The educational part starts at the platform: the station sign shows a big
**number** and exactly that many friends wait on the platform, waving. A panel
opens - TAP TO LOAD THE FRIENDS! - and every tap (or key press) loads one
friend onto the open wagon behind the loco while a big counter counts along
("1 / 2", "2 / 2"...). When the number is reached: WELL DONE!, a steam burst,
bonus points, and the next station is on its way. Extra taps are simply
ignored, so the wagon can never be overfilled. The counting goal grows gently
with the level: 1..3 friends, then 1..5, then up to 10. Sailing past a platform
without stopping costs nothing - a gentle OOPS! NEXT STOP and the journey
continues. There are no lives and no game over.

The locomotive itself kept all its drawing love: spoked drive wheels that
rotate with the distance, a moving coupling rod between them, a maroon boiler
with bands, steam dome, cab with a lit window at night, cowcatcher, headlight
(with a glow after dark), brake sparks at the wheels and chimney smoke that
chuffs faster under throttle and drifts with the wind of your own speed. The
world scrolls through three palettes - day, sunset and night (stars, crescent
moon) - with parallax mountains, hills, trees, clouds, sleepers and a station
building with the big number sign, platform safety line and flag marker. The
HUD shows score, best, stops served, a km/h readout, the level, the distance
to the next marker and a progress bar.

The host test drives it closed-loop: full throttle until the green zone
bracket reaches the braking point, then full brake - the loco must halt inside
the zone, score and open the boarding panel; one tap per friend must close it
within the station's number; and afterwards never braking again must NOT end
the game.

Two test-tooling fixes came with this pass: the shared `countPixels()` helper
compared byte-swapped `C_*` constants against the plain RGB565 that
`GetPixel()` returns, so colour counts were silently always zero (the snake
length check was vacuous) - it now converts, and `make shot` gained a
`MODE=hold` that holds UP for the whole run, handy for driving-game
screenshots.

## Dino Run & the latest polish pass

**Dino Run** joined the menu as the console's first endless runner: jump the
cacti, duck the pterodactyls (they fly at three heights - low ones must be
jumped, middle ones ducked or jumped, high ones run under), and hold DOWN in
the air to drop back down fast. The world scrolls with two layers of parallax
dunes, clouds and pebbles, the speed rises every hundred points, and every 400
points the desert switches between day and night (stars and a crescent moon
included). UP or a tap jumps, DOWN or holding the bottom strip of the
touchscreen ducks. Score, best score and a ~0.7 s score-reading delay before a
restart, like the other runners.

More defects found and fixed in the same pass:

* **Tetramino** - the line-clear flash decayed once per main loop iteration
  instead of per game tick, so it vanished within milliseconds; it now stays
  for about half a second. The piece dealer also became a proper **7-bag
  randomizer** (every shape exactly once per seven pieces - no more I-piece
  droughts), and soft drop scores a point per cell like the modern games.
* **Doodle Jump** - the HEIGHT counter was passed to the centred-text helper
  with x=6, which pushed most of the text off the left edge of the screen; it
  is now drawn left aligned with a drop shadow (regression-tested).
* **GPU** - `Texture_DrawFilledCircle()` still had the one-pixel horizontal
  centre gap that was fixed in the framebuffer circle long ago, and
  `Texture_DrawEmptyCircle()` with radius 0 underflowed its unsigned counter
  and sprayed ~32000 wrapped pixels over the texture; both are fixed and
  regression-tested for radii 0..24. `GPU_DrawSprite()` also clipped wrongly:
  a sprite hanging off the left/top edge vanished completely because its
  negative coordinates were cast to unsigned - the visible part now draws.
* **Snake** - fruits could spawn on the same cell as another fruit (they
  overlapped visually and both were eaten in one step for a silent +4); all
  three fruits now avoid each other.
* **Flappy Bird** - pipes kept moving and scoring for the rest of the tick the
  bird died in, so a pipe passed after the fatal collision still counted.
* **Breakout** - the bricks were four shades of grey; they are now classic
  rainbow rows that dim as they take damage, with cracks on the last hit
  point, and catching a power-up shows what you got (WIDE!, SLOW!, +100,
  MULTIBALL!, +1 LIFE).
* **Simon** - playback speeds up as the sequence grows, like the arcade.
* **Pong** - the paddle flashes on a return, and a rally/speed readout appears
  under the field once the rally heats up.
* **Tooling** - `extras/tests/ppm2png.py` no longer requires Pillow (a pure
  stdlib PNG writer is the fallback), and the calculator's `%0*ld` precision
  is explicitly bounded so `-Wformat-overflow` stays quiet.

## The 3d engine (`src/gpu3d.c`)

A small fixed point (16.16) software renderer, written for the RP2040 which has
no FPU:

* sine/cosine from a 1 KiB flash lookup table (1024 steps per turn),
* perspective and orthographic projection with near plane clipping,
* painter's algorithm depth sorting per object, back face culling,
* flat shading with one directional light plus an ambient term,
* convex polygon fill and affine texture mapping of the 64x64 GPU texture,
* ready made meshes: cube, textured cube, ball (a 32 face low poly sphere),
  pyramid and a horizontal quad.

The main menu shows a rotating cube with the raspberry sprite mapped onto its
faces, Q*bert builds its pyramid out of shaded cubes, Mission Control renders a
3d terrain and the landing module, Asteroids tumbles real 3d rock bodies as
wireframes, Frogger looks at the road and the river through a perspective
camera, Turbo Drive renders the whole road out of 3d quads, and Wagon Order
builds its entire yard - ground, tracks, sleepers, trees, poles, mountains,
stations, cargo wagons, coal, tender and a steam locomotive with rotating
spoked wheels and coupling rods - out of shaded boxes, cylinders and balls.

## Controls

Five switches: `UP`, `DOWN`, `LEFT`, `RIGHT` and `MENU`, plus the touchscreen.

The `MENU` switch is the reset button of the console:

* __one press__ restarts the running game,
* __two presses within 0.4 s__ return to the main menu.

Switches are debounced in software (`src/gameshared.c`), so a bouncing contact
can never be mistaken for a double press.

The main menu can be operated with the touchscreen as well: tap a name to
select it, tap the selected name again to start it.

## Touchscreen

The XPT2046 driver (`src/interface/ili9341_touchscreen.c` + `src/touch.c`)
collects up to eight raw conversions per poll and turns them into one position
with a trimmed mean (the extreme samples are dropped), which removes the single
wild readings a resistive panel produces. The SPI clock was lowered into the
specified range of the XPT2046 and the first conversion after chip select is
discarded because the internal reference needs to settle. `Touch_Update()` in
`src/gameshared.c` additionally filters jitter between polls and reports press
and release edges, which is what the games use for buttons.

The calibration constants live at the top of `src/touch.c`; if the touch feels
off on your panel, adjust `TOUCH_MIN/MAX_RAW_X/Y` there.

## How to compile/run ?

Steps are similar (same) as with any other C++ Pico SDK based project.

```bash
mkdir build
cd build
cmake ..
make -j${nproc}
```

After which you should be able to find `build/PocketGamer.uf2` file to upload to your Pico microcontroller.

> __HINT__: In case you want to try the project without the Pico board `cd` into the `extras` folder and do a `make run`. I've adapted the interface and the main function to run on top of the olc's PixelGameEngine so you can try it directly from your Linux PC. Controls are `QWASD` and mouse click (`Q` is the MENU/reset switch).

### Tests

The very same sources are exercised on the host by a test runner:

```bash
cd extras
make test         # unit tests + a smoke test of every game (1000+ checks)
make test-asan    # the same under AddressSanitizer + UBSan
make shot NAME=qbert FRAMES=120     # render frames of a game to build/qbert.png
```

`make shot` accepts every name from the menu plus `menu`, and an optional
`MODE=idle` to run without simulated input or `MODE=hold` to hold UP for the
whole run. The runner covers the pixel
format, the font rasterizer, line clipping, the touch calibration and filter,
the switch debouncing, the random generator, the 3d math (including the winding
of every built in mesh), the painter's algorithm, the menu button behaviour and
runs all games with pseudo random input.

## Pinout
You should be able to find a compressed __gerber__ file with `extras` directory of the repository and order a new PCB online on which you can solder all the elements easily.

ILI9341 to Pico pin connections looks like this:
 
```
 ILI9341         Pico (RP2040)
 
 T_IRQ  <-----> PIN 20
 T_DO   <-----> PIN 8
 T_DIN  <-----> PIN 11
 T_CS   <-----> PIN 21
 T_CLK  <-----> PIN 10
 SDO    <-----> PIN 4
 LED    <-----> PIN 26 (or directly to 3v3)
 SCK    <-----> PIN 6
 SDI    <-----> PIN 7
 DC     <-----> PIN 18
 RESET  <-----> PIN 17
 CS     <-----> PIN 16
 GND    <-----> GND
 VCC    <-----> 3V3

```
 
 Additionally keyboard switches are added to the following pins:
 
```
                   Pico (RP2040)
 
 KEY_MENU  <-----> PIN 0
 KEY_UP    <-----> PIN 15
 KEY_DOWN  <-----> PIN 27
 KEY_LEFT  <-----> PIN 28
 KEY_RIGHT <-----> PIN 14

```

In case you need to change them they are all defined within their own source files: `src/interface.c`, `src/interface/ili9341.c` and `src/interface/ili9341_touchscreen.c`.

## Code explanation

```mermaid
stateDiagram-v2
    direction LR
    A: DoMainLoop()
    note left of A: Main loop description
    state A {
      GetInputs: Register keyboard and touch events
      [*] --> GetInputs
      GetInputs --> MenuKey
      MenuKey: MENU 1x reset / 2x main menu
      MenuKey --> updateCallback
      updateCallback --> drawCallback
      drawCallback --> GetInputs
    }
    note left of B: SetupCallbacks(Callbacks* callbacks) changes your current game
    B: SetupCallbacks(callbacks)
    state B {
      [*] --> deinitCallback
      deinitCallback --> initCallback
      initCallback --> [*]
    }
```

```mermaid
classDiagram
    class Callbacks{
        void * initCallback()
        void * updateCallback()
        void * drawCallback()
        void * deinitCallback()
    }
```

> Should I add more detailed info here?
