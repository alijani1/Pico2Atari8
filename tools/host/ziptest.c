/* ziptest.c - run the firmware's unzip code (src/a8_unzip_core.c) on the PC.
 *
 * Inflates the Atari file in each zip given, in 4 KB pieces exactly as the
 * device does (into a "flash" buffer instead of flash), and writes it out so
 * it can be compared with a reference unzip.
 *
 * Usage: ziptest <zip> <out file>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a8_unzip_core.h"

static uint8_t flash[128 * 1024];

static void put(uint32_t index, const uint8_t *data, void *ctx) {
    (void)ctx;
    if ((index + 1) * A8Z_SECTOR > sizeof(flash)) { printf("too big for the area\n"); exit(3); }
    memcpy(flash + index * A8Z_SECTOR, data, A8Z_SECTOR);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <zip> <out>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    static uint8_t zip[1 << 20];
    size_t n = fread(zip, 1, sizeof(zip), f);
    fclose(f);
    printf("listed as an Atari game: %s\n", a8z_is_game(zip, n) ? "yes" : "no");
    a8z_entry e;
    if (!a8z_find(zip, n, &e)) { printf("no Atari file\n"); return 2; }
    static uint8_t dict[A8Z_DICT], sector[A8Z_SECTOR];
    uint32_t written = 0;
    int rc = a8z_extract(&e, dict, sector, put, NULL, &written);
    printf("%s: method %u, %u -> %u, rc %d, written %u\n", e.name, e.method, e.csize, e.usize, rc, written);
    if (rc != A8Z_OK) return 2;
    FILE *o = fopen(argv[2], "wb");
    fwrite(flash, 1, e.usize, o);
    fclose(o);
    return 0;
}
