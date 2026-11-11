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
 *
 * Scale (320x240 panel; a8_scale.h): core 1 rearranges the snapshot in
 * place before the overlays and the hashing - TV / TV SHARP / PIXEL squeeze
 * each line to 240 / 160 pixels, centred, with black at the sides (it was
 * each line's border colour, taken from the overscan's left edge - but
 * that column isn't always border: garbled colour lines on the Quad);
 * STRETCH spreads lines
 * 24-215 over all 240. TV's blend (the middle pixel of each 3) is made as
 * the line is turned into RGB565, not on overlay lines.
 *
 * 160x80 panel (PICO_TINY_SCREEN, the Duo Ultimate): core 1 shrinks the
 * same snapshot to 160x80 (a8_scale.c; Scale mode from the settings) 16
 * rows at a time, each band going out by DMA while the next is shrunk (as
 * Pico2Arcade). The whole frame is sent - 25 KB, ~2 ms at 100 MHz - so no
 * row hashing. The FPS and message overlays are drawn after the shrink, at
 * single size, into the first band.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "a8_display.h"
#include "a8_platform.h"
#include "a8_osk.h"
#include "a8_scale.h"
#include "a8_settings.h"

extern "C" {
#include "screen.h"
#include "colours.h"
}

static TFT_eSPI tft;

static const int SCR_W = 320, SCR_H = 240;
static const int SRC_X = (Screen_WIDTH - SCR_W) / 2;    /* 32: middle 320 columns */
static const int BAND = 16;                             /* lines per DMA push */

#ifdef A8_HDMI
static uint8_t *frame8;                                 /* the menu canvas's pixels (heap, a8_display_init) */
#else
static uint8_t  frame8[SCR_W * SCR_H] __attribute__((aligned(4)));   /* snapshot for core 1 */
#endif
#ifdef PICO_TINY_SCREEN
static uint16_t tiny565[2][A8_TINY_W * BAND];           /* ping-pong bands of the shrunk frame */
#elif !defined(A8_HDMI)
static uint16_t band565[2][SCR_W * BAND];               /* ping-pong DMA buffers */
#endif
static uint16_t pal565[256];                            /* byte-swapped for the panel */
static volatile bool frame_ready = false;               /* core 0 -> core 1 */
static volatile bool running = false;                   /* core 1 owns the panel */
static volatile uint32_t frames_shown = 0, frames_skipped = 0;
static volatile uint32_t push_us_sum = 0, push_count = 0;   /* time per push */
static volatile uint32_t rows_sum = 0;                      /* rows sent per push */
static uint32_t row_hash[SCR_H];                        /* what the panel shows */
#ifndef PICO_TINY_SCREEN
const char *const a8_screen_scale_names[A8_SCREEN_SCALES] = { "NORMAL", "TV", "TV SHARP", "PIXEL", "STRETCH" };
static uint16_t pal_plain[256];                         /* RGB565, not byte-swapped: TV's blend */
static int blend_y0 = 0, blend_y1 = 0;                  /* TV: blend lines y0 .. y1-1 */
#endif
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
    { '*', { 0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00 } },
    { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } },
    { '@', { 0x04, 0x15, 0x0E, 0x1B, 0x0E, 0x15, 0x04 } },   /* a gear: Game Settings */
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
 * Over the bottom of the picture: 5 rows (the 5200 keypad: 4) of 10 grid
 * columns, 32 x 20 px a column. Single characters are drawn at twice size, labels (ESC, DEL,
 * SPACE, RET) at normal size. Atari palette indices: black frame, grey
 * keys, blue cursor, white text. */
static const int OSK_CELL_W = SCR_W / A8_OSK_COLS, OSK_CELL_H = 20;
static const uint8_t OSK_FRAME = 0x00, OSK_KEY = 0x04, OSK_CURSOR = 0x96, OSK_TEXT = 0x0F;

