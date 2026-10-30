# Quad Ultimate CX: flash for Pico2Atari8

What the next Quad firmware (GameAdapterRGBPico, MODEL_QUAD) needs to
reserve for Pico2Atari8. Today Pico2Atari8 borrows the arcade slot and uses
flash the Quad firmware happens not to use.

## Today (Quad firmware 1.0)

| Physical | Size | Use |
|---|---|---|
| `0x000000 .. ~0x2BA000` | 2.86 MB | Quad firmware (`board_upload.maximum_size = 0x360000`) |
| `~0x2BA000 .. 0x346FFF` | ~560 KB | **unused** |
| `0x347000 .. 0x35FFFF` | 100 KB | Quad LittleFS (96 KB) + EEPROM (4 KB) |
| `0x360000 .. 0x3BFFFF` | 384 KB | arcade slot (Pico2Arcade, or Pico2Atari8 for now) |
| `0x3C0000 .. 0x3FFFFF` | 256 KB | ARCADE ROMS drive (FAT12) |

Pico2Atari8 uses, inside the unused gap:

| Physical | Size | Use |
|---|---|---|
| `0x31F000 .. 0x31FFFF` | 4 KB | its settings (`A8_SETTINGS_PHYS`) |
| `0x320000 .. 0x33FFFF` | 128 KB | zipped games, unpacked (`A8_UNZIP_PHYS` / `A8_UNZIP_SIZE`) |

## Asked of the next Quad firmware

1. **Reserve the settings sector and the unzip area** `0x31F000 ..
   0x33FFFF`: lower `board_upload.maximum_size` to `0x31F000` (the firmware
   is 2.86 MB) and keep LittleFS/EEPROM where they are.
2. **A slot of its own for Pico2Atari8** (it's ~310 KB; 384 KB like the
   arcade slot), chain-booted like the arcade with its own magic, so both
   can be installed. That needs about 384 KB more below the unzip area, so
   the firmware has to shrink: ~1.9 MB of it is raw mini-game audio
   (`music2` 980 KB, `music1` 254 KB, `ambiant` 164 KB, `menu16s8` 125 KB,
   ...); compressing or downsampling that frees far more than enough.
3. **Pass the unzip area's location** in a watchdog scratch register (like
   the ROM drive's), so it can move without rebuilding Pico2Atari8.
4. **Mini-Games menu:** an "Atari 8-bit" entry next to "Arcade Games".

The unzip area is written on every zipped-game launch (only the 4 KB
sectors the game needs). NOR flash is good for ~100,000 erases per sector:
decades of daily use.

Pico2Atari8 reads the area through QMI address-translation window 1
(0x10400000 = physical 0), since the chain-boot window at 0x10000000 maps
only the slot and the drive.
