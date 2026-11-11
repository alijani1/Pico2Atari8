/* pico_romfs.cpp — see pico_romfs.h. */
#include <Arduino.h>
#include <string.h>
#include "pico_romfs.h"
#include "pico_board.h"
#include <strings.h>
#include <stdio.h>

#ifndef PICO_BOARD_TABLE
/* ── SD card backend (PicoCartReader) ─────────────────────────────────── */
#include <SD.h>

extern "C" bool pico_sd_init(void);

extern "C" bool romfs_init(void) { return pico_sd_init(); }

extern "C" bool romfs_exists(const char *path) { return SD.exists(path); }

extern "C" bool romfs_slurp(const char *path, uint8_t **out, size_t *out_sz) {
    File f = SD.open(path, FILE_READ);
    if (!f) { Serial.printf("[romfs] open failed: %s\n", path); return false; }
    size_t sz = f.size();
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) { Serial.printf("[romfs] malloc %u bytes failed\n", (unsigned)sz); f.close(); return false; }
    size_t got = f.read(buf, sz);
    f.close();
    if (got != sz) { Serial.printf("[romfs] short read: %u/%u\n", (unsigned)got, (unsigned)sz); free(buf); return false; }
    *out = buf; *out_sz = sz;
    return true;
}

extern "C" const char *romfs_where(void) { return "/roms/ on the SD card"; }

extern "C" bool romfs_peek(const char *path, long ofs, uint8_t *buf, size_t n, size_t *size) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    const long sz = (long)f.size(), at = ofs < 0 ? sz + ofs : ofs;
    const bool ok = at >= 0 && at + (long)n <= sz && f.seek((uint32_t)at) && f.read(buf, n) == n;
    f.close();
    if (ok) *size = (size_t)sz;
    return ok;
}
extern "C" bool romfs_on_card(void) { return true; }
extern "C" FILE *romfs_fopen_stream(const char *path) { (void)path; return nullptr; }
extern "C" void romfs_cache_reset(void) {}

#else
/* ── Flash ROM drive backend (read-only FAT12/FAT16 over XIP) ───────────
 * Quad (chain-booted) and standalone Pico 2. The drive is read through XIP;
 * writing it (USB ROM drive mode, pico_drive.cpp) uses its physical offset. */
#include <hardware/structs/watchdog.h>
#include "pico_handoff.h"

/* Hosted builds: where the host's ROM drive is if it didn't pass it (set per
 * env in platformio.ini; defaults = the Quad). */
#ifndef PICO_HOST_FAT_WINDOW_OFS
#define PICO_HOST_FAT_WINDOW_OFS 0x170000u   /* Quad 1.1: 0x380000 - 0x210000 */
#endif
#ifndef PICO_HOST_FAT_PHYS
#define PICO_HOST_FAT_PHYS       0x380000u   /* Quad 1.1: physical flash offset */
#endif
#ifndef PICO_HOST_FAT_SIZE
#define PICO_HOST_FAT_SIZE       0x80000u    /* 512 KB */
#endif
#ifndef PICO_DRIVE_PHYS
#define PICO_DRIVE_PHYS          0x200000u   /* standalone: drive at 2 MB ... */
#endif
#ifndef PICO_DRIVE_SIZE
#define PICO_DRIVE_SIZE          0x100000u   /* ... 1 MB */
#endif

static const uint8_t *vol = nullptr;         /* start of the FAT volume (XIP), when formatted */
static uint32_t volSize = 0;
static const uint8_t *s_xip = nullptr;       /* drive location, formatted or not */
static uint32_t s_phys = 0, s_size = 0;

static void locate_drive(void);
extern "C" void romfs_location(const uint8_t **xip, uint32_t *phys, uint32_t *size) {
    if (!s_xip) locate_drive();
    *xip = s_xip; *phys = s_phys; *size = s_size;
}

/* Where is the drive? Chain-booted by the Quad: it tags scratch[0] with the
 * drive's physical offset and passes its offset/size in our window in
 * scratch[2]/[3]. Otherwise: the Quad build's defaults, or (standalone) our
 * own drive, which XIP sees at its physical offset. */
