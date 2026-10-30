/* a8_display.cpp - the Atari screen on the 320x240 TFT.
 *
 * atari800 renders each frame as 8-bit colour indices into Screen_atari
 * (384 x 240: the full overscan width). The normal playfield is the middle
 * 320 columns, which is exactly the panel's width in landscape, so the
 * picture is shown 1:1 with the side borders cropped.
 *
 * Core 0 runs the emulator; at the end of each frame a8_display_frame()
 * copies those 320 x 240 pixels into a snapshot (a fraction of a ms) and
 * core 1 turns the snapshot into RGB565 bands and DMA-pushes them, while
 * core 0 is already emulating the next frame. If core 1 is still sending
 * the previous one, the new frame is skipped rather than holding up the
 * emulation (and with it the sound).
 *
 * Only rows that changed are sent: a full 320 x 240 frame is 12.3 ms of
 * SPI at 100 MHz (plus overhead) - too close to the 16.7 ms of a frame -
 * but most of an Atari screen usually stays put. Core 1 hashes each row
 * and sends the runs of rows whose hash differs from what the panel shows.
 *
 * FPS overlay: "E60 S58" (emulator / screen frames a second) is drawn into
 * the snapshot's top-right corner before hashing, so it travels with the
 * picture. On / off: Settings > FPS display (a8_display_set_fps).
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "a8_display.h"
#include "a8_platform.h"
#include "a8_osk.h"

extern "C" {
#include "screen.h"
#include "colours.h"
}

static TFT_eSPI tft;

static const int SCR_W = 320, SCR_H = 240;
static const int SRC_X = (Screen_WIDTH - SCR_W) / 2;    /* 32: middle 320 columns */
static const int BAND = 16;                             /* lines per DMA push */

static uint8_t  frame8[SCR_W * SCR_H] __attribute__((aligned(4)));   /* snapshot for core 1 */
static uint16_t band565[2][SCR_W * BAND];               /* ping-pong DMA buffers */
static uint16_t pal565[256];                            /* byte-swapped for the panel */
static volatile bool frame_ready = false;               /* core 0 -> core 1 */
static volatile bool running = false;                   /* core 1 owns the panel */
static volatile uint32_t frames_shown = 0, frames_skipped = 0;
static volatile uint32_t push_us_sum = 0, push_count = 0;   /* time per push */
static volatile uint32_t rows_sum = 0;                      /* rows sent per push */
static uint32_t row_hash[SCR_H];                        /* what the panel shows */
static bool hashes_valid = false;

/* ---- FPS overlay ----------------------------------------------------- */

static volatile bool fps_on = false;     /* Settings > FPS display */
static volatile uint32_t frames_emulated = 0;          /* every frame core 0 finished */
static int fps_emu = 0, fps_scr = 0;                    /* last second's rates */

/* 5 x 7 glyphs, one byte per row, bit 4 = left column. */
static const uint8_t GLYPH_DIGIT[10][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   /* 9 */
};
static const uint8_t GLYPH_E[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
static const uint8_t GLYPH_S[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
/* 5 x 7 letters and punctuation (messages, the on-screen keyboard) */
static const struct { char c; uint8_t g[7]; } GLYPH_LETTERS[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { ';', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08 } },
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
};
static const uint8_t *glyph(char c) {
    if (c >= '0' && c <= '9') return GLYPH_DIGIT[c - '0'];
    for (const auto &l : GLYPH_LETTERS) if (l.c == c) return l.g;
    return nullptr;                       /* space / unknown: blank */
}

static char toast_text[12];
static volatile uint32_t toast_until = 0;

void a8_display_toast(const char *text) {
    strncpy(toast_text, text, sizeof(toast_text) - 1);
    toast_text[sizeof(toast_text) - 1] = 0;
    toast_until = millis() + 2000;
}

static const uint8_t OV_BG = 0x00, OV_FG = 0x0F;       /* Atari black / white */
static const int OV_SCALE = 2, OV_CHAR_W = 6 * OV_SCALE, OV_H = 7 * OV_SCALE + 4;

static void __not_in_flash_func(ov_glyph_s)(int x, int y, const uint8_t *g, int scale, uint8_t colour) {
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r] & (0x10 >> c))
                for (int dy = 0; dy < scale; dy++)
                    memset(frame8 + (y + r * scale + dy) * SCR_W + x + c * scale, colour, scale);
}

static void __not_in_flash_func(ov_glyph)(int x, int y, const uint8_t *g) {
    ov_glyph_s(x, y, g, OV_SCALE, OV_FG);
}

/* ---- on-screen keyboard (a8_osk.cpp) ------------------------------------
 * Over the bottom of the picture: 5 rows of 10 grid columns, 32 x 20 px a
 * column. Single characters are drawn at twice size, labels (ESC, DEL,
 * SPACE, RET) at normal size. Atari palette indices: black frame, grey
 * keys, blue cursor, white text. */