static void __not_in_flash_func(draw_osk)(void) {
    const int rows = a8_osk_rows(), OSK_Y = SCR_H - rows * OSK_CELL_H - 2;
    for (int y = OSK_Y - 2; y < SCR_H; y++) memset(frame8 + y * SCR_W, OSK_FRAME, SCR_W);
    int cr, ci;
    a8_osk_cursor(&cr, &ci);
    for (int r = 0; r < rows; r++) {
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

#ifdef PICO_TINY_SCREEN
/* Drawing on the shrunk frame, one band at a time: everything is clipped
 * to the band's rows (tb_y0 .. tb_y0 + tb_rows - 1), so each overlay is
 * drawn into whichever bands it crosses. Pixels in panel order. */
static uint16_t *tb;
static int tb_y0, tb_rows;

static void __not_in_flash_func(tiny_fill)(int x, int y, int w, int h, uint16_t c) {
    int y0 = y > tb_y0 ? y : tb_y0, y1 = y + h < tb_y0 + tb_rows ? y + h : tb_y0 + tb_rows;
    int x0 = x > 0 ? x : 0, x1 = x + w < A8_TINY_W ? x + w : A8_TINY_W;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) tb[(yy - tb_y0) * A8_TINY_W + xx] = c;
}

/* 5x7 text, `adv` px a character (6, or 5 to squeeze 3 letters into a key). */
static void __not_in_flash_func(tiny_text)(int x0, int y0, const char *t, uint16_t fg, int adv = 6) {
    for (int i = 0; t[i]; i++) {
        const uint8_t *g = t[i] == 'E' ? GLYPH_E : t[i] == 'S' ? GLYPH_S : glyph(t[i]);
        if (!g) continue;
        for (int r = 0; r < 7; r++) {
            int y = y0 + r - tb_y0;
            if (y < 0 || y >= tb_rows) continue;
            for (int c = 0; c < 5; c++) {
                int x = x0 + i * adv + c;
                if ((g[r] & (0x10 >> c)) && x >= 0 && x < A8_TINY_W) tb[y * A8_TINY_W + x] = fg;
            }
        }
    }
}

/* A label on black (toast, FPS). */
static void __not_in_flash_func(tiny_label)(int x0, int y0, const char *t) {
    tiny_fill(x0, y0, (int)strlen(t) * 6 + 1, 9, 0);
    tiny_text(x0 + 1, y0 + 1, t, 0xFFFF);
}

/* The on-screen keyboard at the panel's own size, over the bottom of the
 * picture whatever the Scale mode: 10 grid columns of 16 px, rows 11 px. */
static const int TOSK_CELL_W = A8_TINY_W / A8_OSK_COLS, TOSK_CELL_H = 11;

static void __not_in_flash_func(tiny_osk)(void) {
    const int rows = a8_osk_rows(), osk_y = A8_TINY_H - rows * TOSK_CELL_H;
    tiny_fill(0, osk_y - 1, A8_TINY_W, A8_TINY_H - osk_y + 1, pal565[OSK_FRAME]);
    int cr, ci;
    a8_osk_cursor(&cr, &ci);
    for (int r = 0; r < rows; r++) {
        int y0 = osk_y + r * TOSK_CELL_H;
        if (y0 + TOSK_CELL_H <= tb_y0 || y0 >= tb_y0 + tb_rows) continue;   /* not in this band */
        int n, col = 0;
        const A8Key *keys = a8_osk_row(r, &n);
        for (int i = 0; i < n; i++) {
            int x0 = col * TOSK_CELL_W, w = keys[i].span * TOSK_CELL_W - 1;
            tiny_fill(x0, y0, w, TOSK_CELL_H - 1, pal565[(r == cr && i == ci) ? OSK_CURSOR : OSK_KEY]);
            const char *t = keys[i].label;
            int len = (int)strlen(t), adv = len * 6 - 1 <= w - 2 ? 6 : 5;
            int tw = len * adv - (adv - 5);
            tiny_text(x0 + (w - tw + 1) / 2, y0 + 2, t, pal565[OSK_TEXT], adv);
            col += keys[i].span;
        }
    }
}

static void __not_in_flash_func(tiny_overlays)(void) {
    if (a8_osk_is_open()) tiny_osk();
    if (tb_y0 > 0) return;                          /* the labels are in the top band */
    if (toast_until && (int32_t)(toast_until - millis()) > 0) tiny_label(0, 0, toast_text);
    if (fps_on) {
        int e = fps_emu > 99 ? 99 : fps_emu, s = fps_scr > 99 ? 99 : fps_scr;
        char txt[8] = { 'E', (char)('0' + e / 10), (char)('0' + e % 10), ' ',
                        'S', (char)('0' + s / 10), (char)('0' + s % 10), 0 };
        tiny_label(A8_TINY_W - 7 * 6 - 1, 0, txt);
    }
}
#endif

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

#ifdef A8_HDMI
/* HDMI: the menus draw with the same TFT_eSPI calls into an 8-bit (RGB332)
 * sprite whose pixels are frame8 (see the HDMI section at the end).
 * fillScreen() fills TFT_eSPI's _width x _height, which a sprite leaves at
 * the panel size of the build flags (TFT_WIDTH 240 x TFT_HEIGHT 320, the
 * panel's portrait size until init() rotates it, which the canvas never
 * runs): every clear missed x 240-319, and leftovers showed there (bytes of
 * the game zips the list peeks into, a8_menu.cpp's scratch use). So the
 * canvas takes its real size. */
class A8Canvas : public TFT_eSprite {
public:
    explicit A8Canvas(TFT_eSPI *parent) : TFT_eSprite(parent) {}
    void set_screen_size(int32_t w, int32_t h) {
        _init_width = _width = w;
        _init_height = _height = h;
    }
};
static A8Canvas canvas(&tft);
TFT_eSPI &a8_tft(void) { return canvas; }
#else
TFT_eSPI &a8_tft(void) { return tft; }
#endif

uint8_t *a8_display_scratch(size_t *size) {
#ifdef A8_HDMI
    *size = SCR_W * SCR_H;
#else
    *size = sizeof(frame8);
#endif
    return frame8;
}

#ifndef A8_HDMI
void a8_display_init(void) {
#ifdef A8_HEADLESS
    /* No panel (the Fruit Jam sound test): TFT_eSPI is never started, so
     * it touches no pins (its build flags' pins are the Fruit Jam's button
     * 3 and ESP32 UART), and nothing is drawn: core 1 stays idle. */
    Serial.println("[display] none (headless build)");
    return;
#endif
    /* clk_peri back on clk_sys: platformio's f_cpu override leaves it on
     * the USB PLL (48 MHz), which caps SPI at 24 MHz (as in Pico2Arcade). */
    uint32_t sys_hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz, sys_hz);
    tft.init();
#ifdef PICO_TINY_SCREEN
    tft.setRotation(1);                  /* landscape 160 x 80, as the Duo firmware */
#else
    tft.setRotation(3);                  /* landscape 320 x 240, as the arcade */
#endif
    tft.setSwapBytes(false);
    tft.fillScreen(TFT_BLACK);
    if (!tft.initDMA()) Serial.println("[display] initDMA failed");
    Serial.printf("[display] sys %lu MHz, SPI %lu MHz\n", (unsigned long)(sys_hz / 1000000),
                  (unsigned long)(spi_get_baudrate(spi0) / 1000000));
}
#endif

void a8_display_palette(void) {
    static uint16_t plain[256];          /* not byte-swapped: for the shrink */
    for (int i = 0; i < 256; i++) {
        int rgb = Colours_table[i];
        uint16_t c = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
        pal565[i] = (uint16_t)((c >> 8) | (c << 8));
        plain[i] = c;
#ifndef PICO_TINY_SCREEN
        pal_plain[i] = c;
#endif
    }
    a8_scale_palette(plain);
}

