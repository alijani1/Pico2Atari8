# Pico2Atari8

**Atari 8-bit computer games on iCode devices** — the Atari 400/800/XL/XE,
emulated by [atari800](https://github.com/atari800/atari800) on the
RP2350, with your original Atari joysticks.

Status: **in development — Quad Ultimate CX first.**

## What it runs

- **Executables** — `.xex` `.com` `.exe`
- **Cartridges** — `.car` (any type), `.rom` / `.bin` (standard 2–16 KB and
  XEGS sizes)
- **Disk images** — `.atr` `.xfd` (read-only)
- **Zip files** holding any of those (one game per zip). The game is
  unpacked into a reserved flash area when it starts (about a second for a
  90 KB disk) and runs from there; see [docs/QUAD_FLASH.md](docs/QUAD_FLASH.md).

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

**Keyboard.** Hold Blue for half a second in a game and an on-screen
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

## Quad Ultimate CX

Until the Quad firmware gets its own Atari 8-bit slot, Pico2Atari8 goes into
the Quad's **arcade slot** (replacing Pico2Arcade there), and
**Mini-Games → Arcade Games** starts it.

1. Build and flash: `pio run -e quad -t upload` (writes only the arcade
   slot at 0x360000; the Quad firmware and its ROM drive are left alone).
2. **Mini-Games → Arcade Games** opens the game list.
3. To copy games: pick **ROM Drive (USB)** at the end of the list. The
   Quad shows up on your computer as the **ARCADE ROMS** drive; copy the
   files into the root or an `ATARI` folder, eject, and press MENU or START.

| Control | In the game list | In a game |
|---|---|---|
| Ports 1–4 | port 1: move / fire = play | Atari joysticks 1–4 (on an 800XL only 1–2 exist: start four-player games such as M.U.L.E. with SELECT) |
| Joy-Con stick / click | move / play | joystick 1 / fire 1 |
| Red | play | fire 1 |
| Blue | | tap: RETURN; hold ½ s: on-screen keyboard - the game pauses, fire types a key (Settings > Keyboard) |
| Yellow | | SPACE |
| Green | | OPTION |
| START / SELECT | START = play; SELECT = play on the other machine (400/800: four joysticks, OS-B) | START / SELECT |
| Side buttons | up / down | |
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
