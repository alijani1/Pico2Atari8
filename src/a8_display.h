/* a8_display.h - the TFT: menus on core 0, the emulated screen from core 1. */
#ifndef A8_DISPLAY_H
#define A8_DISPLAY_H

#include <TFT_eSPI.h>

TFT_eSPI &a8_tft(void);
/* The frame snapshot (75 KB), free to use before a game starts. */
uint8_t *a8_display_scratch(size_t *size);

void a8_display_init(void);        /* panel on, landscape, DMA (core 0, at boot) */
void a8_display_palette(void);     /* Colours_table -> panel colours */
void a8_display_start(void);       /* hand the panel to core 1 for the game */
void a8_display_stop(void);        /* ... and take it back */
void a8_display_core1_loop(void);  /* core 1: send a frame when one is ready */
void a8_display_refresh(void);     /* paused: show the last frame again (with overlays) */
void a8_display_set_fps(bool on);  /* "E60 S58" overlay (Settings > FPS display) */
void a8_display_toast(const char *text);   /* message top-left for 2 s (digits, D E F I N O R S V) */
void a8_display_stats(uint32_t *shown, uint32_t *skipped);
/* Average push time (us) and rows sent per push, since the last call. */
uint32_t a8_display_push_us(uint32_t *avg_rows);

#endif