#ifndef A8_HDMI
void a8_display_start(void) {
#ifdef A8_HEADLESS
    return;                              /* running stays false: no frames copied */
#endif
    a8_display_palette();
    tft.fillScreen(TFT_BLACK);
    hashes_valid = false;                /* send the first frame whole */
    running = true;
}

void a8_display_stop(void) {
#ifdef A8_HEADLESS
    return;
#endif
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

#ifdef PICO_TINY_SCREEN
/* The 160x80 panel: shrink the snapshot and send all of it. */
static void __not_in_flash_func(push_frame)(void) {
    uint32_t t0 = time_us_32();
    const int mode = a8_settings_scale_now();   /* the game's own, or Settings > Scale;
                                                   the keyboard is drawn after the shrink */
    tft.startWrite();
    tft.setAddrWindow(0, 0, A8_TINY_W, A8_TINY_H);
    for (int y = 0, b = 0; y < A8_TINY_H; y += BAND, b ^= 1) {
        int rows = A8_TINY_H - y < BAND ? A8_TINY_H - y : BAND;
        a8_scale_rows(frame8, tiny565[b], mode, 1, y, y + rows);
        tb = tiny565[b]; tb_y0 = y; tb_rows = rows;
        tiny_overlays();
        tft.dmaWait();                   /* the previous band is out */
        tft.pushPixelsDMA(tiny565[b], A8_TINY_W * rows);
    }
    tft.dmaWait();
    tft.endWrite();
    push_us_sum += time_us_32() - t0;
    rows_sum += A8_TINY_H;
    push_count++;
    frames_shown++;
}
#else
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
        /* TV: the middle pixel of each 3 is the blend of its two source
         * pixels (they are in it and its right neighbour, scale_frame) */
        for (int r = 0; r < rows; r++) {
            int yy = y + r;
            if (yy < blend_y0 || yy >= blend_y1) continue;
            const uint8_t *s = src + r * SCR_W;
            uint16_t *d = dst + r * SCR_W;
            for (int x = (SCR_W - 240) / 2 + 1; x < (SCR_W + 240) / 2; x += 3) {
                uint32_t a = pal_plain[s[x]], b = pal_plain[s[x + 1]];
                uint16_t c = (uint16_t)(((a & 0xF7DEu) + (b & 0xF7DEu)) >> 1);
                d[x] = (uint16_t)((c >> 8) | (c << 8));
            }
        }
        tft.dmaWait();                   /* the other buffer is free again */
        tft.pushPixelsDMA(dst, SCR_W * rows);
        band_buf ^= 1;
    }
}

/* Scale (a8_scale.h): rearrange the snapshot in place. */
static const uint8_t SIDE_BAND = 0x00;          /* Atari palette: black */
static uint8_t scale_tmp[SCR_W];
static void __not_in_flash_func(scale_frame)(int mode) {
    if (mode == A8_BIG_TV || mode == A8_BIG_TV_SHARP || mode == A8_BIG_PIXEL) {
        const int w = mode == A8_BIG_PIXEL ? 160 : 240, x0 = (SCR_W - w) / 2;
        for (int y = 0; y < SCR_H; y++) {
            uint8_t *r = frame8 + y * SCR_W;
            memcpy(scale_tmp, r, SCR_W);
            memset(r, SIDE_BAND, x0);    /* black side bands */
            memset(r + x0 + w, SIDE_BAND, SCR_W - x0 - w);
            if (mode == A8_BIG_PIXEL)
                for (int i = 0; i < w; i++) r[x0 + i] = scale_tmp[2 * i];
            else                         /* 4 source pixels -> 3: 0, 1, 2 (TV: 1 = blend of 1, 2) */
                for (int i = 0; i < w; i++) r[x0 + i] = scale_tmp[i * 4 / 3];
        }
    } else if (mode == A8_BIG_STRETCH) {
        /* line y shows line 24 + y * 192 / 240: in place, the top half top
         * down and the bottom half bottom up (each source still unwritten) */
        for (int y = 0; y < SCR_H / 2; y++) {
            int sy = 24 + y * 192 / SCR_H;
            if (sy != y) memcpy(frame8 + y * SCR_W, frame8 + sy * SCR_W, SCR_W);
        }
        for (int y = SCR_H - 1; y >= SCR_H / 2; y--) {
            int sy = 24 + y * 192 / SCR_H;
            if (sy != y) memcpy(frame8 + y * SCR_W, frame8 + sy * SCR_W, SCR_W);
        }
    }
}

