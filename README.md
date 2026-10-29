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

The machine is an 800XL with 64 KB, NTSC, BASIC off. The Atari operating
system is **AltirraOS** (Avery Lee's free replacement, built in), so no
Atari ROM files are needed.

## Quad Ultimate CX

Until the Quad firmware gets its own Atari 8-bit slot, Pico2Atari8 goes into
the Quad's **arcade slot** (replacing Pico2Arcade there), and
**Mini-Games → Arcade Games** starts it.

1. Build and flash: `pio run -e quad -t upload` (writes only the arcade
   slot at 0x360000; the Quad firmware and its ROM drive are left alone).
2. Copy games onto the Quad's **ARCADE ROMS** drive (**Mini-Games → Arcade
   ROM Drive**): in the root or an `ATARI` folder.
3. **Mini-Games → Arcade Games** opens the game list.

| Control | In the game list | In a game |
|---|---|---|
| Ports 1–4 | port 1: move / fire = play | Atari joysticks 1–4 (XL/XE games read 1–2) |
| Joy-Con stick / click | move / play | joystick 1 / fire 1 |
| Red | play | fire 1 |
| Blue | | RETURN |
| Yellow | | SPACE |
| Green | | OPTION |
| START / SELECT | START = play | START / SELECT |
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
