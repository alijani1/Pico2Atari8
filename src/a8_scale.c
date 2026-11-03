/* a8_scale.c - see a8_scale.h.
 *
 * Each output pixel is the weighted sum of up to four source lines (the
 * mode's taps, weights adding up to 16) times the two pixels of its column
 * pair. The colours are "spread" into one 32-bit word - green in the top
 * half, red and blue in the bottom (0x07E0F81F), as Pico2Arcade's scaler
 * does - so one multiply-add weighs all three: with 16 x 2 = 32 parts, each
 * field still fits below the next, and one shift by 5 divides them all. The
 * palette is spread once (256 words), so a source pixel is one lookup.
 * DROP needs no arithmetic: one lookup in the panel palette per pixel.
 */
#include "a8_scale.h"
#include <string.h>

#ifdef ARDUINO
#include <pico.h>
#define HOT(f) __not_in_flash_func(f)     /* core 1 runs this; core 0 owns the flash cache */
#else
#define HOT(f) f
#endif

const char *const a8_scale_names[A8_SCALE_MODES] = { "SMOOTH", "SHARP", "DROP", "CROP" };

static const uint32_t SPREAD_M = 0x07E0F81Fu;
static const uint32_t RND32 = 16u | (16u << 11) | (16u << 21);   /* +0.5 per field, before >> 5 */

static uint32_t pal_sp[256];               /* spread colours */
static uint16_t pal_px[2][256];            /* RGB565 as is / byte-swapped (DROP) */

/* Per output row: up to 4 source lines and their weights (sum 16). */
enum { MAX_TAPS = 4 };
static uint8_t tap_row[A8_TINY_H][MAX_TAPS], tap_w[A8_TINY_H][MAX_TAPS], tap_n[A8_TINY_H];
static int taps_mode = -1;

/* The lines shown: 8-223. Most games draw the normal 192-line playfield
 * (24-215), but some reach further - Shamus II from line 8, Blue Max from
 * 12, Star Raiders' panel and Jumpman's floor to 217-219 - so the window is
 * wider: 216 lines into 80 rows, 2.7:1. CROP shows the middle 160 at 2:1. */
static const int FIELD_Y = 8, FIELD_H = 216;
static const int CROP_Y = FIELD_Y + (FIELD_H - 2 * A8_TINY_H) / 2;   /* 36-195 */

void a8_scale_palette(const uint16_t pal565[256]) {
    for (int i = 0; i < 256; i++) {
        uint32_t v = pal565[i];
        pal_sp[i] = (v | (v << 16)) & SPREAD_M;
        pal_px[0][i] = (uint16_t)v;
        pal_px[1][i] = (uint16_t)((v >> 8) | (v << 8));
    }
}

/* Positions in 1/80ths of a line (U): output row oy covers source lines
 * [a, a + FIELD_H) of them, a line r is [80 r, 80 r + 80). */
static void build_taps(int mode) {
    const int U = A8_TINY_H;
    for (int oy = 0; oy < A8_TINY_H; oy++) {
        int n = 0;
        const int a = FIELD_Y * U + oy * FIELD_H, b = a + FIELD_H, c = a + FIELD_H / 2;
        if (mode == A8_SCALE_CROP) {                 /* 2:1 */
            int r = CROP_Y + 2 * oy;
            tap_row[oy][0] = (uint8_t)r;       tap_w[oy][0] = 8;
            tap_row[oy][1] = (uint8_t)(r + 1); tap_w[oy][1] = 8;
            n = 2;
        } else if (mode == A8_SCALE_DROP) {           /* the line at its middle */
            tap_row[oy][0] = (uint8_t)(c / U); tap_w[oy][0] = 16;
            n = 1;
        } else if (mode == A8_SCALE_SHARP) {          /* the two lines whose middles are nearest */
            int r0 = (c - U / 2) / U;
            tap_row[oy][0] = (uint8_t)r0;       tap_w[oy][0] = 8;
            tap_row[oy][1] = (uint8_t)(r0 + 1); tap_w[oy][1] = 8;
            n = 2;
        } else {                                      /* SMOOTH: by how much of each it covers */
            int sum = 0, big = 0;
            for (int r = a / U; r * U < b && n < MAX_TAPS; r++) {
                int lo = r * U > a ? r * U : a, hi = r * U + U < b ? r * U + U : b;
                int w = ((hi - lo) * 16 + FIELD_H / 2) / FIELD_H;
                if (w == 0) continue;
                tap_row[oy][n] = (uint8_t)r; tap_w[oy][n] = (uint8_t)w;
                if (w > tap_w[oy][big]) big = n;
                sum += w; n++;
            }
            tap_w[oy][big] = (uint8_t)(tap_w[oy][big] + 16 - sum);   /* exactly 16 */
        }
        tap_n[oy] = (uint8_t)n;
    }
    taps_mode = mode;
}

void HOT(a8_scale_rows)(const uint8_t *src, uint16_t *dst, int mode, int swap, int oy0, int oy1) {
    if (mode < 0 || mode >= A8_SCALE_MODES) mode = A8_SCALE_SMOOTH;
    if (mode != taps_mode) build_taps(mode);
    swap = swap ? 1 : 0;
    for (int oy = oy0; oy < oy1; oy++) {
        uint16_t *out = dst + (oy - oy0) * A8_TINY_W;
        if (mode == A8_SCALE_DROP) {
            const uint8_t *s = src + tap_row[oy][0] * A8_SCALE_SRC_W;
            const uint16_t *px = pal_px[swap];
            for (int x = 0; x < A8_TINY_W; x++) out[x] = px[s[2 * x]];
            continue;
        }
        const int n = tap_n[oy];
        const uint8_t *s[MAX_TAPS];
        uint32_t w[MAX_TAPS];
        for (int t = 0; t < n; t++) { s[t] = src + tap_row[oy][t] * A8_SCALE_SRC_W; w[t] = tap_w[oy][t]; }
        for (int x = 0; x < A8_TINY_W; x++) {
            uint32_t acc = RND32;
            for (int t = 0; t < n; t++)
                acc += w[t] * (pal_sp[s[t][2 * x]] + pal_sp[s[t][2 * x + 1]]);
            acc = (acc >> 5) & SPREAD_M;
            uint16_t v = (uint16_t)(acc | (acc >> 16));
            out[x] = swap ? (uint16_t)((v >> 8) | (v << 8)) : v;
        }
    }
}

void a8_scale_frame(const uint8_t *src, uint16_t *dst, int mode, int swap) {
    a8_scale_rows(src, dst, mode, swap, 0, A8_TINY_H);
}