static void __not_in_flash_func(push_frame)(void) {
    uint32_t t0 = time_us_32();
    uint32_t sent = 0;
    const int mode = a8_settings_scale_now();   /* the game's own, or Settings > Scale */
    scale_frame(mode);                          /* before the overlays: they stay as they are */
    blend_y0 = blend_y1 = 0;
    if (mode == A8_BIG_TV) {                    /* not where an overlay is drawn */
        blend_y0 = (fps_on || toast_until) ? OV_H + 2 : 0;
        blend_y1 = a8_osk_is_open() ? SCR_H - a8_osk_rows() * OSK_CELL_H - 4 : SCR_H;
    }
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
#endif

void a8_display_core1_loop(void) {
    if (running) fps_tick();
    if (running && frame_ready) {
        push_frame();
        frame_ready = false;
    }
}
#endif  /* !A8_HDMI */

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

#ifdef A8_HDMI
/* ---- HDMI (the Fruit Jam: RP2350 HSTX) -----------------------------------
 *
 * 800x480 at 60 Hz (by default; see HDMI_MODES) through Pimoroni's dvhstx driver (from Adafruit's
 * Adafruit-DVI-HSTX library; only its driver is built, at the end of this file)
 * in scanline mode: there is no frame buffer. The driver's DMA interrupt
 * (core 1, highest priority) asks for each output line just before it goes
 * out, and hdmi_line() makes it - 320 pixels, each sent twice across, each
 * line twice down - from colour indices through a palette of doubled RGB565
 * words. In RAM, as is everything it reads.
 *
 * Menus: the TFT_eSPI drawing code draws into an 8-bit (RGB332) sprite whose
 * pixels are frame8 (the snapshot buffer the panels use, here on the heap),
 * shown through an RGB332 palette.
 *
 * In a game: no snapshot copy. The lines are read straight from the
 * emulator's Screen_atari (its middle 320 columns), with the Scale mode
 * applied as they are read: STRETCH a row map, TV / TV SHARP 4 pixels -> 3
 * (TV: the middle one the blend of its two), PIXEL every second pixel, the
 * side bands black - as scale_frame does in place for the 320x240 panel.
 * The rows an overlay covers (FPS / message at the top, the on-screen
 * keyboard at the bottom) come from frame8 instead, which core 1 fills once
 * a frame: the scaled game rows, then the overlays on top.
 *
 * Clock: the driver's start-up moves clk_sys to PLL_USB / 2 = 240 MHz and
 * runs HSTX from PLL_SYS at the mode's exact clock (126 MHz: 25.2 MHz pixels,
 * 60.0 Hz). platformio.ini leaves f_cpu at 150 MHz so the core's main()
 * doesn't set clk_sys back (it does only when F_CPU != 150 MHz).
 *
 * Tearing: the emulator (59.92 Hz, paced by the sound) writes Screen_atari
 * while it is read, so a tear line may drift slowly through the picture.
 * TODO: if it shows, start each emulated frame at the HDMI vblank.
 * TODO: flash writes park core 1 (rp2040.idleOtherCore), and with it the
 * line interrupt: the picture drops out for that moment (the driver
 * restarts its stream after). */
#include "drivers/dvhstx/dvhstx.hpp"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/gpio.h"

static pimoroni::DVHSTX hdmi;                           /* claims DMA channels 0-2 here */
static uint32_t hdmi_pal[256];                          /* index -> an RGB565 pixel twice */
static uint8_t  hdmi_src_row[SCR_H];                    /* STRETCH: each row's source line */
static uint8_t  atari332[256];                          /* Atari colour -> RGB332 (canvas) */
static volatile bool hdmi_game = false;                 /* lines from Screen_atari, else frame8 */
static volatile uint8_t hdmi_mode = 0;                  /* the Scale mode the lines use */
static volatile int ov_top = 0, ov_bottom = SCR_H;      /* rows < ov_top or >= ov_bottom: frame8 */
static volatile bool hdmi_wanted = false, hdmi_started = false, hdmi_ok = false;
static volatile bool composing = false;                 /* core 1 is in hdmi_compose */
static volatile bool ov_redraw = true;                  /* frame8 was reused: redraw the overlays */

#ifdef A8_HDMI_AUDIO
/* ---- sound over HDMI ------------------------------------------------------
 * The driver sends 16-bit stereo in data islands in the blanking (its
 * dvhstx_audio.cpp and pico_hdmi's packet code, compiled at the end of this
 * file and in a8_stubs.c), from a queue of 256 encoded packets (36 KB, in
 * RAM: A8_HDMI_AUDIO builds only). 44100 Hz = exactly 2 x the emulator's
 * 22050: each sample is sent twice, the first of the two the mean of it
 * and the one before. Core 1 encodes: pico_audio.cpp's ring (filled and
 * paced as for I2S) -> 2 samples -> one packet of 4 frames -> the queue,
 * kept HDMI_AUDIO_TARGET packets deep (18 ms). The packet code runs from
 * RAM (the driver places it there). DMA channel 3 is the driver's audio
 * channel: reserved here from start-up, so the I2S stream can't take it,
 * and released to the driver when it starts with sound. */
extern "C" {
#include "drivers/dvhstx/audio_config.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/hstx_data_island_queue.h"
}
#include "hardware/dma.h"
#include "pico_audio.h"
static const uint32_t HDMI_AUDIO_RATE = 44100;
static const uint32_t HDMI_AUDIO_TARGET = 200;          /* packets queued (4 frames each) */
static const uint HDMI_AUDIO_DMA = pimoroni::DVHSTX::NUM_AUDIO_CHANS - 1;
#ifndef A8_SOUND_HDMI
#define A8_SOUND_HDMI 0                                 /* 1: sound over HDMI from start-up */
#endif
static volatile bool hdmi_audio_wanted = A8_SOUND_HDMI, hdmi_audio_on = false;
static int hdmi_audio_frame = 0;                        /* the packets' frame counter (channel status) */
static int16_t hdmi_audio_prev = 0;

/* Core 1: top the queue up from the sound ring. */
static void __no_inline_not_in_flash_func(hdmi_audio_feed)(void) {
    const bool in_hsync = hstx_di_queue_get_hsync_active();
    while (hstx_di_queue_get_level() < HDMI_AUDIO_TARGET) {
        int16_t in[2];
        if (pico_audio_hdmi_take(in, 2) < 2) return;      /* nothing due: the driver sends silence */
        audio_sample_t f[4];
        int16_t a = in[0], b = in[1];
        f[0].left = f[0].right = (int16_t)((hdmi_audio_prev + a) / 2);
        f[1].left = f[1].right = a;
        f[2].left = f[2].right = (int16_t)((a + b) / 2);
        f[3].left = f[3].right = b;
        hdmi_audio_prev = b;
        hstx_packet_t packet;
        int next = hstx_packet_set_audio_samples_cs_rate(&packet, f, 4, hdmi_audio_frame, HDMI_AUDIO_RATE);
        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, in_hsync);
        if (!hstx_di_queue_push(&island)) return;        /* (below the target: has room) */
        hdmi_audio_frame = next;
    }
}
#endif
static const uint8_t HDMI_BLACK = 0x00;                 /* Atari palette: black (side bands) */

