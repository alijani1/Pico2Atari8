/* a8_unzip_core.h - a zip's Atari file, inflated in 4 KB pieces (plain C). */
#ifndef A8_UNZIP_CORE_H
#define A8_UNZIP_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A8Z_SECTOR 4096     /* piece size (one flash sector) */
#define A8Z_DICT   32768    /* deflate window */

enum { A8Z_OK = 0, A8Z_BAD = -1, A8Z_METHOD = -2 };

typedef struct {
    const uint8_t *data;    /* compressed bytes */
    uint32_t csize, usize;
    unsigned method;        /* 0 stored, 8 deflate */
    char name[64];          /* without folders */
} a8z_entry;

/* The first .xex .com .exe .car .rom .bin .atr .xfd in the zip. */
int a8z_find(const uint8_t *zip, size_t size, a8z_entry *out);

/* Inflate it: put() gets each 4 KB piece in order (the last one padded with
 * 0xFF). dict: A8Z_DICT bytes, sector: A8Z_SECTOR bytes. */
int a8z_extract(const a8z_entry *e, uint8_t *dict, uint8_t *sector,
                void (*put)(uint32_t index, const uint8_t *data, void *ctx), void *ctx,
                uint32_t *written);

#ifdef __cplusplus
}
#endif

#endif