static void locate_drive(void) {
    uint32_t s0 = watchdog_hw->scratch[HANDOFF_SCRATCH_HOST];
    if ((s0 & 0xFFFF0000u) == HANDOFF_HOST_QUAD_TAG) {
        uint32_t ofs = PICO_HOST_FAT_WINDOW_OFS, size = PICO_HOST_FAT_SIZE;
        uint32_t s2 = watchdog_hw->scratch[HANDOFF_SCRATCH_FATOFS];
        uint32_t s3 = watchdog_hw->scratch[HANDOFF_SCRATCH_FATSIZE];
        if ((s2 & 0xFFFF0000u) == HANDOFF_FATOFS_TAG)  ofs  = (s2 & 0xFFFFu) << 12;
        if ((s3 & 0xFFFF0000u) == HANDOFF_FATSIZE_TAG) size = (s3 & 0xFFFFu) << 12;
        s_xip = (const uint8_t *)(XIP_BASE + ofs);
        s_phys = (s0 & 0xFFFFu) << 12;
        s_size = size;
    } else {
#ifdef PICO_HOSTED
        s_xip = (const uint8_t *)(XIP_BASE + PICO_HOST_FAT_WINDOW_OFS);
        s_phys = PICO_HOST_FAT_PHYS;
        s_size = PICO_HOST_FAT_SIZE;
#else
        s_xip = (const uint8_t *)(XIP_BASE + PICO_DRIVE_PHYS);
        s_phys = PICO_DRIVE_PHYS;
        s_size = PICO_DRIVE_SIZE;
#endif
    }
}
static uint32_t bps, spc, fatOfs, rootOfs, rootEntries, dataOfs, clusterBytes, clusterCount;
static bool fat16 = false;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Next cluster in the chain; >= 0xFF8 (FAT12) / 0xFFF8 (FAT16) = end. */
static uint32_t fat_next(uint32_t c) {
    if (fat16) return rd16(vol + fatOfs + c * 2);
    uint16_t v = rd16(vol + fatOfs + c + c / 2);
    return (c & 1) ? (v >> 4) : (v & 0x0FFF);
}
static bool fat_eoc(uint32_t c) { return fat16 ? c >= 0xFFF8 : c >= 0x0FF8; }
static bool cluster_ok(uint32_t c) { return c >= 2 && c < clusterCount + 2; }
static uint32_t cluster_ofs(uint32_t c) { return dataOfs + (c - 2) * clusterBytes; }

static bool romfs_init_flash(void) {
    if (!s_xip) locate_drive();
    vol = s_xip;
    volSize = s_size;
    uint32_t ofs = s_phys;

    if (vol[510] != 0x55 || vol[511] != 0xAA) {
        Serial.printf("[romfs] no FAT volume at 0x%lx (format it: ROM Drive mode)\n", (unsigned long)ofs);
        vol = nullptr; return false;
    }
    bps = rd16(vol + 11);
    spc = vol[13];
    uint32_t rsvd = rd16(vol + 14), nfats = vol[16], spf = rd16(vol + 22);
    rootEntries = rd16(vol + 17);
    uint32_t totSec = rd16(vol + 19);
    if (totSec == 0) totSec = rd32(vol + 32);
    if (bps != 512 || spc == 0 || nfats == 0 || spf == 0 || totSec * bps > volSize) {
        Serial.println("[romfs] unsupported FAT geometry");
        vol = nullptr; return false;
    }
    fatOfs = rsvd * bps;
    rootOfs = (rsvd + nfats * spf) * bps;
    uint32_t rootSec = (rootEntries * 32 + bps - 1) / bps;
    dataOfs = rootOfs + rootSec * bps;
    clusterBytes = spc * bps;
    clusterCount = (totSec * bps - dataOfs) / clusterBytes;
    fat16 = clusterCount >= 4085;
    Serial.printf("[romfs] FAT%d ROM drive at +0x%lx: %lu clusters x %lu B\n",
                  fat16 ? 16 : 12, (unsigned long)ofs,
                  (unsigned long)clusterCount, (unsigned long)clusterBytes);
    return true;
}