/* The video mode: the picture is 640x480 (320x240 doubled), centred in the
 * mode's lines with black around it, or cropped (720x400: 20 Atari rows off
 * the top and the bottom). Displays differ in what they take: the first
 * one tried showed nothing at 640x480 @ 60 Hz (the usual mode for 320x240)
 * but 720x400 @ 70 Hz, 720x576 @ 50 Hz and 800x480 @ 60 Hz, 800x600 only
 * sometimes (2026-11-11). 800x480 is the default (the user's pick: full
 * height, black side bands); A8_HDMI_MODE picks another at build time, and
 * the serial monitor's V key steps through them.
 * TODO: a Settings row for it (kept in the settings sector), for displays
 * that take none of the default. */
struct HdmiMode { uint16_t w, h; const char *name; };
static const HdmiMode HDMI_MODES[] = {
    { 320, 240, "640x480 @ 60 Hz" },
    { 360, 200, "720x400 @ 70 Hz (as Doom)" },
    { 360, 240, "720x480 @ 60 Hz (480p)" },
    { 360, 288, "720x576 @ 50 Hz (576p)" },
    { 400, 300, "800x600 @ 60 Hz" },
    { 400, 240, "800x480 @ 60 Hz" },
};
static const int HDMI_NMODES = sizeof(HDMI_MODES) / sizeof(HDMI_MODES[0]);
#ifndef A8_HDMI_MODE
#define A8_HDMI_MODE 5                                  /* index into HDMI_MODES: 800x480 @ 60 Hz */
#endif
static volatile int hdmi_vmode = A8_HDMI_MODE, hdmi_vmode_wanted = A8_HDMI_MODE;
static volatile int out_words = SCR_W, out_lines = 2 * SCR_H;   /* the mode's line: words (2 pixels), lines */

/* Picture row r (0..239) as 320 words. */
static inline __attribute__((always_inline)) void hdmi_picture_line(int r, uint32_t *dst) {
    const uint32_t *pal = hdmi_pal;
    if (!hdmi_game || r < ov_top || r >= ov_bottom) {
        const uint8_t *s = frame8 + r * SCR_W;
        for (int x = 0; x < SCR_W; x++) dst[x] = pal[s[x]];
        return;
    }
    const int mode = hdmi_mode;
    const uint8_t *s = (const uint8_t *)Screen_atari + SRC_X
                       + (mode == A8_BIG_STRETCH ? hdmi_src_row[r] : r) * Screen_WIDTH;
    if (mode == A8_BIG_TV || mode == A8_BIG_TV_SHARP) {
        const uint32_t black = pal[HDMI_BLACK];
        for (int x = 0; x < (SCR_W - 240) / 2; x++) { dst[x] = black; dst[SCR_W - 1 - x] = black; }
        uint32_t *d = dst + (SCR_W - 240) / 2;
        for (int k = 0; k < 80; k++, s += 4, d += 3) {      /* source 4k, 4k+1, 4k+2 */
            d[0] = pal[s[0]];
            if (mode == A8_BIG_TV) {
                uint32_t a = pal_plain[s[1]], b = pal_plain[s[2]];
                uint32_t c = ((a & 0xF7DEu) + (b & 0xF7DEu)) >> 1;
                d[1] = c | (c << 16);
            } else {
                d[1] = pal[s[1]];
            }
            d[2] = pal[s[2]];
        }
    } else if (mode == A8_BIG_PIXEL) {
        const uint32_t black = pal[HDMI_BLACK];
        for (int x = 0; x < (SCR_W - 160) / 2; x++) { dst[x] = black; dst[SCR_W - 1 - x] = black; }
        uint32_t *d = dst + (SCR_W - 160) / 2;
        for (int i = 0; i < 160; i++) d[i] = pal[s[2 * i]];
    } else {
        for (int x = 0; x < SCR_W; x++) dst[x] = pal[s[x]];
    }
}

/* One output line (the driver's interrupt, core 1). */
static void __not_in_flash_func(hdmi_line)(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;
    const int words = out_words, pad = (words - SCR_W) / 2;
    const int r = ((int)active_line - (out_lines - 2 * SCR_H) / 2) >> 1;
    const uint32_t black = hdmi_pal[HDMI_BLACK];        /* index 0: black in both palettes */
    if (r < 0 || r >= SCR_H) {
        for (int x = 0; x < words; x++) dst[x] = black;
        return;
    }
    for (int x = 0; x < pad; x++) { dst[x] = black; dst[words - 1 - x] = black; }
    hdmi_picture_line(r, dst + pad);
}

/* Row y of the scaled game picture as colour indices (no TV blend: an
 * overlay's rows aren't blended on the panel either). */
static void __not_in_flash_func(hdmi_scaled_row)(int y, int mode, uint8_t *dst) {
    const uint8_t *s = (const uint8_t *)Screen_atari + SRC_X
                       + (mode == A8_BIG_STRETCH ? hdmi_src_row[y] : y) * Screen_WIDTH;
    if (mode == A8_BIG_TV || mode == A8_BIG_TV_SHARP || mode == A8_BIG_PIXEL) {
        const int w = mode == A8_BIG_PIXEL ? 160 : 240, x0 = (SCR_W - w) / 2;
        memset(dst, HDMI_BLACK, x0);
        memset(dst + x0 + w, HDMI_BLACK, SCR_W - x0 - w);
        if (mode == A8_BIG_PIXEL)
            for (int i = 0; i < w; i++) dst[x0 + i] = s[2 * i];
        else
            for (int i = 0; i < w; i++) dst[x0 + i] = s[i * 4 / 3];
    } else {
        memcpy(dst, s, SCR_W);
    }
}

