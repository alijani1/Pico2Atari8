/* a8_unzip_core.c - find a zip's Atari file and inflate it in 4 KB pieces.
 *
 * Plain C with no Pico dependencies, so the PC test (tools/host/ziptest.c)
 * runs exactly this code. a8_zip.cpp supplies the buffers and writes each
 * piece to flash.
 */
#include <string.h>
#include <strings.h>
#include "a8_unzip_core.h"
#include "uzlib.h"

static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int game_ext(const char *name, int len) {
    static const char *const exts[] = { ".xex", ".com", ".exe", ".car", ".rom",
                                        ".bin", ".atr", ".xfd" };
    for (unsigned i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        int n = (int)strlen(exts[i]);
        if (len > n && strncasecmp(name + len - n, exts[i], n) == 0) return 1;
    }
    return 0;
}

/* Notes and pictures that often come with a game */
static int doc_ext(const char *name, int len) {
    static const char *const exts[] = { ".txt", ".nfo", ".diz", ".doc", ".pdf",
                                        ".md", ".htm", ".html", ".jpg", ".png", ".gif" };
    for (unsigned i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        int n = (int)strlen(exts[i]);
        if (len > n && strncasecmp(name + len - n, exts[i], n) == 0) return 1;
    }
    return 0;
}

/* The central directory and its entry count, or NULL. */
static const uint8_t *central_dir(const uint8_t *zip, size_t size, uint32_t *count) {
    /* end of central directory: in the last 64 KB + 22 bytes */
    if (size < 22) return NULL;
    size_t lo = size > 65557 ? size - 65557 : 0;
    const uint8_t *eocd = NULL;
    for (size_t i = size - 22 + 1; i-- > lo; )
        if (rd32(zip + i) == 0x06054b50u) { eocd = zip + i; break; }
    if (!eocd) return NULL;
    uint32_t cd = rd32(eocd + 16);
    if (cd >= size) return NULL;
    *count = rd16(eocd + 10);
    return zip + cd;
}

int a8z_is_game(const uint8_t *zip, size_t size) {
    uint32_t count;
    const uint8_t *p = central_dir(zip, size, &count);
    if (!p) return 0;
    int cart_like = 0, files = 0;
    for (uint32_t i = 0; i < count && p + 46 <= zip + size; i++) {
        if (rd32(p) != 0x02014b50u) return 0;
        unsigned nlen = rd16(p + 28), elen = rd16(p + 30), clen = rd16(p + 32);
        const char *name = (const char *)(p + 46);
        if (p + 46 + nlen > zip + size) return 0;
        if (nlen > 0 && name[nlen - 1] != '/' && !doc_ext(name, (int)nlen)) {   /* not a folder or notes */
            files++;
            if (game_ext(name, (int)nlen)) {
                if ((nlen > 4 && strncasecmp(name + nlen - 4, ".bin", 4) == 0) ||
                    (nlen > 4 && strncasecmp(name + nlen - 4, ".rom", 4) == 0))
                    cart_like++;
                else
                    return 1;                             /* .xex .atr .car ... */
            }
        }
        p += 46 + nlen + elen + clen;
    }
    /* a .bin / .rom counts only alone (notes aside): arcade zips hold many
     * ROM chips (Donkey Kong's are .bin) */
    return cart_like == 1 && files == 1;
}

int a8z_find(const uint8_t *zip, size_t size, a8z_entry *out) {
    uint32_t count;
    const uint8_t *p = central_dir(zip, size, &count);
    if (!p) return 0;
    for (uint32_t i = 0; i < count && p + 46 <= zip + size; i++) {
        if (rd32(p) != 0x02014b50u) return 0;
        unsigned method = rd16(p + 10), nlen = rd16(p + 28), elen = rd16(p + 30), clen = rd16(p + 32);
        uint32_t csize = rd32(p + 20), usize = rd32(p + 24), lho = rd32(p + 42);
        const char *name = (const char *)(p + 46);
        if (game_ext(name, (int)nlen) && lho + 30 <= size) {
            const uint8_t *lh = zip + lho;
            if (rd32(lh) != 0x04034b50u) return 0;
            out->data = lh + 30 + rd16(lh + 26) + rd16(lh + 28);
            out->csize = csize;
            out->usize = usize;
            out->method = method;
            const char *base = name;              /* without any folder part */
            for (unsigned k = 0; k < nlen; k++) if (name[k] == '/' || name[k] == '\\') base = name + k + 1;
            size_t bl = nlen - (size_t)(base - name);
            if (bl > sizeof(out->name) - 1) bl = sizeof(out->name) - 1;
            memcpy(out->name, base, bl);
            out->name[bl] = 0;
            return out->data + csize <= zip + size;
        }
        p += 46 + nlen + elen + clen;
    }
    return 0;
}

int a8z_extract(const a8z_entry *e, uint8_t *dict, uint8_t *sector,
                void (*put)(uint32_t index, const uint8_t *data, void *ctx), void *ctx,
                uint32_t *written_out) {
    uint32_t written = 0, index = 0;
    if (e->method == 0) {                                   /* stored */
        if (e->csize != e->usize) return A8Z_BAD;
        while (written < e->usize) {
            uint32_t n = e->usize - written;
            if (n > A8Z_SECTOR) n = A8Z_SECTOR;
            memset(sector, 0xFF, A8Z_SECTOR);
            memcpy(sector, e->data + written, n);
            put(index++, sector, ctx);
            written += n;
        }
    } else if (e->method == 8) {                            /* deflate */
        TINF_DATA d;
        memset(&d, 0, sizeof(d));
        d.source = e->data;
        d.source_limit = e->data + e->csize;
        uzlib_uncompress_init(&d, dict, A8Z_DICT);
        for (;;) {
            d.dest = d.dest_start = sector;
            d.dest_limit = sector + A8Z_SECTOR;
            int rc = uzlib_uncompress(&d);
            uint32_t n = (uint32_t)(d.dest - sector);
            if (rc != TINF_OK && rc != TINF_DONE) { *written_out = written; return A8Z_BAD; }
            if (n) {
                if (n < A8Z_SECTOR) memset(sector + n, 0xFF, A8Z_SECTOR - n);
                put(index++, sector, ctx);
                written += n;
            }
            if (rc == TINF_DONE || written >= e->usize) break;
        }
    } else {
        return A8Z_METHOD;
    }
    *written_out = written;
    return written == e->usize ? A8Z_OK : A8Z_BAD;
}