/* "galaga.zip" -> "GALAGA  ZIP" (8.3, space padded). False if not 8.3. */
static bool to_83(const char *name, char out[11]) {
    memset(out, ' ', 11);
    const char *dot = strrchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name);
    if (base == 0 || base > 8) return false;
    for (size_t i = 0; i < base; i++) out[i] = toupper((unsigned char)name[i]);
    if (dot) {
        size_t ext = strlen(dot + 1);
        if (ext > 3) return false;
        for (size_t i = 0; i < ext; i++) out[8 + i] = toupper((unsigned char)dot[1 + i]);
    }
    return true;
}

/* Look for `name83` among `count` 32-byte entries at `dir`. */
static const uint8_t *scan_dir(const uint8_t *dir, uint32_t count, const char *name83, uint8_t wantAttr) {
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = dir + i * 32;
        if (e[0] == 0x00) return nullptr;                 /* end of directory */
        if (e[0] == 0xE5) continue;                        /* deleted */
        uint8_t attr = e[11];
        if (attr == 0x0F || (attr & 0x08)) continue;       /* LFN piece / volume label */
        if ((attr & 0x10) != wantAttr) continue;           /* file vs. directory */
        if (memcmp(e, name83, 11) == 0) return e;
    }
    return nullptr;
}

/* Find a directory entry in a subdirectory's cluster chain (wantAttr 0x10:
 * a folder). */
static const uint8_t *scan_subdir(uint32_t cluster, const char *name83, uint8_t wantAttr = 0) {
    for (int guard = 0; cluster_ok(cluster) && guard < 4096; guard++) {
        const uint8_t *e = scan_dir(vol + cluster_ofs(cluster), clusterBytes / 32, name83, wantAttr);
        if (e) return e;
        cluster = fat_next(cluster);
        if (fat_eoc(cluster)) break;
    }
    return nullptr;
}

/* A folder in the root directory, by its 8.3 name ("ATARI"). */
static const uint8_t *find_dir(const char *name) {
    char n83[11];
    if (!vol || !to_83(name, n83)) return nullptr;
    return scan_dir(vol + rootOfs, rootEntries, n83, 0x10);
}

static const int LIST_DEPTH = 3;           /* folders below the root that are listed */

/* `name83` (a file) among `count` entries at `dir` or in their folders,
 * `depth` deep at most as the game list goes (hidden and system ones
 * skipped). */
static const uint8_t *search_folder(uint32_t cluster, const char *name83, int depth);
static const uint8_t *search_entries(const uint8_t *dir, uint32_t count, const char *name83, int depth) {
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = dir + i * 32;
        if (e[0] == 0x00) return nullptr;                 /* end of directory */
        if (e[0] == 0xE5 || e[0] == '.') continue;         /* deleted, . and .. */
        uint8_t attr = e[11];
        if (attr == 0x0F || (attr & 0x08)) continue;       /* LFN piece / volume label */
        if (attr & 0x10) {
            if (depth >= LIST_DEPTH || (attr & 0x06)) continue;
            const uint8_t *r = search_folder(rd16(e + 26), name83, depth + 1);
            if (r) return r;
        } else if (memcmp(e, name83, 11) == 0) {
            return e;
        }
    }
    return nullptr;
}
static const uint8_t *search_folder(uint32_t cluster, const char *name83, int depth) {
    for (int guard = 0; cluster_ok(cluster) && guard < 4096; guard++) {
        const uint8_t *e = search_entries(vol + cluster_ofs(cluster), clusterBytes / 32, name83, depth);
        if (e) return e;
        cluster = fat_next(cluster);
        if (fat_eoc(cluster)) break;
    }
    return nullptr;
}

/* Directory entry for `path`: "/DIR/SUB/NAME.EXT" follows the folders
 * (8.3 names); a bare name (or "/roms/NAME.EXT") is looked for in the root,
 * then the ROMS folder, then the ATARI folder, then in any folder (the OS
 * ROM files, ATARIOSB.ROM ..., wherever they were put). */