static void hdmi_pal_332(void) {
    for (int i = 0; i < 256; i++) {
        uint32_t r = (i >> 5) & 7, g = (i >> 2) & 7, b = i & 3;
        uint32_t c = ((r * 31 / 7) << 11) | ((g * 63 / 7) << 5) | (b * 31 / 3);
        hdmi_pal[i] = c | (c << 16);
    }
}

static void hdmi_pal_atari(void) {
    a8_display_palette();                /* pal_plain: the Atari colours as RGB565 */
    for (int i = 0; i < 256; i++) {
        uint32_t c = pal_plain[i];
        hdmi_pal[i] = c | (c << 16);
        atari332[i] = (uint8_t)(((c >> 8) & 0xE0) | ((c >> 6) & 0x1C) | ((c >> 3) & 0x03));
    }
}

/* Core 0, at boot: the canvas, then core 1 starts the output (the driver's
 * interrupt runs on the core that starts it). */
void a8_display_init(void) {
    canvas.setColorDepth(8);
    frame8 = (uint8_t *)canvas.createSprite(SCR_W, SCR_H);
    if (!frame8) {
        Serial.println("[display] no RAM for the 320x240 canvas");
        return;
    }
    canvas.set_screen_size(SCR_W, SCR_H);    /* fillScreen(): all of it (A8Canvas) */
#ifdef A8_HDMI_AUDIO
    if (!A8_SOUND_HDMI) dma_channel_claim(HDMI_AUDIO_DMA);   /* kept for HDMI sound (see there) */
#endif
    canvas.fillSprite(TFT_BLACK);
    for (int y = 0; y < SCR_H; y++) hdmi_src_row[y] = (uint8_t)(24 + y * 192 / SCR_H);
    hdmi_pal_332();
    hdmi_wanted = true;
    for (uint32_t t0 = millis(); !hdmi_started && millis() - t0 < 2000; ) delay(1);
    Serial.printf("[display] HDMI %s: sys %lu MHz, HSTX %lu MHz, peri %lu MHz%c",
                  hdmi_ok ? "running" : "FAILED",
                  (unsigned long)(clock_get_hz(clk_sys) / 1000000),
                  (unsigned long)(clock_get_hz(clk_hstx) / 1000000),
                  (unsigned long)(clock_get_hz(clk_peri) / 1000000), 10);
    a8_display_report();
}

void a8_display_start(void) {
    hdmi_pal_atari();
    ov_redraw = true;
    ov_top = 0;
    ov_bottom = SCR_H;
    frame_ready = false;
    running = true;
    hdmi_game = true;
}

void a8_display_stop(void) {
    running = false;
    __dmb();
    while (composing) tight_loop_contents();
    /* the paused picture stays under the menus, in the canvas's colours */
    const uint8_t *src = (const uint8_t *)Screen_atari + SRC_X;
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++)
            frame8[y * SCR_W + x] = atari332[src[y * Screen_WIDTH + x]];
    hdmi_game = false;
    hdmi_pal_332();
}

/* Core 0, end of each emulated frame: just a flag (the lines are read from
 * Screen_atari as they go out). */
extern "C" void a8_display_frame(void) {
    if (!running) return;
    frames_emulated++;
    frame_ready = true;
}

void a8_display_refresh(void) { a8_display_frame(); }

/* The on-screen keyboard for HDMI, as draw_osk draws it, but one row of
 * keys at a time into a strip that is then copied in whole: the lines are
 * read from frame8 meanwhile, and draw_osk (clear, then the keys) showed
 * half-drawn keys for a frame when the cursor moved. */
/* (in PSRAM: written only when the keyboard changes, then copied to frame8
 * - never read by the line interrupt; SRAM is short with HDMI sound) */
PSRAM static uint8_t osk_strip[OSK_CELL_H * SCR_W];

static void osk_strip_glyph(int x, int y, const uint8_t *g, int scale, uint8_t colour) {
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (g[r] & (0x10 >> c))
                for (int dy = 0; dy < scale; dy++)
                    memset(osk_strip + (y + r * scale + dy) * SCR_W + x + c * scale, colour, scale);
}

static void hdmi_draw_osk(void) {
    const int rows = a8_osk_rows(), OSK_Y = SCR_H - rows * OSK_CELL_H - 2;
    /* the frame above and below the keys: black, as it already is when
     * redrawn, so writing it again shows nothing */
    for (int y = OSK_Y - 2; y < OSK_Y; y++) memset(frame8 + y * SCR_W, OSK_FRAME, SCR_W);
    for (int y = OSK_Y + rows * OSK_CELL_H; y < SCR_H; y++) memset(frame8 + y * SCR_W, OSK_FRAME, SCR_W);
    int cr, ci;
    a8_osk_cursor(&cr, &ci);
    for (int r = 0; r < rows; r++) {
        memset(osk_strip, OSK_FRAME, sizeof(osk_strip));
        int n;
        const A8Key *keys = a8_osk_row(r, &n);
        int col = 0;
        for (int i = 0; i < n; i++) {
            int x0 = col * OSK_CELL_W + 1, w = keys[i].span * OSK_CELL_W - 2;
            int y0 = 1, h = OSK_CELL_H - 2;                 /* in the strip */
            uint8_t face = (r == cr && i == ci) ? OSK_CURSOR : OSK_KEY;
            for (int y = y0; y < y0 + h; y++) memset(osk_strip + y * SCR_W + x0, face, w);
            const char *t = keys[i].label;
            int len = (int)strlen(t), scale = len == 1 ? 2 : 1;
            int tw = len * 6 * scale - scale, th = 7 * scale;
            int tx = x0 + (w - tw) / 2, ty = y0 + (h - th) / 2;
            for (int k = 0; k < len; k++) {
                const uint8_t *g = glyph(t[k]);
                if (g) osk_strip_glyph(tx + k * 6 * scale, ty, g, scale, OSK_TEXT);
            }
            col += keys[i].span;
        }
        memcpy(frame8 + (OSK_Y + r * OSK_CELL_H) * SCR_W, osk_strip, sizeof(osk_strip));
    }
}

