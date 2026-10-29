/* a8_files.cpp - fopen() for the atari800 core, served from the ROM drive.
 *
 * The core opens cartridges, executables and disk images with stdio. There
 * is no filesystem behind newlib here, so fopen/fclose are wrapped at link
 * time (-Wl,--wrap=fopen,--wrap=fclose in platformio.ini): a read-only open
 * returns an fmemopen() stream over the file - read in place from flash when
 * it's stored in one piece (the drive is memory-mapped), else slurped into
 * RAM and freed again on fclose. Any write
 * mode fails, so the core treats disks as write-protected and skips saving
 * its config. Files are the drive's paths, e.g. "/ATARI/GAME.XEX".
 */
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico_romfs.h"
#include "a8_platform.h"

extern "C" {
FILE *__real_fopen(const char *path, const char *mode);
int   __real_fclose(FILE *f);
FILE *__wrap_fopen(const char *path, const char *mode);
int   __wrap_fclose(FILE *f);
}

/* Streams we opened, and the buffers behind them. A few at a time is plenty
 * (a cartridge or executable is read and closed; a disk stays open). */
static struct { FILE *f; uint8_t *buf; } open_files[4];

extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (path) {
        static char stage[48];
        snprintf(stage, sizeof(stage), "fopen %s %s", path, mode ? mode : "");
        a8_stage = stage;
    }
    if (!path || !mode || strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))
        return nullptr;                       /* read-only drive */
    int slot = -1;
    for (int i = 0; i < 4; i++) if (!open_files[i].f) { slot = i; break; }
    if (slot < 0) return nullptr;
    uint8_t *buf = nullptr;            /* ours to free, if slurped */
    const uint8_t *data = nullptr;
    size_t size = 0;
    if (romfs_map(path, &data, &size)) {
        /* in place */
    } else if (romfs_slurp(path, &buf, &size)) {
        data = buf;
    } else {
        return nullptr;
    }
    FILE *f = fmemopen((void *)data, size, "rb");
    if (!f) { free(buf); return nullptr; }
    open_files[slot].f = f;
    open_files[slot].buf = buf;
    return f;
}

extern "C" int __wrap_fclose(FILE *f) {
    int r = __real_fclose(f);
    for (int i = 0; i < 4; i++) {
        if (open_files[i].f == f) {
            free(open_files[i].buf);
            open_files[i].f = nullptr;
            open_files[i].buf = nullptr;
        }
    }
    return r;
}