static const uint8_t *find_file(const char *path) {
    if (!vol) return nullptr;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char n83[11];
    if (!to_83(name, n83)) return nullptr;
    const char *p = (path[0] == '/') ? path + 1 : path;
    if (strchr(p, '/') && strncasecmp(p, "roms/", 5) != 0) {
        /* walk the folders */
        const uint8_t *d = nullptr;
        while (const char *slash = strchr(p, '/')) {
            char dir[13], d83[11];
            size_t n = (size_t)(slash - p);
            if (n == 0 || n >= sizeof(dir)) return nullptr;
            memcpy(dir, p, n); dir[n] = 0;
            if (!to_83(dir, d83)) return nullptr;
            d = d ? scan_subdir(rd16(d + 26), d83, 0x10)
                  : scan_dir(vol + rootOfs, rootEntries, d83, 0x10);
            if (!d) return nullptr;
            p = slash + 1;
        }
        return scan_subdir(rd16(d + 26), n83);
    }
    const uint8_t *e = scan_dir(vol + rootOfs, rootEntries, n83, 0);
    if (e) return e;
    static const char *const dirs[] = { "ROMS", "ATARI" };
    for (const char *dn : dirs) {
        const uint8_t *d = find_dir(dn);
        if (d && (e = scan_subdir(rd16(d + 26), n83))) return e;
    }
    return search_entries(vol + rootOfs, rootEntries, n83, 0);
}

/* ---- listing --------------------------------------------------------- */

/* Long file name pieces (VFAT): 13 UCS-2 characters each, ASCII kept. */
static void lfn_piece(const uint8_t *e, char *lfn, size_t cap) {
    static const uint8_t offs[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    int seq = (e[0] & 0x1F) - 1;
    if (seq < 0) return;
    for (int i = 0; i < 13; i++) {
        size_t at = (size_t)seq * 13 + i;
        if (at + 1 >= cap) return;
        uint16_t c = rd16(e + offs[i]);
        if (c == 0x0000 || c == 0xFFFF) { lfn[at] = 0; return; }
        lfn[at] = (c < 0x80) ? (char)c : '?';
        if (i == 12 && (e[0] & 0x40)) lfn[at + 1] = 0;
    }
}

static bool list_folder(uint32_t cluster, const char *folder, int depth,
                        romfs_list_cb cb, void *ctx);

/* Calls cb for each file among `count` entries at `dir`, and goes into its
 * folders (`depth` deep at most; hidden and system ones skipped). Returns
 * true once the callback asked to stop. `lfn` carries a long name across
 * clusters. */
static bool list_entries(const uint8_t *dir, uint32_t count, const char *folder, int depth,
                         romfs_list_cb cb, void *ctx, char *lfn, size_t cap) {
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = dir + i * 32;
        if (e[0] == 0x00) return false;                    /* end of directory */
        if (e[0] == 0xE5) { lfn[0] = 0; continue; }        /* deleted */
        uint8_t attr = e[11];
        if (attr == 0x0F) {                                 /* long name piece */
            if (e[0] & 0x40) memset(lfn, 0, cap);
            lfn_piece(e, lfn, cap);
            continue;
        }
        if ((attr & 0x08) || e[0] == '.') { lfn[0] = 0; continue; }   /* label, . and .. */
        if (attr & 0x10) {                                  /* a folder */
            lfn[0] = 0;
            if (depth >= LIST_DEPTH || (attr & 0x06)) continue;
            char dn[13], sub[48];
            int k = 0;
            for (int j = 0; j < 8 && e[j] != ' '; j++) dn[k++] = (char)e[j];
            if (e[8] != ' ') {
                dn[k++] = '.';
                for (int j = 8; j < 11 && e[j] != ' '; j++) dn[k++] = (char)e[j];
            }
            dn[k] = 0;
            if (folder) snprintf(sub, sizeof(sub), "%s/%s", folder, dn);
            else        snprintf(sub, sizeof(sub), "%s", dn);
            if (list_folder(rd16(e + 26), sub, depth + 1, cb, ctx)) return true;
            continue;
        }
        char n83[13];
        int k = 0;
        for (int j = 0; j < 8 && e[j] != ' '; j++) n83[k++] = (char)e[j];
        if (e[8] != ' ') {
            n83[k++] = '.';
            for (int j = 8; j < 11 && e[j] != ' '; j++) n83[k++] = (char)e[j];
        }
        n83[k] = 0;
        char path[64];
        if (folder) snprintf(path, sizeof(path), "/%s/%s", folder, n83);
        else        snprintf(path, sizeof(path), "/%s", n83);
        bool more = cb(path, lfn[0] ? lfn : n83, rd32(e + 28), ctx);
        lfn[0] = 0;
        if (!more) return true;
    }
    return false;
}

