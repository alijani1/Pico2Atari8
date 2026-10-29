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