/* What the on-screen keyboard shows: its layout, keys and cursor. */
static uint32_t osk_signature(void) {
    uint32_t h = 2166136261u;
    int cr, ci, rows = a8_osk_rows();
    a8_osk_cursor(&cr, &ci);
    h = (h ^ (uint32_t)rows) * 16777619u;
    h = (h ^ (uint32_t)(cr * 64 + ci)) * 16777619u;
    for (int r = 0; r < rows; r++) {
        int n;
        const A8Key *keys = a8_osk_row(r, &n);
        for (int i = 0; i < n; i++) {
            h = (h ^ (uint32_t)(uintptr_t)keys[i].label) * 16777619u;
            h = (h ^ (uint32_t)keys[i].span) * 16777619u;
            for (const char *t = keys[i].label; *t; t++) h = (h ^ (uint8_t)*t) * 16777619u;
        }
    }
    return h;
}

/* Core 1, once a frame: the overlays' rows into frame8. The lines are read
 * from frame8 while this runs, so nothing is written there that isn't
 * meant to be seen: an overlay is redrawn only when it changes (drawing
 * clears it first), and the game rows around the top boxes are copied
 * around them, never under them. The keyboard covers its rows whole, so no
 * game rows go there at all. (Writing them all every frame, then the
 * overlays over them, showed as tearing in the keyboard.) */
static void hdmi_compose(void) {
    static uint32_t osk_sig = 0;
    static int fps_shown = -1, last_top = 0, last_bottom = SCR_H;
    static char toast_shown[sizeof(toast_text)] = "";
    static uint8_t line[SCR_W];
    uint32_t t0 = time_us_32();
    if (ov_redraw) {                     /* a game (re)started: frame8 held the menus */
        ov_redraw = false;
        last_top = 0;
        last_bottom = SCR_H;
        osk_sig = 0;
    }
    const int mode = a8_settings_scale_now();   /* the game's own, or Settings > Scale */
    const bool toast = toast_until && (int32_t)(toast_until - millis()) > 0;
    const int top = (fps_on || toast) ? OV_H + 2 : 0;
    const bool osk = a8_osk_is_open();
    const int bottom = osk ? SCR_H - a8_osk_rows() * OSK_CELL_H - 4 : SCR_H;

    /* top band: the game rows, around the boxes (as draw_toast / draw_fps
     * place them: OV_H rows from y 2; the toast from x 2, FPS to SCR_W - 2) */
    const int box_y0 = 2, box_y1 = 2 + OV_H;
    const int toast_x1 = toast ? 2 + (int)strlen(toast_text) * OV_CHAR_W + 2 : 0;
    const int fps_x0 = fps_on ? SCR_W - (7 * OV_CHAR_W + 2) - 2 : SCR_W;
    for (int y = 0; y < top; y++) {
        uint8_t *d = frame8 + y * SCR_W;
        hdmi_scaled_row(y, mode, line);
        if (y < box_y0 || y >= box_y1) { memcpy(d, line, SCR_W); continue; }
        if (toast) memcpy(d, line, 2);                       /* left of the toast */
        memcpy(d + toast_x1, line + toast_x1, fps_x0 - toast_x1);   /* between */
        if (fps_on) memcpy(d + SCR_W - 2, line + SCR_W - 2, 2);     /* right of FPS */
    }
    const int fps_now = fps_emu * 100 + fps_scr;
    if (fps_on && (fps_now != fps_shown || last_top == 0)) { draw_fps(); fps_shown = fps_now; }
    if (toast && (strcmp(toast_text, toast_shown) || last_top == 0)) {
        draw_toast();
        strcpy(toast_shown, toast_text);
    }
    if (!toast) toast_shown[0] = 0;
    if (!fps_on) fps_shown = -1;

    /* the keyboard: only when it changed (or its band just appeared) */
    if (osk) {
        uint32_t sig = osk_signature();
        if (sig != osk_sig || bottom != last_bottom) { hdmi_draw_osk(); osk_sig = sig; }
    } else {
        osk_sig = 0;
    }

    hdmi_mode = (uint8_t)mode;
    ov_top = top;
    ov_bottom = bottom;
    last_top = top;
    last_bottom = bottom;
    push_us_sum += time_us_32() - t0;
    rows_sum += (uint32_t)(top + SCR_H - bottom);
    push_count++;
}

#ifdef A8_HDMI_AUDIO
/* Sound over HDMI on / off (core 1 restarts the output with or without
 * it). False if the mode had no room for it. */
bool a8_display_set_hdmi_audio(bool on) {
    hdmi_audio_wanted = on;
    for (uint32_t t0 = millis(); hdmi_audio_on != on && millis() - t0 < 1000; ) delay(1);
    return hdmi_audio_on == on;
}
bool a8_display_hdmi_audio(void) { return hdmi_audio_on; }
uint32_t a8_display_hdmi_audio_gaps(void) { return hstx_di_queue_silence_count; }
#endif

/* The serial monitor's V key: the next video mode (core 1 switches). */
void a8_display_next_hdmi_mode(void) {
    hdmi_vmode_wanted = (hdmi_vmode + 1) % HDMI_NMODES;
}

/* At start: how the output is set up - the mode, HSTX's control register,
 * its clock, the frames sent, and the function of GPIO 12-19 (0 = HSTX). */
void a8_display_report(void) {
    char funcs[9];
    for (int i = 0; i < 8; i++) funcs[i] = (char)('0' + (gpio_get_function(12 + i) & 0x1F) % 10);
    funcs[8] = 0;
    Serial.printf("[display] HDMI %s %s, frames %lu, HSTX csr %08lx, clk_hstx %lu Hz, sys %lu Hz, "
                  "GPIO 12-19 functions %s (0 = HSTX), game %d, scale %d%c",
                  HDMI_MODES[hdmi_vmode].name, hdmi_ok ? "running" : "NOT RUNNING",
                  (unsigned long)hdmi.get_frame_count(),
                  (unsigned long)hstx_ctrl_hw->csr, (unsigned long)clock_get_hz(clk_hstx),
                  (unsigned long)clock_get_hz(clk_sys), funcs, (int)hdmi_game, (int)hdmi_mode, 10);
}