static bool list_folder(uint32_t cluster, const char *folder, int depth,
                        romfs_list_cb cb, void *ctx) {
    char lfn[64];
    lfn[0] = 0;
    for (int guard = 0; cluster_ok(cluster) && guard < 4096; guard++) {
        if (list_entries(vol + cluster_ofs(cluster), clusterBytes / 32, folder, depth, cb, ctx, lfn, sizeof(lfn)))
            return true;
        cluster = fat_next(cluster);
        if (fat_eoc(cluster)) break;
    }
    return false;
}

#ifdef A8_SD
/* ── the SD card (A8_SD; the Fruit Jam's microSD slot) ─────────────────────
 * With a card in at start-up the games come from it, else from the flash
 * drive (USB ROM drive mode still writes the flash drive). Through the
 * core's SD library: SPI (SPI0 - the slot's pins on the Fruit Jam) at
 * A8_SD_SPI_MHZ, or its SDIO mode (A8_SD_SDIO) on the same pins.
 * Paths are the card's own (long names). A name alone ("/ATARIOSB.ROM") is
 * looked up as on the flash drive: the root, ROMS, ATARI, then any folder.
 * A file up to A8_SD_CACHE_SIZE is loaded into PSRAM when it's opened or
 * mapped (romfs_map), and run from there - one after the other in the area
 * (an OS ROM, the cartridge, a disk...), emptied when a game is picked
 * (romfs_cache_reset): a file the core still uses is never overwritten. A
 * bigger one (a hard-disk sized disk image) is read from the card as the
 * core asks (romfs_fopen_stream). A8_SD_CACHE_SIZE 0: every file that way
 * (a device with no PSRAM to spare).
 * TODO: the card in USB drive mode (as PicoCartReader), and disk writes. */
#include <SD.h>
#include <SPI.h>
#ifndef A8_SD_SPI_MHZ
#define A8_SD_SPI_MHZ 25
#endif
#ifndef A8_SD_CACHE_SIZE
#define A8_SD_CACHE_SIZE (4u * 1024u * 1024u)
#endif
static bool sd_on = false;
#if A8_SD_CACHE_SIZE > 0
PSRAM static uint8_t sd_cache[A8_SD_CACHE_SIZE];
#endif
static const int SD_CACHED = 8;                /* files in the area at once */
static struct { char path[128]; uint32_t at, size; } sd_files[SD_CACHED];
static int sd_nfiles = 0;
static uint32_t sd_used = 0;
static const int SD_LIST_DEPTH = 3;           /* folders below the root, as the flash drive's */

static bool sd_begin(void) {
#ifdef A8_SD_SDIO
    const bool ok = SD.begin(PIN_SD_CLK, PIN_SD_CMD_MOSI, PIN_SD_DAT0_MISO);
    Serial.printf("[romfs] SD card (SDIO): %s%c", ok ? "found" : "none", 10);
#else
    SPI.setRX(PIN_SD_DAT0_MISO);
    SPI.setTX(PIN_SD_CMD_MOSI);
    SPI.setSCK(PIN_SD_CLK);
    const bool ok = SD.begin(PIN_SD_DAT3_CS, SD_SCK_MHZ(A8_SD_SPI_MHZ), SPI);
    Serial.printf("[romfs] SD card (SPI, %d MHz): %s%c", A8_SD_SPI_MHZ, ok ? "found" : "none", 10);
#endif
    return ok;
}

