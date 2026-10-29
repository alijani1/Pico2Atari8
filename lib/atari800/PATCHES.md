# Changes to upstream atari800

The files in `src/` are from atari800 (see `UPSTREAM_COMMIT`), unchanged
except as listed here. `config.h` is ours (upstream generates it with
autoconf).

| File | Change |
|---|---|
| `sysrom.c` | `SYSROM_FindInDir`: returns FALSE without `HAVE_DIRENT_H` (no directories to scan) |
| `sio.c` | `DataBuffer`: 1 KB instead of 64 KB unless `NETSIO` (the 64 KB is for FujiNet; disk sectors are at most 512 bytes) |
