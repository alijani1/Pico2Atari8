# Changes to upstream atari800

The files in `src/` are from atari800 (see `UPSTREAM_COMMIT`), unchanged
except as listed here. `config.h` is ours (upstream generates it with
autoconf).

| File | Change |
|---|---|
| `sysrom.c` | `SYSROM_FindInDir`: returns FALSE without `HAVE_DIRENT_H` (no directories to scan) |
| `sio.c` | `DataBuffer`: 1 KB instead of 64 KB unless `NETSIO` (the 64 KB is for FujiNet; disk sectors are at most 512 bytes) |
| `log.c` | not built: `Log_print` / `Log_flushlog` are in `src/a8_platform.c` (upstream's keeps an 8 KB buffer on the stack and prints with printf; ours sends lines to the serial monitor) |
| `cartridge.c` | with `A8_MAPPED_CARTS`, cartridge images point straight at the file when the platform can map it (`A8_MapFile`; the ROM drive is memory-mapped flash) instead of a malloc copy, and such images are never freed (`A8_IsMapped`) |
| `input.c`, `input.h` | with `A8_POT_INPUT`, the 5200's analog sticks come from the platform's `Atari_POT()` (upstream does this only for `DREAMCAST`); a stick at the centre still falls back to the digital directions |
| `pokeysnd.c` | with `A8_SKIP_HIGH_TONES` (classic engine): a pure tone faster than the sample rate is held at its average, half its volume, instead of every edge being emulated (games park silent channels at AUDF 0, ~32 kHz: thousands of events a frame, and a 9.9 kHz alias whine). Channels in a high-pass filter pair (1/3, 2/4 with the filter on) are never parked, since the filter latches their flip-flop; a parked channel resumes with its flip-flop at 1; and `catch_up()` at each register write moves every counter on together, so a channel started after all four were parked doesn't work off the parked time an event at a time (up to 338,000 events in one frame) |
| `pokeysnd.c` | with `A8_FIXED_SYNC`: `Generate_sync_rf`, run on every POKEY write, keeps its sample position in 16.16 fixed point instead of doubles and `modf` (the RP2350 has no double-precision FPU; games that write POKEY ~1240 times a frame took the Pico past its frame time) |