/* A file `name` in folder `dir` (path, "" = root), folders `depth` deep. */
static bool sd_search(const char *dir, const char *name, int depth, char *out, size_t cap) {
    File d = SD.open(dir[0] ? dir : "/");
    if (!d || !d.isDirectory()) return false;
    bool found = false;
    char sub[128];
    for (File e = d.openNextFile(); e && !found; e = d.openNextFile()) {
        const char *nm = e.name();
        if (e.isDirectory()) {
            if (depth > 0 && nm[0] != '.' && strcasecmp(nm, "System Volume Information")) {
                snprintf(sub, sizeof(sub), "%s/%s", dir, nm);
                found = sd_search(sub, name, depth - 1, out, cap);
            }
        } else if (!strcasecmp(nm, name)) {
            snprintf(out, cap, "%s/%s", dir, nm);
            found = true;
        }
        e.close();
    }
    d.close();
    return found;
}

/* The card's path for `path`: as it is, or - a name alone - found by name. */
static bool sd_find(const char *path, char *out, size_t cap) {
    if (SD.exists(path)) { snprintf(out, cap, "%s", path); return true; }
    const char *p = path[0] == '/' ? path + 1 : path;
    if (strchr(p, '/')) return false;
    static const char *const dirs[] = { "/ROMS", "/ATARI" };
    for (const char *dn : dirs) {
        char q[128];
        snprintf(q, sizeof(q), "%s/%s", dn, p);
        if (SD.exists(q)) { snprintf(out, cap, "%s", q); return true; }
    }
    return sd_search("", p, SD_LIST_DEPTH, out, cap);
}

static bool sd_list_dir(const char *dir, int depth, romfs_list_cb cb, void *ctx) {
    File d = SD.open(dir[0] ? dir : "/");
    if (!d || !d.isDirectory()) return true;
    bool more = true;
    char path[128];
    for (File e = d.openNextFile(); e && more; e = d.openNextFile()) {
        const char *nm = e.name();
        if (nm[0] == '.' || !strcasecmp(nm, "System Volume Information")) { e.close(); continue; }
        if (snprintf(path, sizeof(path), "%s/%s", dir, nm) >= (int)sizeof(path)) { e.close(); continue; }
        const bool is_dir = e.isDirectory();
        const uint32_t size = (uint32_t)e.size();
        e.close();
        if (is_dir) { if (depth < SD_LIST_DEPTH) more = sd_list_dir(path, depth + 1, cb, ctx); }
        else more = cb(path, nm, size, ctx);
    }
    d.close();
    return more;
}

static bool sd_map(const char *path, const uint8_t **out, size_t *out_sz) {
#if A8_SD_CACHE_SIZE > 0
    char p[128];
    if (!sd_find(path, p, sizeof(p))) return false;
    for (int i = 0; i < sd_nfiles; i++)
        if (!strcmp(sd_files[i].path, p)) { *out = sd_cache + sd_files[i].at; *out_sz = sd_files[i].size; return true; }
    File f = SD.open(p, FILE_READ);
    if (!f) return false;
    const uint32_t size = (uint32_t)f.size();
    const uint32_t at = (sd_used + 15) & ~15u;
    if (size == 0 || sd_nfiles >= SD_CACHED || at + size > A8_SD_CACHE_SIZE) { f.close(); return false; }
    const uint32_t t0 = millis();
    const size_t got = f.read(sd_cache + at, size);
    f.close();
    if (got != size) { Serial.printf("[romfs] %s: short read%c", p, 10); return false; }
    snprintf(sd_files[sd_nfiles].path, sizeof(sd_files[0].path), "%s", p);
    sd_files[sd_nfiles].at = at;
    sd_files[sd_nfiles].size = size;
    sd_nfiles++;
    sd_used = at + size;
    if (size > 64 * 1024)
        Serial.printf("[romfs] %s: %lu bytes from the card in %lu ms%c", p, (unsigned long)size,
                      (unsigned long)(millis() - t0), 10);
    *out = sd_cache + at;
    *out_sz = size;
    return true;
#else
    (void)path; (void)out; (void)out_sz;
    return false;
#endif
}

static bool sd_read(const char *path, uint8_t *buf, size_t cap, size_t *out_sz) {
    char p[128];
    if (!sd_find(path, p, sizeof(p))) return false;
    File f = SD.open(p, FILE_READ);
    if (!f) return false;
    const size_t size = f.size();
    const bool ok = size && size <= cap && f.read(buf, size) == size;
    f.close();
    if (ok) *out_sz = size;
    return ok;
}

