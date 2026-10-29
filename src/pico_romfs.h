/* pico_romfs.h — where the game ROM zips come from.
 *
 * Default (PicoCartReader): the SD card, /roms/<set>.zip.
 * PICO_QUAD / PICO_5200 (hosted: Quad Ultimate CX, Duo 5200 Ultimate): the
 *   host's 256 KB FAT12 "ARCADE ROMS" drive (early Quad drives: "QUAD ROMS") in
 *   flash. The Quad chain-boots us with the boot ROM's rom_chain_image(); the
 *   window it maps to 0x10000000 contains this image followed by the ROM drive,
 *   so the drive is plain memory at 0x10000000 + its window offset. The Quad
 *   passes offset/size in watchdog scratch[2]/[3] (see pico_handoff.h); build
 *   defaults are used if they are missing. Read via XIP; the Quad writes the drive
 *   (Mini-Games > Arcade ROM Drive), as does our own USB ROM drive mode
 *   (it gets the physical offset from the Quad in scratch[0]).
 *
 * PICO_STANDALONE (plain Pico 2): our own FAT12 "ARCADE ROMS" drive at 2 MB
 *   (1 MB), read the same way (XIP maps it 1:1).
 * On both flash boards the drive is written only in USB ROM drive mode
 * (pico_drive.cpp), which formats it on first use.
 *
 * Paths are the SD-style "/roms/<name>.zip"; on the Quad drive the zip is
 * looked up by name in the root directory, or in a ROMS folder.
 */
#ifndef PICO_ROMFS_H
#define PICO_ROMFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the ROM storage (SD init / locate + validate the flash drive). */
bool romfs_init(void);
/* True if the ROM zip at `path` (e.g. "/roms/galaga.zip") is present. */
bool romfs_exists(const char *path);
/* Read the whole file into a malloc'd buffer (caller frees). */
bool romfs_slurp(const char *path, uint8_t **out, size_t *out_sz);
/* Every file on the drive - root, then the ATARI and ROMS folders. `path`
 * ("/ATARI/GAME.XEX") opens it with romfs_slurp; `display` is its long name
 * when it has one. Return false from the callback to stop. Flash boards only. */
typedef bool (*romfs_list_cb)(const char *path, const char *display, uint32_t size, void *ctx);
void romfs_list(romfs_list_cb cb, void *ctx);

/* Read a file into buf (at most cap bytes); false if missing or too big. */
bool romfs_read(const char *path, uint8_t *buf, size_t cap, size_t *out_sz);

/* A file stored in one piece on a flash drive: its address in flash (read
 * it in place, no copy). False if it's fragmented (use romfs_slurp). */
bool romfs_map(const char *path, const uint8_t **out, size_t *out_sz);

/* Short human description of where ROMs go, for menu/error text. */
const char *romfs_where(void);
/* Flash boards only: the drive's XIP address, physical flash offset and size
 * (formatted or not) — for USB ROM drive mode. */
void romfs_location(const uint8_t **xip, uint32_t *phys, uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
