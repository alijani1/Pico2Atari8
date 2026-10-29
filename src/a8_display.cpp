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
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hardware/clocks.h"
#include "a8_display.h"
#include "a8_platform.h"

extern "C" {
#include "screen.h"
#include "colours.h"
}

static TFT_eSPI tft;

static const int SCR_W = 320, SCR_H = 240;
static const int SRC_X = (Screen_WIDTH - SCR_W) / 2;    /* 32: middle 320 columns */
static const int BAND = 16;                             /* lines per DMA push */

static uint8_t  frame8[SCR_W * SCR_H];                  /* snapshot for core 1 */
static uint16_t band565[2][SCR_W * BAND];               /* ping-pong DMA buffers */
static uint16_t pal565[256];                            /* byte-swapped for the panel */
static volatile bool frame_ready = false;               /* core 0 -> core 1 */
static volatile bool running = false;                   /* core 1 owns the panel */
static volatile uint32_t frames_shown = 0, frames_skipped = 0;

TFT_eSPI &a8_tft(void) { return tft; }

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
    if (frame_ready) { frames_skipped++; return; }   /* core 1 still busy */
    const uint8_t *src = (const uint8_t *)Screen_atari + SRC_X;
    for (int y = 0; y < SCR_H; y++)
        memcpy(frame8 + y * SCR_W, src + y * Screen_WIDTH, SCR_W);
    frame_ready = true;
}

/* Core 1: send a snapshot. */
static void push_frame(void) {
    tft.startWrite();
    tft.setAddrWindow(0, 0, SCR_W, SCR_H);
    int b = 0;
    for (int y0 = 0; y0 < SCR_H; y0 += BAND) {
        uint16_t *dst = band565[b];
        const uint8_t *src = frame8 + y0 * SCR_W;
        for (int i = 0; i < SCR_W * BAND; i++) dst[i] = pal565[src[i]];
        tft.dmaWait();                   /* the other buffer is free again */
        tft.pushPixelsDMA(dst, SCR_W * BAND);
        b ^= 1;
    }
    tft.dmaWait();
    tft.endWrite();
    frames_shown++;
}

void a8_display_core1_loop(void) {
    if (running && frame_ready) {
        push_frame();
        frame_ready = false;
    }
}

void a8_display_stats(uint32_t *shown, uint32_t *skipped) {
    *shown = frames_shown;
    *skipped = frames_skipped;
}