static bool sd_peek(const char *path, long ofs, uint8_t *buf, size_t n, size_t *size) {
    char p[128];
    if (!sd_find(path, p, sizeof(p))) return false;
    File f = SD.open(p, FILE_READ);
    if (!f) return false;
    const long sz = (long)f.size();
    const long at = ofs < 0 ? sz + ofs : ofs;
    const bool ok = at >= 0 && at + (long)n <= sz && f.seek((uint32_t)at) && f.read(buf, n) == n;
    f.close();
    if (ok) *size = (size_t)sz;
    return ok;
}

/* A big file as a stdio stream over the card (fopencookie). newlib has it,
 * but its header shows it only with _GNU_SOURCE set before stdio.h (which
 * Arduino.h includes first): declared here, its functions' struct laid out
 * as newlib's cookie_io_functions_t (read, write, seek, close). */
#ifdef __LARGE64_FILES
typedef _off64_t sd_off_t;
#else
typedef off_t sd_off_t;
#endif
struct SdCookieIo {
    ssize_t (*read)(void *, char *, size_t);
    ssize_t (*write)(void *, const char *, size_t);
    int (*seek)(void *, sd_off_t *, int);
    int (*close)(void *);
};
extern "C" FILE *fopencookie(void *cookie, const char *mode, SdCookieIo functions);
struct SdCookie { File f; };
static ssize_t sd_ck_read(void *c, char *buf, size_t n) {
    return (ssize_t)((SdCookie *)c)->f.read((uint8_t *)buf, n);
}
static int sd_ck_seek(void *c, sd_off_t *pos, int whence) {
    File &f = ((SdCookie *)c)->f;
    long p = (long)*pos;
    if (whence == SEEK_CUR) p += (long)f.position();
    else if (whence == SEEK_END) p += (long)f.size();
    if (p < 0 || !f.seek((uint32_t)p)) return -1;
    *pos = p;
    return 0;
}
static int sd_ck_close(void *c) {
    SdCookie *k = (SdCookie *)c;
    k->f.close();
    delete k;
    return 0;
}

extern "C" FILE *romfs_fopen_stream(const char *path) {
    if (!sd_on) return nullptr;
    char p[128];
    if (!sd_find(path, p, sizeof(p))) return nullptr;
    SdCookie *k = new SdCookie;
    k->f = SD.open(p, FILE_READ);
    if (!k->f) { delete k; return nullptr; }
    SdCookieIo io = { sd_ck_read, nullptr, sd_ck_seek, sd_ck_close };
    FILE *f = fopencookie(k, "rb", io);
    if (!f) { k->f.close(); delete k; return nullptr; }
    Serial.printf("[romfs] %s: %lu bytes, read from the card as it goes%c", p,
                  (unsigned long)k->f.size(), 10);
    return f;
}

extern "C" void romfs_cache_reset(void) { sd_nfiles = 0; sd_used = 0; }
extern "C" bool romfs_on_card(void) { return sd_on; }
#else
extern "C" FILE *romfs_fopen_stream(const char *path) { (void)path; return nullptr; }
extern "C" void romfs_cache_reset(void) {}
extern "C" bool romfs_on_card(void) { return false; }
#endif

/* The card (A8_SD) if there's one, and the flash drive (USB ROM drive mode
 * writes it, and it holds the games when there's no card). */
extern "C" bool romfs_init(void) {
#ifdef A8_SD
    sd_on = sd_begin();
    const bool flash_ok = romfs_init_flash();
    if (sd_on) Serial.printf("[romfs] games: the SD card%c", 10);
    return sd_on || flash_ok;
#else
    return romfs_init_flash();
#endif
}

extern "C" void romfs_list(romfs_list_cb cb, void *ctx) {
#ifdef A8_SD
    if (sd_on) { sd_list_dir("", 0, cb, ctx); return; }
#endif
    if (!vol) return;
    char lfn[64];
    lfn[0] = 0;
    list_entries(vol + rootOfs, rootEntries, nullptr, 0, cb, ctx, lfn, sizeof(lfn));
}

extern "C" bool romfs_exists(const char *path) {
#ifdef A8_SD
    if (sd_on) { char p[128]; return sd_find(path, p, sizeof(p)); }
#endif
    return find_file(path) != nullptr;
}

