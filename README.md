# Pico2Atari8

**Atari 8-bit computer games on the Raspberry Pi Pico 2 / RP2350**, such as
[iCode devices](https://www.icode.com) — the Atari 400/800/XL/XE and the Atari 5200,
emulated by [atari800](https://github.com/atari800/atari800) (the atari800
development team) with [AltirraOS](https://www.virtualdub.org/altirra.html)
(Avery Lee) built in, played with your
original Atari joysticks and paddles.

Version **2.0**, for the Quad Ultimate CX / CX-W and the Duo 5200 Ultimate —
see [docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md).

## What it runs

- **Executables** — `.xex` `.com` `.exe`
- **Cartridges** — `.car` (any type), `.rom` / `.bin` (standard 2–16 KB and
  XEGS sizes)
- **Atari 5200 cartridges** — `.a52`, `.bin` / `.rom` that look like 5200
  ones, and `.car` of a 5200 type (4, 8, 16 one- and two-chip, 32 and
  40 KB, and the homebrew Super Carts); see [Atari 5200](#atari-5200)
- **Disk images** — `.atr` `.xfd` (read-only)
- **Zip files** holding any of those (one game per zip). The game is
  unpacked into a reserved flash area when it starts (about a second for a
  90 KB disk) and runs from there; see [docs/FLASH_LAYOUT.md](docs/FLASH_LAYOUT.md).
  The arcade's game zips on the same drive are left out of the list: a zip
  shows only if it holds an `.xex` `.com` `.exe` `.car` `.atr` `.xfd`, or a
  `.bin` / `.rom` as its only file (notes like `.txt` aside).

The machine is an 800XL with 64 KB, NTSC, BASIC off, with atari800's
vibrant palette. The Atari operating
system is **AltirraOS** (Avery Lee's free replacement, built in), so no
Atari ROM files are needed.

**Real Atari ROMs (optional).** Some early games (Synapse's Slime and
Shamus, for example) jump straight into the original Atari OS and crash on
AltirraOS. Copy your own `ATARIOSB.ROM` (400/800 OS-B, 10 KB) and
`ATARIXL.ROM` (XL/XE OS, 16 KB) onto the drive (any folder, up to three
deep: the root, `ROMS` and `ATARI` are looked in first) and they're used
instead. When a game crashes the emulated 6502 on the XL, it
is restarted on an 800 (OS-B if the file is there); files named with
`-osb` or `(OS-B)` start on the 800 straight away. If it crashes there too,
the screen says so and MENU goes back to the list.

**Keyboard.** Tap MENU (or press Blue, its default) in a game and an on-screen
keyboard with most of the Atari's keys (0-9, A-Z, ; , . / - =, ESC, DEL,
SPACE, RETURN, and RST: the Atari's RESET key) opens over the bottom of the picture, and the game pauses.
The stick (or Joy-Con) moves the cursor; fire types the key: the game runs
while fire holds it down (and a few frames after), then pauses again. A
tap of MENU (or Blue) closes the keyboard and the game carries on.

**Disk drive off.** Some disk games refuse to start while a drive answers
(this Shamus disk reboots on START if any drive replies to a status
request). The DRV key on the on-screen keyboard switches disk drive 1 off
and on, like the switch on a real drive: let the game load, type DRV
("DRIVE OFF"), then START. (2.1: Game Settings > Disk drive 1 instead.)

**Settings** (a row at the end of the game list; saved in flash):

| Setting | Choices |
|---|---|
| Keyboard | Pause+Auto Resume (default): the game pauses, the first key typed is sent and closes the keyboard / Auto-pause: paused while open, each key runs the game briefly, Blue closes / Keep running |
| FPS display | Off (default) / On |
| Volume | 0-100 % (shared with the Quad) |
| Default machine | 800XL (default) / 400/800 - SELECT in the list starts the other |
| TV system | NTSC (default) / PAL |
| Paddle sensitivity | (Quad) how much of a paddle's turn spans the game's whole range: Very low = all of it, Low 75 %, Medium 50 % (default, about what Atari games expect), High 40 %, Very high 30 % |
| Scale | the picture. 320x240 (Quad, Duo 5200): Normal (1:1) / TV (240 wide, centred: Atari pixels as wide as on a TV, softened) / TV sharp (the same, unblended) / Pixel (160 wide: one screen pixel per Atari pixel; hi-res text loses detail) / Stretch (the 192-line playfield over all 240 lines; the default). 160x80 (Duo): Smooth / Sharp / Drop / Crop. Per game: Game Settings > Scale; in play: a button mapped to Next scale (wraps) |
| Controller test | every control live: the 5200 controllers' sticks (as the pot value the game gets, plus the raw counts and the range learnt), Fire A / B and keys; the Quad's joystick ports, paddles on ports 1–2 (pot value, raw counts, range, buttons) and side buttons; the Joy-Con; the device buttons. MENU goes back |

## Game Settings

Each game can have its own button setup. Open **Game Settings** from the
game list (highlight the game, press **Green**) or during a game (tap
**MENU** for the on-screen keyboard, then the **gear** key, which replaced
DRV; the game waits). Changes are remembered for that game (up to 300 games).

| Row | Choices |
|---|---|
| Red, Green, Yellow, Blue | left / right: Fire, OPTION, SELECT, START, SPACE, RETURN, ESC, Keyboard, Next scale (160x80 panel), Stick 2 Up / Down / Left / Right, None; FIRE: pick any key from a keyboard. A 5200 game: Bottom fire, Top fire, `*`, `#`, START, PAUSE, RESET, Keyboard, Stick 2 ..., None, or any keypad key. Blue is Keyboard by default. Stick 2: joystick 2's directions (a 5200: controller 2's stick at its edges) - four buttons as Robotron's firing stick |
| Thumbstick click (Duo: 5-way press) | as above |
| Left / Right side button (Quad) | as above |
| Thumbstick | Joystick / Paddle (400/800/XL games; Yellow + click in a game switches it too) |
| Paddle center | -113 to +113 (400/800/XL games): where the thumbstick paddle rests, for games whose playfield centre isn't the paddle's middle; + = to the right. Left / right: 1 a step (5 when held); FIRE: back to 0 |
| Horizontal center | -113 to +113 (5200 games): the same for controller 1's analog stick x (thumbstick or a 5200 controller), at rest too, for 5200 games whose centre is off; + = to the right |
| Scale | Default / the screen's modes (Settings > Scale: Normal, TV, TV sharp, Pixel, Stretch on 320x240; Smooth, Sharp, Drop, Crop on 160x80): this game's picture Scale; Default = Settings > Scale. A button mapped to Next scale steps through them in play, and wraps |
| Machine | Default / 800XL / 400/800 (400/800/XL games): the machine this game starts on; Default = Settings > Default machine. SELECT in the list still starts the other one |
| Disk drive 1 | On / Off after load (disk games and zips): for the few disk games that won't start while a drive answers - the drive switches off by itself 1.5 s after the game stops reading the disk (DRIVE OFF shows). Changed during a game, it applies at once. Replaces the keyboard's DRV key |
| Reset to defaults | the buttons and the thumbstick back to the table below |

START, SELECT, MENU (tap: keyboard, hold: the game list) and the sticks always stay as they are. Values
that differ from the defaults show in orange. A game that isn't on its
defaults shows **PADDLE MODE** or **CUSTOM KEYS** for a moment when it
starts. Red and the click always type on the on-screen keyboard, whatever
they're mapped to.

## Atari 5200

A 5200 game starts on an emulated 5200 (16 KB, NTSC) on either device. Its
BIOS is the built-in Altirra 5200 OS; copy your own `5200.ROM` (2 KB) onto
the drive (root or `ATARI` folder) and it's used instead. The list labels
5200 games **5200**, zipped ones too.

A raw `.bin` / `.rom` carries no type, so it's told by its last bytes (an
800 cartridge ends in its run address, a `$00` "cartridge here" byte and an
option byte; a 5200 one in its title, year and start address). One that
matches neither is a 5200 game on the Duo 5200 and an 800 one on the Quad.
A 16 KB 5200 cartridge comes as one chip or (more often) two; which is
guessed from where its code jumps to. Rename a game `.a52` to make sure.

| Control | In a 5200 game |
|---|---|
| Joy-Con stick | controller 1's analog stick (the stick's position, not just its direction) |
| Red / Joy-Con click | bottom fire |
| Green | top fire |
| START / SELECT | START / PAUSE |
| Yellow | nothing (Game Settings can make it `*` or any key) |
| Blue | the controller keypad on screen: 0-9 `*` `#` START PAUSE RESET (as the keyboard: the game pauses, fire presses a key) |
| MENU | hold 1 s: back to the game list |
| 5200 controllers (Duo) | controllers 1 and 2: analog stick, Fire A = bottom, Fire B = top, keypad 0-9 `*` `#` START PAUSE RESET. The Joy-Con stands in for controller 1's stick while it's moved. In a 400/800/XL game they're joysticks (Fire A or B = fire, keypad digits = number keys, START = START) |
| Paddles (Quad, ports 1–2) | a pair stands in for that port's 5200 controller: paddle A = the stick's x, paddle B = its y (turned right = right / down); A's button = bottom fire, B's = top fire |

## Quad Ultimate CX

From Quad firmware 1.1 (CX and CX-W), Pico2Atari8 has its own slot beside
Pico2Arcade, comes with the Quad release, and shares its ARCADE ROMS drive.

1. To flash just this image: `pio run -e quad -t upload` (writes only the
   Atari 8-bit slot at 0x210000; the Quad firmware, the arcade and the ROM
   drive are left alone).
2. **Mini-Games → Atari 8-bit** opens the game list.
3. To copy games: pick **ROM Drive (USB)** at the end of the list. The
   Quad shows up on your computer as the **ARCADE ROMS** drive; copy the
   files anywhere on it (folders are fine, three deep), eject, and press MENU or START.

| Control | In the game list | In a game |
|---|---|---|
| Ports 1–4 | port 1: move / fire = play | Atari joysticks 1–4 (on an 800XL only 1–2 exist: start four-player games such as M.U.L.E. with SELECT) |
| Joy-Con stick / click | move / play | joystick 1 / fire 1 |
| Yellow + click | | the Joy-Con as **paddle 1** instead of joystick 1, remembered for that game (press again to switch back; also Game Settings > Thumbstick): its left / right is the knob (Settings > Paddle sensitivity), Red / click the paddle's button. Real paddles on port 1 take over |
| Paddles (ports 1–2) | | the four paddles (pots 0–3), for Kaboom!, Super Breakout and the like; plugging them in shows PADDLES 1 / 2 IN. Turn each one fully right once so it learns its end; 1–228 spans about half the turn, as on a real Atari (Settings > Paddle sensitivity) |
| Red | play | fire 1 |
| Blue | | on-screen keyboard (by default; Game Settings can change it) |
| MENU | | tap: on-screen keyboard, open / close - the game pauses, fire types a key (Settings > Keyboard); its gear key opens Game Settings. Hold 1 s: back to the game list |
| Yellow | | nothing (Game Settings can change it; SPACE is on the keyboard) |
| Green | this game's Game Settings | fire 1, like Red (Game Settings can change it; OPTION is on the keyboard) |
| START / SELECT | START = play; SELECT = play on the other machine (400/800: four joysticks, OS-B) | START / SELECT |
| Side buttons | up / down | right: RETURN, left: nothing (Game Settings can change both) |
| MENU | back to the Quad | hold 1 s: back to the game list |

## Building

PlatformIO, the same toolchain as Pico2Arcade:

```
pio run -e quad
```

| Env | Device |
|---|---|
| `quad` | Quad Ultimate CX / CX-W, chain-booted from its Atari 8-bit slot |
| `duo5200` | Duo 5200 Ultimate, chain-booted from its Atari 8-bit slot |
| `duo_tiny` | Duo Ultimate (160x80 ST7735 panel) - **test build**, standalone: flashes at offset 0 and **replaces** the Duo firmware (reflash the Duo UF2 afterwards). Shares the 1 MB ROM drive at 0x200000 with Pico2Arcade's `duo_tiny` |

## Duo Ultimate (160x80 test build)

The picture is shrunk to the 160x80 panel ([src/a8_scale.c](src/a8_scale.c)):
across 320 → 160 (exactly 2:1: most Atari graphics are 160 colour clocks
wide), down lines 8–223 → 80 rows (2.7:1; the 192-line playfield plus the
lines some games use above and below it, such as Star Raiders' panel). A
**MENU tap** in a game picks the next Scale mode, shown top-left and kept:

| Scale | How |
|---|---|
| Smooth (default) | each row averages the lines it covers, by how much |
| Sharp | each row averages the two lines nearest its middle |
| Drop | the line at each row's middle, the left pixel of each pair: sharpest and fastest, but one-line details can vanish |
| Crop | lines 36–195 at exactly 2:1: no blur, but the lines above and below (often the score) are cut |

Core 1 shrinks the frame 16 rows at a time, each band going out by DMA
while the next is shrunk. The PC harness renders the same thing:
`TINY=prefix` writes the last frame in every mode.

| Control | In a game |
|---|---|
| Ports 1–2 | Atari joysticks 1–2; paddles |
| 5-way | also joystick 1 (press: fire; RETURN is on the on-screen keyboard) |
| Red | fire |
| Black / White | START / SELECT |
| Green / Yellow | fire / nothing (Game Settings) |
| Blue | on-screen keyboard |
| MENU | tap: next Scale mode; hold 1 s: back to the game list |

The menus are laid out for the panel (6x8 font, 10 px rows): **Settings**
scrolls and starts with a **Scale** row (the same choice as the MENU tap),
then Keyboard, FPS display, Volume, Machine, TV system, Paddles
(sensitivity), Controller test. The **controller test** shows both ports
with their paddles' pot values, the 5-way and every button. The
**on-screen keyboard** (Blue) is drawn after the shrink at the panel's own
size - 16 x 11 px keys over the bottom of the picture - so it reads the
same in every Scale mode, Crop included.

The Duo's DB9 pins 7 and 8 are switched, not wired: at start-up they are put
in Atari mode as GameAdapterRGBPico does (pin 7 = +5 V for the paddles,
pin 8 = ground for the joysticks; `a8_input_init`).

**Done and tested on the Duo:** the picture in all four Scale modes at
60 fps, the game list, Settings, the controller test, the on-screen
keyboard, joysticks and paddles on both ports.

**Not done yet:**

- **Not part of the Duo firmware.** This is a standalone test build that
  replaces the Duo firmware; the Duo Ultimate has no Atari 8-bit slot to
  chain-boot it from, so there is no handoff (volume, return to the Duo's
  menu) as on the Quad and Duo 5200. MENU in the game list just restarts it.
- **Landscape only.** No portrait mode.
- **No analog stick**, so no paddle stand-in: the Quad's Joy-Con paddle mode
  (Yellow + click) has nothing to use here. The 5-way could act as a
  digital paddle (a knob turned at a steady speed).
- **Small text is hard to read** at 2.7:1 (40-column text, scores): Crop
  helps when what matters is in lines 36-195.
- **5200 games** have no 5200 controller to use: paddles stand in for its
  stick (as on the Quad), the keypad is on Blue.

## Layout

| Path | What |
|---|---|
| `lib/atari800/` | the atari800 emulation core (GPL-2.0-or-later), a trimmed copy of upstream with a hand-written `config.h`; changes to upstream files are listed in `lib/atari800/PATCHES.md` |
| `src/a8_platform.c` | atari800's platform layer: input, screen, sound |
| `src/a8_display.cpp` | the Atari screen on the TFT (core 1, DMA) |
| `src/a8_input.cpp` | the Quad's controls |
| `src/a8_menu.cpp` | the game list |
| `src/a8_files.cpp` | `fopen` for the core, served from the ROM drive |
| `src/pico_*` | from Pico2Arcade: board pins, PWM audio, the host handoff, the ROM drive |

## Licences

Pico2Atari8 is GPL-3.0 (see `LICENSE`). The atari800 core, by the atari800
development team, is GPL-2.0-or-later (`lib/atari800/COPYING`). AltirraOS and Altirra BASIC
(`lib/atari800/src/roms/`) are © Avery Lee, freely redistributable under
the notice in each file.