static const int OSK_CELL_W = SCR_W / A8_OSK_COLS, OSK_CELL_H = 20;
static const int OSK_Y = SCR_H - A8_OSK_ROWS * OSK_CELL_H - 2;
static const uint8_t OSK_FRAME = 0x00, OSK_KEY = 0x04, OSK_CURSOR = 0x96, OSK_TEXT = 0x0F;

static void __not_in_flash_func(draw_osk)(void) {
    for (int y = OSK_Y - 2; y < SCR_H; y++) memset(frame8 + y * SCR_W, OSK_FRAME, SCR_W);
    int cr, ci;
    a8_osk_cursor(&cr, &ci);
    for (int r = 0; r < A8_OSK_ROWS; r++) {
        int n;
        const A8Key *keys = a8_osk_row(r, &n);
        int col = 0;
        for (int i = 0; i < n; i++) {
            int x0 = col * OSK_CELL_W + 1, w = keys[i].span * OSK_CELL_W - 2;
            int y0 = OSK_Y + r * OSK_CELL_H + 1, h = OSK_CELL_H - 2;
            uint8_t face = (r == cr && i == ci) ? OSK_CURSOR : OSK_KEY;
            for (int y = y0; y < y0 + h; y++) memset(frame8 + y * SCR_W + x0, face, w);
            const char *t = keys[i].label;
            int len = (int)strlen(t), scale = len == 1 ? 2 : 1;
            int tw = len * 6 * scale - scale, th = 7 * scale;
            int tx = x0 + (w - tw) / 2, ty = y0 + (h - th) / 2;
            for (int k = 0; k < len; k++) {
                const uint8_t *g = glyph(t[k]);
                if (g) ov_glyph_s(tx + k * 6 * scale, ty, g, scale, OSK_TEXT);
            }
            col += keys[i].span;
        }
    }
}

/* A short message in the top-left corner of the snapshot. */
static void __not_in_flash_func(draw_toast)(void) {
    int n = (int)strlen(toast_text), w = n * OV_CHAR_W + 2;
    const int x0 = 2, y0 = 2;
    for (int y = y0; y < y0 + OV_H; y++) memset(frame8 + y * SCR_W + x0, OV_BG, w);
    for (int i = 0; i < n; i++) {
        const uint8_t *g = glyph(toast_text[i]);
        if (g) ov_glyph(x0 + 2 + i * OV_CHAR_W, y0 + 2, g);
    }
}

/* "E60 S58" in the top-right corner of the snapshot. */
static void __not_in_flash_func(draw_fps)(void) {
    char txt[12];
    int e = fps_emu > 99 ? 99 : fps_emu, s = fps_scr > 99 ? 99 : fps_scr;
    txt[0] = 'E'; txt[1] = '0' + e / 10; txt[2] = '0' + e % 10; txt[3] = ' ';
    txt[4] = 'S'; txt[5] = '0' + s / 10; txt[6] = '0' + s % 10; txt[7] = 0;
    const int n = 7, w = n * OV_CHAR_W + 2;
    const int x0 = SCR_W - w - 2, y0 = 2;
    for (int y = y0; y < y0 + OV_H; y++) memset(frame8 + y * SCR_W + x0, OV_BG, w);
    for (int i = 0; i < n; i++) {
        const uint8_t *g = txt[i] == 'E' ? GLYPH_E : txt[i] == 'S' ? GLYPH_S :
                           (txt[i] >= '0' && txt[i] <= '9') ? GLYPH_DIGIT[txt[i] - '0'] : nullptr;
        if (g) ov_glyph(x0 + 2 + i * OV_CHAR_W, y0 + 2, g);
    }
}

void a8_display_set_fps(bool on) { fps_on = on; }

/* Core 1, once a second: the rates for the overlay. */
static void fps_tick(void) {
    static uint32_t last_ms = 0, last_emu = 0, last_shown = 0;
    uint32_t now = millis();
    if (now - last_ms < 1000) return;
    uint32_t dt = now - last_ms;
    fps_emu = (int)((frames_emulated - last_emu) * 1000 / dt);
    fps_scr = (int)((frames_shown - last_shown) * 1000 / dt);
    last_emu = frames_emulated; last_shown = frames_shown; last_ms = now;
}

TFT_eSPI &a8_tft(void) { return tft; }

uint8_t *a8_display_scratch(size_t *size) {
    *size = sizeof(frame8);
    return frame8;
}