extern "C" bool romfs_map(const char *path, const uint8_t **out, size_t *out_sz) {
#ifdef A8_SD
    if (sd_on) return sd_map(path, out, out_sz);
#endif
    const uint8_t *e = find_file(path);
    if (!e) return false;
    uint32_t size = rd32(e + 28), cluster = rd16(e + 26), first = cluster;
    if (size == 0 || size > volSize || !cluster_ok(cluster)) return false;
    /* contiguous: each cluster follows the one before */
    for (uint32_t done = clusterBytes; done < size; done += clusterBytes) {
        uint32_t next = fat_next(cluster);
        if (next != cluster + 1 || !cluster_ok(next)) return false;
        cluster = next;
    }
    *out = vol + cluster_ofs(first);
    *out_sz = size;
    return true;
}

extern "C" bool romfs_read(const char *path, uint8_t *buf, size_t cap, size_t *out_sz) {
#ifdef A8_SD
    if (sd_on) return sd_read(path, buf, cap, out_sz);
#endif
    const uint8_t *e = find_file(path);
    if (!e) return false;
    uint32_t size = rd32(e + 28), cluster = rd16(e + 26);
    if (size == 0 || size > cap) return false;
    for (uint32_t done = 0; done < size; ) {
        if (!cluster_ok(cluster)) return false;
        uint32_t n = size - done;
        if (n > clusterBytes) n = clusterBytes;
        memcpy(buf + done, vol + cluster_ofs(cluster), n);
        done += n;
        if (done < size) cluster = fat_next(cluster);
    }
    *out_sz = size;
    return true;
}

extern "C" bool romfs_slurp(const char *path, uint8_t **out, size_t *out_sz) {
#ifdef A8_SD
    if (sd_on) {                             /* (into RAM: small files only - the heap is small) */
        char p[128];
        if (!sd_find(path, p, sizeof(p))) { Serial.printf("[romfs] not found: %s\n", path); return false; }
        File f = SD.open(p, FILE_READ);
        if (!f) return false;
        const size_t size = f.size();
        uint8_t *buf = size ? (uint8_t *)malloc(size) : nullptr;
        const bool ok = buf && f.read(buf, size) == size;
        f.close();
        if (!ok) { free(buf); return false; }
        *out = buf; *out_sz = size;
        return true;
    }
#endif
    const uint8_t *e = find_file(path);
    if (!e) { Serial.printf("[romfs] not found: %s\n", path); return false; }
    uint32_t size = rd32(e + 28), cluster = rd16(e + 26);
    if (size == 0 || size > volSize) return false;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) { Serial.printf("[romfs] malloc %lu bytes failed\n", (unsigned long)size); return false; }
    /* Copy cluster by cluster (the file may be fragmented). */
    uint32_t done = 0;
    while (done < size) {
        if (!cluster_ok(cluster)) { free(buf); Serial.println("[romfs] broken cluster chain"); return false; }
        uint32_t n = size - done;
        if (n > clusterBytes) n = clusterBytes;
        memcpy(buf + done, vol + cluster_ofs(cluster), n);
        done += n;
        if (done < size) cluster = fat_next(cluster);
    }
    *out = buf; *out_sz = size;
    return true;
}

/* Every flash board's drive is labelled ARCADE ROMS (the Quad's first drives
 * were "QUAD ROMS"; only the label differs). */
extern "C" const char *romfs_where(void) {
#ifdef A8_SD
    if (sd_on) return "the SD card";
#endif
    return "the ARCADE ROMS drive";
}

extern "C" bool romfs_peek(const char *path, long ofs, uint8_t *buf, size_t n, size_t *size) {
#ifdef A8_SD
    if (sd_on) return sd_peek(path, ofs, buf, n, size);
#endif
    const uint8_t *p;                        /* in place (as the list looked before) */
    size_t sz;
    if (!romfs_map(path, &p, &sz)) return false;
    const long at = ofs < 0 ? (long)sz + ofs : ofs;
    if (at < 0 || at + (long)n > (long)sz) return false;
    memcpy(buf, p + at, n);
    *size = sz;
    return true;
}
#endif
