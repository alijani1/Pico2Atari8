# Pico2Atari8 release notes

## 2.1 (in progress)

**Game Settings: each game remembers its own buttons**
- **A Game Settings screen for every game.** Open it from the game list
  (highlight a game and press **Green**) or during a game (**Blue**, then
  the new **gear** key on the on-screen keyboard; the game waits).
- **Map Red, Green, Yellow, the thumbstick click and the Quad's left and
  right side buttons** to Fire, OPTION, SELECT, START, SPACE, RETURN, ESC,
  None, or any key on the keyboard. In a 5200 game: bottom fire, top fire
  or any keypad key. The Duo Ultimate maps its 5-way press instead of the
  click. Blue, START, SELECT, MENU and the sticks stay fixed.
- **Thumbstick: Joystick / Paddle** is now a setting you can see, per game.
  Yellow + click still switches it during play.
- **Machine: Default / 800XL / 400/800** per game, so a game that needs
  the 400/800 always starts on it.
- **Disk drive 1: On / Off after load** replaces the keyboard's DRV key and
  is remembered per game: for the few disk games that won't start while a
  drive answers, the drive switches itself off 1.5 seconds after the game
  stops reading the disk.
- **Paddle center (-113 to +113)** per game: moves where the thumbstick
  paddle rests, for paddle games whose centre is off.
- A game that isn't on its defaults shows **PADDLE MODE** or **CUSTOM KEYS**
  for a moment when it starts. Up to 300 games are remembered; games set to
  use the thumbstick as a paddle in 2.0 keep that setting.
- The Yellow + click message now reads **PADDLE** / **JOYSTICK** (it was cut
  short before).

**Steady full speed**
- **The emulator's per-scanline work now runs from RAM:** the 6502 CPU
  (with its opcode and cycle tables) and ANTIC, the chip that draws every
  line. Run from flash they competed for a 16 KB code cache, so speed
  depended on where code happened to land: unrelated changes dropped some
  games from 60 to 53 fps, and Arkanoid to 41 (its line drawing ran 4x
  slower when it shared cache slots with other hot code). Games now hold
  60 fps however the rest of the code changes.
- **More free memory than 2.0** despite that: the emulator's file-path
  buffers were 1 KB each (about 45 of them); 128 bytes is plenty here.

## 2.0 (31 October 2026)

The first numbered release: Atari 400/800/XL and 5200 emulation (atari800)
on the Quad Ultimate CX / CX-W (from Quad firmware 1.1) and the Duo 5200
Ultimate (from 1.2). The version shows at the top right of the Settings
screen.

### New since the last builds

**Smoother sound, fewer slowdowns**
- **Games no longer slow down or click when they silence a channel with a
  very high tone.** Some games (Clowns and Balloons, for example) mute a
  channel by setting it to an inaudible ~32 kHz tone. The emulator now
  holds such a tone at its average level instead of working through every
  wave. These games run full speed, and a false 9.9 kHz whine that a real
  Atari never made is gone.
- **No stall when a sound starts after a silence.** The first sound after a
  quiet stretch could cost the emulator a long freeze and a click (up to
  338,000 steps in a single frame). This affected most games to some extent.
- **Faster sound for games that write the sound chip rapidly**, such as
  Miner 2049er's level start and digitized effects.
- **The sound stretches instead of clicking when the emulator falls
  behind.** During a heavy moment the sound now plays a little slower, as a
  real machine running slow would, and returns to normal when the emulator
  catches up. Normal-speed play is never affected.

**Quad paddles**
- **Atari paddles on ports 1 and 2** (all four), for Kaboom!, Super Breakout
  and the like. Plugging them in shows PADDLES 1 / 2 IN. Turn each paddle
  fully right once so it learns its end.
- **Settings → Paddle sensitivity:** Very low / Low / Medium (default) /
  High / Very high: how much of the knob's turn covers the game's range.
- **Paddles in 5200 games:** a pair of paddles stands in for that port's
  5200 controller (paddle A = left / right, B = up / down; their buttons
  are the bottom and top fire).
- **Joy-Con as paddle 1:** press **Yellow + click** in a 400/800/XL game to
  switch the Joy-Con between joystick 1 and paddle 1. It's remembered for
  each game.
- **Controller test** shows both paddle pairs (with the knob's position) and
  the Quad's side buttons.

**Other**
- **OS ROM files are found in any folder.** Your own `ATARIOSB.ROM`,
  `ATARIXL.ROM` and `5200.ROM` can now go in any folder, up to three deep.
  Before, they were only found in the root, `ROMS` or `ATARI`. Games that
  need the original OS, such as Slime and Shamus, now find it.
- **Menus:** the Joy-Con needs a firmer push left or right, so the
  selection no longer jumps by accident.

### Also in this release (from earlier builds)

**Games**
- Runs `.atr` / `.xfd` disks, `.xex` / `.com` / `.exe` programs, `.car` /
  `.rom` / `.bin` cartridges (up to 64 KB, read in place) and `.a52` 5200
  games, and zipped games of any of these.
- Starts on an 800XL (64 KB); **SELECT** in the game list starts a game on a
  400/800 instead (four joysticks, OS-B). **Settings → Default machine**
  swaps the two.
- **Atari 5200 games** run on an emulated 5200: the Joy-Con is the analog
  stick and Blue opens the keypad. On the Duo 5200 the real 5200
  controllers work, analog sticks and keypads included.
- **A game that crashes on the XL is restarted on an 800** (with OS-B if the
  file is on the drive). If it crashes there too, the screen says so.
- **Real Atari ROMs (optional):** `ATARIOSB.ROM`, `ATARIXL.ROM` and
  `5200.ROM` on the drive are used instead of the built-in Altirra OS.

**Controls**
- **Quad:** joysticks on ports 1–4, the Joy-Con as joystick 1, Red = fire,
  Yellow = SPACE, Green = OPTION, right side button = RETURN, START /
  SELECT as on the Atari. Hold MENU for 1 second to go back to the game
  list.
- **On-screen keyboard (Blue)** with most of the Atari's keys. The game
  pauses while it's open. Its **DRV** key switches disk drive 1 off and on,
  for disks that won't start while a drive answers (Shamus).
- **Settings:** keyboard behaviour, FPS display, volume, default machine,
  TV system (NTSC / PAL), paddle sensitivity, and the controller test.

**Copying games**
- Pick **ROM Drive (USB)** at the end of the game list. The device appears
  on your computer as a drive; copy games anywhere on it, in folders up to
  three deep.

### Good to know
- **Install:** the Atari 8-bit slot comes with the Quad (1.1 and later) and
  Duo 5200 (1.2 and later) firmware releases. The standalone images are
  `Pico2Atari8-quad-2.0.uf2` and `Pico2Atari8-duo5200-2.0.uf2`.
- **Settings are kept** across updates, except that a settings layout older
  than the paddle-sensitivity release returns to the defaults once.
- **Still to come:** Atari CX-53 Trak-Ball support (5200 Super Breakout
  needs it). Miner 2049er still drops frames for about two seconds when a
  level starts, but no longer clicks.