void a8_display_init(void) {
    /* clk_peri back on clk_sys: platformio's f_cpu override leaves it on
     * the USB PLL (48 MHz), which caps SPI at 24 MHz (as in Pico2Arcade). */
    uint32_t sys_hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz, sys_hz);
    tft.init();
    tft.setRotation(3);                  /* landscape 320 x 240, as the arcade */
    tft.setSwapBytes(false);
    tft.fillScreen(TFT_BLACK);
    if (!tft.initDMA()) Serial.println("[display] initDMA failed");
    Serial.printf("[display] sys %lu MHz, SPI %lu MHz\n", (unsigned long)(sys_hz / 1000000),
                  (unsigned long)(spi_get_baudrate(spi0) / 1000000));
}

void a8_display_palette(void) {
    for (int i = 0; i < 256; i++) {
        int rgb = Colours_table[i];
        uint16_t c = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
        pal565[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}

void a8_display_start(void) {
    a8_display_palette();
    tft.fillScreen(TFT_BLACK);
    hashes_valid = false;                /* send the first frame whole */
    running = true;
}

void a8_display_stop(void) {
    running = false;
    while (frame_ready) tight_loop_contents();
    tft.dmaWait();
}

/* Core 0, end of each emulated frame. */
extern "C" void a8_display_frame(void) {
    if (!running) return;
    frames_emulated++;
    if (frame_ready) { frames_skipped++; return; }   /* core 1 still busy */
    const uint8_t *src = (const uint8_t *)Screen_atari + SRC_X;
    for (int y = 0; y < SCR_H; y++)
        memcpy(frame8 + y * SCR_W, src + y * Screen_WIDTH, SCR_W);
    frame_ready = true;
}

/* Paused: the emulator makes no frames, but the keyboard on top changes.
 * Re-send the last picture (still in Screen_atari) when core 1 is free. */
void a8_display_refresh(void) { a8_display_frame(); }

/* Core 1: send a snapshot. In RAM: core 0 runs the emulator out of flash,
 * and the two cores share the flash cache. */
static uint32_t __not_in_flash_func(hash_row)(const uint8_t *row) {
    const uint32_t *w = (const uint32_t *)row;
    uint32_t h = 2166136261u;
    for (int i = 0; i < SCR_W / 4; i++) h = (h ^ w[i]) * 16777619u;
    return h;
}

/* Send rows y0 .. y0+n-1 (one address window, ping-pong bands). */
static int band_buf = 0;
static void __not_in_flash_func(push_rows)(int y0, int n) {
    tft.dmaWait();                       /* window change: the bus must be idle */
    tft.setAddrWindow(0, y0, SCR_W, n);
    for (int y = y0; y < y0 + n; y += BAND) {
        int rows = (y0 + n - y < BAND) ? y0 + n - y : BAND;
        uint16_t *dst = band565[band_buf];
        const uint8_t *src = frame8 + y * SCR_W;
        for (int i = 0; i < SCR_W * rows; i++) dst[i] = pal565[src[i]];
        tft.dmaWait();                   /* the other buffer is free again */
        tft.pushPixelsDMA(dst, SCR_W * rows);
        band_buf ^= 1;
    }
}

static void __not_in_flash_func(push_frame)(void) {
    uint32_t t0 = time_us_32();
    uint32_t sent = 0;
    if (fps_on) draw_fps();
    if (toast_until && (int32_t)(toast_until - millis()) > 0) draw_toast();
    if (a8_osk_is_open()) draw_osk();
    tft.startWrite();
    int y = 0;
    while (y < SCR_H) {
        uint32_t h = hash_row(frame8 + y * SCR_W);
        if (hashes_valid && h == row_hash[y]) { y++; continue; }
        /* a run of changed rows */
        int start = y;
        row_hash[y++] = h;
        while (y < SCR_H) {
            h = hash_row(frame8 + y * SCR_W);
            if (hashes_valid && h == row_hash[y]) break;
            row_hash[y++] = h;
        }
        push_rows(start, y - start);
        sent += (uint32_t)(y - start);
    }
    tft.dmaWait();
    tft.endWrite();
    hashes_valid = true;
    push_us_sum += time_us_32() - t0;
    rows_sum += sent;
    push_count++;
    frames_shown++;
}

void a8_display_core1_loop(void) {
    if (running) fps_tick();
    if (running && frame_ready) {
        push_frame();
        frame_ready = false;
    }
}

void a8_display_stats(uint32_t *shown, uint32_t *skipped) {
    *shown = frames_shown;
    *skipped = frames_skipped;
}

uint32_t a8_display_push_us(uint32_t *avg_rows) {
    uint32_t n = push_count, s = push_us_sum, r = rows_sum;
    push_count = 0; push_us_sum = 0; rows_sum = 0;
    if (avg_rows) *avg_rows = n ? r / n : 0;
    return n ? s / n : 0;
}
