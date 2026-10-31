# Pico2Atari8

**Atari 8-bit computer games on iCode devices** — the Atari 400/800/XL/XE
and the Atari 5200, emulated by [atari800](https://github.com/atari800/atari800) on the
RP2350, with your original Atari joysticks.

Status: **in development — Quad Ultimate CX first.**

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
`ATARIXL.ROM` (XL/XE OS, 16 KB) onto the drive (root or `ATARI` folder) and
they're used instead. When a game crashes the emulated 6502 on the XL, it
is restarted on an 800 (OS-B if the file is there); files named with
`-osb` or `(OS-B)` start on the 800 straight away. If it crashes there too,
the screen says so and MENU goes back to the list.

**Keyboard.** Press Blue in a game and an on-screen
keyboard with most of the Atari's keys (0-9, A-Z, ; , . / - =, ESC, DEL,
SPACE, RETURN) opens over the bottom of the picture, and the game pauses.
The stick (or Joy-Con) moves the cursor; fire types the key: the game runs
while fire holds it down (and a few frames after), then pauses again. A
tap of Blue closes the keyboard and the game carries on.

**Disk drive off.** Some disk games refuse to start while a drive answers
(this Shamus disk reboots on START if any drive replies to a status
request). The DRV key on the on-screen keyboard switches disk drive 1 off
and on, like the switch on a real drive: let the game load, type DRV
("DRIVE OFF"), then START.

**Settings** (a row at the end of the game list; saved in flash):

| Setting | Choices |
|---|---|
| Keyboard | Pause+Auto Resume (default): the game pauses, the first key typed is sent and closes the keyboard / Auto-pause: paused while open, each key runs the game briefly, Blue closes / Keep running |
| FPS display | Off (default) / On |
| Volume | 0-100 % (shared with the Quad) |
| Default machine | 800XL (default) / 400/800 - SELECT in the list starts the other |
| TV system | NTSC (default) / PAL |
| Paddle sensitivity | (Quad) how much of a paddle's turn spans the game's whole range: Very low = all of it, Low 75 %, Medium 50 % (default, about what Atari games expect), High 40 %, Very high 30 % |
| Controller test | every control live: the 5200 controllers' sticks (as the pot value the game gets, plus the raw counts and the range learnt), Fire A / B and keys; the Quad's joystick ports, paddles on ports 1–2 (pot value, raw counts, range, buttons) and side buttons; the Joy-Con; the device buttons. MENU goes back |

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
| Yellow | `*` |
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
| Yellow + click | | the Joy-Con as **paddle 1** instead of joystick 1, remembered for that game (press again to switch back): its left / right is the knob (Settings > Paddle sensitivity), Red / click the paddle's button. Real paddles on port 1 take over |
| Paddles (ports 1–2) | | the four paddles (pots 0–3), for Kaboom!, Super Breakout and the like; plugging them in shows PADDLES 1 / 2 IN. Turn each one fully right once so it learns its end; 1–228 spans about half the turn, as on a real Atari (Settings > Paddle sensitivity) |
| Red | play | fire 1 |
| Blue | | on-screen keyboard: press to open / close - the game pauses, fire types a key (Settings > Keyboard) |
| Yellow | | SPACE |
| Green | | OPTION |
| START / SELECT | START = play; SELECT = play on the other machine (400/800: four joysticks, OS-B) | START / SELECT |
| Side buttons | up / down | right: RETURN |
| MENU | back to the Quad | hold 1 s: back to the game list |

## Building

PlatformIO, the same toolchain as Pico2Arcade:

```
pio run -e quad
```

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

Pico2Atari8 is GPL-3.0 (see `LICENSE`). The atari800 core is
GPL-2.0-or-later (`lib/atari800/COPYING`). AltirraOS and Altirra BASIC
(`lib/atari800/src/roms/`) are © Avery Lee, freely redistributable under
the notice in each file.
