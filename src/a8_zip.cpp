/* a8_zip.cpp - zipped games: unpacked into a flash area, run from there.
 *
 * The emulator needs a game's whole file at random (disk sectors, cartridge
 * banks), and there's far too little RAM for that. So a zip's game file is
 * inflated, 4 KB at a time, into a reserved flash area (A8_UNZIP_PHYS,
 * A8_UNZIP_SIZE) and then opened from there exactly like a file on the ROM
 * drive (a8_files.cpp: paths under "/@UNZIP/").
 *
 * Runs before the game starts, when the emulator's big buffers are idle:
 * the frame snapshot holds a copy of a fragmented zip, the Atari screen
 * buffer is the 32 KB inflate dictionary and the 4 KB flash sector staging.
 *
 * Writing and reading the area: a8_flash.cpp.
 */
#include <Arduino.h>
#include <strings.h>
#include "a8_zip.h"
#include "a8_display.h"
#include "a8_flash.h"
#include "pico_romfs.h"
#include "a8_unzip_core.h"

static uint32_t unzipped_size = 0;

#ifdef A8_SD
/* From the SD card: unpacked into PSRAM (A8_SD_UNZIP_SIZE), not the flash
 * area - no flash write (faster, no wear, the USB ports not reset), and
 * room for bigger games. 0: the flash area, as from the flash drive. */
#ifndef A8_SD_UNZIP_SIZE
#define A8_SD_UNZIP_SIZE (3584u * 1024u)
#endif
#if A8_SD_UNZIP_SIZE > 0
PSRAM static uint8_t unzip_psram[A8_SD_UNZIP_SIZE];
#endif
static bool unzip_in_psram = false;
#endif

extern "C" uint8_t *a8_screen_scratch(size_t *size);    /* a8_platform.c, idle before start */

const uint8_t *a8_unzipped(size_t *size) {
    if (!unzipped_size) return nullptr;
    *size = unzipped_size;
#if defined(A8_SD) && A8_SD_UNZIP_SIZE > 0
    if (unzip_in_psram) return unzip_psram;
#endif
    return a8_flash_read(A8_UNZIP_PHYS);
}

/* One 4 KB sector of the area. */
static void write_sector(uint32_t index, const uint8_t *data) {
    a8_flash_write_sector(A8_UNZIP_PHYS + index * 4096u, data);
}

static void put_sector(uint32_t index, const uint8_t *data, void *) {
#if defined(A8_SD) && A8_SD_UNZIP_SIZE > 0
    if (unzip_in_psram) {
        if ((index + 1) * 4096u <= A8_SD_UNZIP_SIZE) memcpy(unzip_psram + index * 4096u, data, 4096);
        return;
    }
#endif
    write_sector(index, data);
}

bool a8_unzip(const char *zip_path, char *inner_name, size_t inner_cap) {
    unzipped_size = 0;

    /* the zip: in place if stored in one piece, else copied into the
     * (idle) frame snapshot */
    const uint8_t *zip;
    size_t zsize;
    if (!romfs_map(zip_path, &zip, &zsize)) {
        size_t cap;
        uint8_t *buf = a8_display_scratch(&cap);
        if (!romfs_read(zip_path, buf, cap, &zsize)) {
            Serial.printf("[zip] %s: can't read (larger than %u bytes?)\n", zip_path, (unsigned)cap);
            return false;
        }
        zip = buf;
    }

    a8z_entry e;
    if (!a8z_find(zip, zsize, &e)) { Serial.printf("[zip] %s: no Atari file inside\n", zip_path); return false; }
    Serial.printf("[zip] %s: %s, %lu -> %lu bytes, method %u\n", zip_path, e.name,
                  (unsigned long)e.csize, (unsigned long)e.usize, e.method);
    uint32_t area = A8_UNZIP_SIZE;
#ifdef A8_SD
    unzip_in_psram = false;
#if A8_SD_UNZIP_SIZE > 0
    if (romfs_on_card()) { unzip_in_psram = true; area = A8_SD_UNZIP_SIZE; }
#endif
#endif
    if (e.usize == 0 || e.usize > area) {
        Serial.printf("[zip] %s is %lu bytes; the unzip area holds %u\n", e.name,
                      (unsigned long)e.usize, (unsigned)area);
        return false;
    }

    size_t scratch_cap;
    uint8_t *scratch = a8_screen_scratch(&scratch_cap);   /* dictionary + one sector */
    if (scratch_cap < A8Z_DICT + A8Z_SECTOR) return false;
    uint32_t t0 = millis(), written = 0;
    int rc = a8z_extract(&e, scratch, scratch + A8Z_DICT, put_sector, nullptr, &written);
    if (rc != A8Z_OK) {
        Serial.printf("[zip] %s: %s after %lu of %lu bytes\n", e.name,
                      rc == A8Z_METHOD ? "unsupported compression" : "inflate failed",
                      (unsigned long)written, (unsigned long)e.usize);
        return false;
    }
    unzipped_size = e.usize;
    strncpy(inner_name, e.name, inner_cap - 1);
    inner_name[inner_cap - 1] = 0;
    Serial.printf("[zip] unpacked %lu bytes in %lu ms\n", (unsigned long)written, (unsigned long)(millis() - t0));
    return true;
}
