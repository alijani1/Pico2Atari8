# Flash layout for Pico2Atari8: Quad Ultimate CX and Duo 5200 Ultimate

What the next Quad and Duo 5200 firmwares (GameAdapterRGBPico) need to
reserve so Pico2Atari8 can sit beside Pico2Arcade. Both devices have 4 MB of
flash, and **the ARCADE ROMS drive keeps its full size on both** (Quad
256 KB, Duo 5200 512 KB). The room comes out of the main firmware instead.

## What Pico2Atari8 needs on each device

| Area | Size | Why |
|---|---|---|
| Atari 8-bit slot | 384 KB | its image (331 KB today), chain-booted like the arcade slot |
| Unzip area | 128 KB | a zipped game is unpacked here and run from flash (largest disk 92 KB, largest cartridge 128 KB) |
| Settings | 4 KB | the Settings screen's choices |

All three are read through QMI address-translation window 1 (0x10400000 =
physical 0), so they can be anywhere; the slot's chain-boot window has to
reach the ROM drive (it may cover the arcade slot on the way).

## Making room: the firmware's audio

Both firmwares share the same mini-game sounds (`include/sounds.hpp`),
already 8-bit mono (`scripts/downsample_sound_8bit.py`) and played at
16 kHz (`PMTA_SAMPLE_RATE`), one byte a sample. They are still most of the
constant data (2.6 MB of the Duo 5200's 3.0 MB). The biggest:

| Asset | Size (8-bit, 16 kHz) | Length |
|---|---|---|
| `music2` | 979 KB | 61 s |
| `music1` | 254 KB | 16 s |
| `ambiant` | 164 KB | 10 s |
| `bitmap_5200_Trackball` (5200, an image) | 128 KB | |
| `menu16s8` | 125 KB | 8 s |
| `theend` / `jawdeath` / `victory` | 86 / 69 / 54 KB | |

`music2` alone covers either device. The options:

| Option for `music2` | Size | Saves | Trade-off |
|---|---|---|---|
| **Drop it: Brick Bash plays `music1` (simplest)** | 0 | **979 KB** | Brick Bash and Iron Dome share one 16 s track; three lines in `BricksGame.cpp`, two in `sounds.hpp` |
| **4-bit IMA ADPCM at 16 kHz (recommended)** | 490 KB | **489 KB** | none audible: same rate, whole track; needs a small decoder in PicoMultiTrackAudio |
| 8-bit at 8 kHz | 490 KB | 489 KB | duller sound (half the rate) |
| A shorter loop (~30 s) at 16 kHz | ~490 KB | ~489 KB | less of the music; needs a clean repeat point |

The Quad needs ~113 KB of it, the Duo 5200 ~386 KB (below).

### Simplest: drop `music2`

`music2` plays only in Brick Bash; `music1` is Iron Dome's music. Brick Bash
can use `music1` instead:

- `src/games/BricksGame.cpp` lines 140, 141, 302: `"music2"` -> `"music1"`
  (the `isPlaying`, `play` and `stop` calls);
- `include/sounds.hpp`: remove `#include "sounds/music2.h"` and
  `PMTA_SOUND_8(music2)`.

That frees all 979 KB with no new code: room for both devices with plenty to
spare. The cost is that the two games share one track.

### Recommended: IMA ADPCM

Keeps both tracks.

IMA ADPCM stores each sample as a 4-bit step from the one before, so a
track takes half the bytes of 8-bit PCM at the same 16 kHz, and it usually
sounds as good as the 8-bit original on the small PWM speaker (it starts
from 16-bit, so it can even sound better).

- **Converting:** a script beside `scripts/downsample_sound_8bit.py` turns a
  16-bit original (or ffmpeg: `-acodec adpcm_ima_wav`) into a 4-bit array,
  plus a few bytes of decoder state.
- **Playing:** PicoMultiTrackAudio gets a third sound type next to 16-bit
  and 8-bit (`PMTA_SOUND_ADPCM`), decoded one sample at a time as it mixes:
  a table lookup, an add and a clamp, a negligible load at 16 kHz.
- **Looping and seeking:** decoding has to start from the beginning or from a
  saved state; restarting a looped track (as Bricks does with `music2`) is
  fine. Store a state every few KB if a track ever needs to start mid-way.
- **Which tracks:** `music2` alone is enough for both devices; `music1`
  (254 KB -> 127 KB) and `ambiant` (164 KB -> 82 KB) can follow if more
  room is ever needed.

---

## Quad Ultimate CX (MODEL_QUAD)

### Today (Quad firmware 1.0)

| Physical | Size | Use |
|---|---|---|
| `0x000000 .. ~0x2BA000` | 2.86 MB | Quad firmware (`board_upload.maximum_size = 0x360000`) |
| `~0x2BA000 .. 0x346FFF` | ~560 KB | **unused** |
| `0x347000 .. 0x35FFFF` | 100 KB | Quad LittleFS (96 KB) + EEPROM (4 KB) |
| `0x360000 .. 0x3BFFFF` | 384 KB | arcade slot (Pico2Arcade, or Pico2Atari8 for now) |
| `0x3C0000 .. 0x3FFFFF` | 256 KB | ARCADE ROMS drive (FAT12) |

Until the next firmware, Pico2Atari8 is flashed into the arcade slot and uses
the unused gap for its unzip area (`0x320000`) and settings (`0x340000`).

### Next Quad firmware

| Physical | Size | Use |
|---|---|---|
| `0x000000 .. 0x2BFFFF` | 2.75 MB | Quad firmware: `board_upload.maximum_size = 0x2C0000` |
| `0x2C0000 .. 0x31FFFF` | 384 KB | **Atari 8-bit slot** |
| `0x320000 .. 0x33FFFF` | 128 KB | **Pico2Atari8 unzip area** (as today) |
| `0x340000 .. 0x340FFF` | 4 KB | **Pico2Atari8 settings** (as today) |
| `0x341000 .. 0x346FFF` | 24 KB | unused |
| `0x347000 .. 0x35FFFF` | 100 KB | Quad LittleFS + EEPROM (unchanged) |
| `0x360000 .. 0x3BFFFF` | 384 KB | arcade slot (unchanged) |
| `0x3C0000 .. 0x3FFFFF` | 256 KB | ARCADE ROMS drive (unchanged) |

The firmware must come down from 2.86 MB to **2.75 MB or less: at least
~113 KB smaller**. The unzip area and settings stay where today's
Pico2Atari8 already puts them.

---

## Duo 5200 Ultimate (MODEL_5200)

### Today (Duo 5200 firmware 1.1)

| Physical | Size | Use |
|---|---|---|
| `0x000000 .. ~0x2DE000` | 3.01 MB | Duo 5200 firmware (`build_release.py` checks it stays below `0x300000`) |
| `~0x2DE000 .. 0x2FFFFF` | ~134 KB | free |
| `0x300000 .. 0x35FFFF` | 384 KB | arcade slot (last 4 KB: high scores) |
| `0x360000 .. 0x3DFFFF` | 512 KB | ARCADE ROMS drive (FAT12) |
| `0x3E0000 .. 0x3E6FFF` | 28 KB | unused |
| `0x3E7000 .. 0x3FFFFF` | 100 KB | LittleFS (96 KB) + EEPROM (4 KB), where 5200 1.0 put them |

Free today: ~134 KB + 28 KB = ~162 KB, against the 516 KB needed.

### Next Duo 5200 firmware

| Physical | Size | Use |
|---|---|---|
| `0x000000 .. 0x27FFFF` | 2.5 MB | Duo 5200 firmware: must stay below `0x280000` |
| `0x280000 .. 0x29FFFF` | 128 KB | **Pico2Atari8 unzip area** |
| `0x2A0000 .. 0x2FFFFF` | 384 KB | **Atari 8-bit slot** |
| `0x300000 .. 0x35FFFF` | 384 KB | arcade slot (unchanged) |
| `0x360000 .. 0x3DFFFF` | 512 KB | ARCADE ROMS drive (unchanged) |
| `0x3E0000 .. 0x3E0FFF` | 4 KB | **Pico2Atari8 settings** (in today's unused 28 KB) |
| `0x3E1000 .. 0x3E6FFF` | 24 KB | unused |
| `0x3E7000 .. 0x3FFFFF` | 100 KB | LittleFS + EEPROM (unchanged) |

The firmware must come down from 3.01 MB to **2.5 MB or less: at least
~386 KB smaller**. The Atari slot's chain-boot window runs
`0x2A0000 .. 0x3DFFFF` (over the arcade slot, to the drive): the ROM drive
is at window offset `0xC0000`.

Pico2Atari8 also needs a `duo5200` build: the 5200 pin map and controllers
(analog sticks as joysticks, the keypad's 0-9 * # as keys), much as
Pico2Arcade's 5200 build does.

---

## Asked of both firmwares

1. **Shrink the firmware** to the size above (the audio, above) and lower
   its size limit, so the areas above stay free.
2. **The Atari 8-bit slot**, chain-booted like the arcade slot but with its
   own magic, so both images can be installed and launched.
3. **Pass the locations** in watchdog scratch registers, as for the ROM
   drive: the ROM drive's offset in the Atari slot's window, and the unzip
   area and settings sector, so they can move without rebuilding
   Pico2Atari8.
4. **Mini-Games menu:** an "Atari 8-bit" entry next to "Arcade Games".

## Flash wear

The unzip area is written on every zipped-game launch (only the 4 KB
sectors the game needs) and the settings sector only when a setting
changes. NOR flash is good for ~100,000 erases per sector: decades of daily
use.