/* Core 1, all the time (main.cpp loop1): starts the output when asked, then
 * in a game the overlays once a frame. */
void a8_display_core1_loop(void) {
#ifdef A8_HDMI_AUDIO
    const bool audio_change = hdmi_started && hdmi_audio_wanted != hdmi_audio_on;
#else
    const bool audio_change = false;
#endif
    if (hdmi_wanted && (!hdmi_started || hdmi_vmode_wanted != hdmi_vmode || audio_change)) {
        /* the Fruit Jam's HSTX pins (its variant): CK+ 13, D0+ 15, D1+ 17, D2+ 19 */
        static const pimoroni::DVHSTXPinout pins = { PIN_CKP, { PIN_D0P, PIN_D1P, PIN_D2P } };
        if (hdmi_started) hdmi.reset();
        const HdmiMode &m = HDMI_MODES[hdmi_vmode_wanted];
        hdmi.set_scanline_callback(hdmi_line);
#ifdef A8_HDMI_AUDIO
        bool with_audio = hdmi_audio_wanted;
        if (with_audio) {
            if (dma_channel_is_claimed(HDMI_AUDIO_DMA)) dma_channel_unclaim(HDMI_AUDIO_DMA);
            hdmi_audio_frame = 0;
            hdmi_audio_prev = 0;
        }
        hdmi.enable_audio(with_audio ? HDMI_AUDIO_RATE : 0);
        hdmi_ok = hdmi.init(m.w, m.h, pimoroni::DVHSTX::MODE_RGB565, false, pins);
        if (!hdmi_ok && with_audio) {           /* a mode with no room for sound: picture only */
            Serial.printf("[display] HDMI sound doesn't fit %s: picture only%c", m.name, 10);
            with_audio = false;
            hdmi.enable_audio(0);
            hdmi_ok = hdmi.init(m.w, m.h, pimoroni::DVHSTX::MODE_RGB565, false, pins);
        }
        if (!with_audio && !dma_channel_is_claimed(HDMI_AUDIO_DMA)) dma_channel_claim(HDMI_AUDIO_DMA);
        hdmi_audio_on = with_audio;
        hdmi_audio_wanted = with_audio;
#else
        hdmi_ok = hdmi.init(m.w, m.h, pimoroni::DVHSTX::MODE_RGB565, false, pins);
#endif
        if (hdmi_ok) {
            out_words = hdmi.get_width() / 2;
            out_lines = hdmi.get_height();
        }
        hdmi_vmode = hdmi_vmode_wanted;
        hdmi_started = true;
        Serial.printf("[display] HDMI mode %d of %d: %s, %s (%d x %d)%s%c", hdmi_vmode + 1, HDMI_NMODES,
                      m.name, hdmi_ok ? "running" : "FAILED", out_words * 2, out_lines,
#ifdef A8_HDMI_AUDIO
                      hdmi_audio_on ? ", sound 44100 Hz" : ", no sound",
#else
                      "",
#endif
                      10);
    }
#ifdef A8_HDMI_AUDIO
    if (hdmi_audio_on) hdmi_audio_feed();
#endif
    composing = true;
    __dmb();
    if (running) {
        fps_tick();
        frames_shown = hdmi.get_frame_count();
        if (frame_ready) {
            hdmi_compose();
            frame_ready = false;
        }
    }
    composing = false;
}

/* The driver's code, compiled here (last: its file-level names stay out of
 * the code above). platformio.ini fetches Adafruit-DVI-HSTX but keeps it
 * out of the build - this core links every object of a library whole, and
 * the library's Arduino wrapper needs Adafruit GFX - so only Pimoroni's
 * dvhstx driver is built: not its HDMI audio (dvhstx_audio.cpp), as nothing
 * enables it. The text-mode font isn't used here, but init() refers to it.
 * (In this file, not files of their own: even an empty extra object file
 * reorders the linker's veneers in the other builds.) */
#include "drivers/dvhstx/dvhstx.cpp"
/* 800x600 @ 60 Hz by the VESA book: the driver's has a front porch of 44
 * (lines of 1060 pixels, not 1056). With that, the first display tried
 * showed 7 thin evenly spaced black vertical bars (when it showed 800x600 at
 * all - it takes the mode only sometimes, and with 40 not at all); a second
 * display shows it clean with 40 (2026-11-11).
 * TODO: the driver's 44 on that second display, to know whether the bars
 * were the porch or the first display.
 * The driver's table entry is renamed out of the way and this one takes its
 * name. */
#define dvi_timing_800x600p_60hz dvi_timing_800x600p_60hz_driver
#include "drivers/dvhstx/dvi.cpp"
#undef dvi_timing_800x600p_60hz
const struct dvi_timing dvi_timing_800x600p_60hz = {
    .h_sync_polarity = false,
    .h_front_porch   = 40,
    .h_sync_width    = 128,
    .h_back_porch    = 88,
    .h_active_pixels = 800,

    .v_sync_polarity = false,
    .v_front_porch   = 1,
    .v_sync_width    = 4,
    .v_back_porch    = 23,
    .v_active_lines  = 600,

    .bit_clk_khz     = 400000
};
#include "drivers/dvhstx/font_cp437.c"
#ifdef A8_HDMI_AUDIO
#undef NUM_CHANS                         /* dvhstx.cpp's 3; the audio code's is 4 */
#include "drivers/dvhstx/dvhstx_audio.cpp"
#endif
#endif  /* A8_HDMI */
