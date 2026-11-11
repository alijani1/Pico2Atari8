/* a8_scale.h - the Atari picture shrunk to a 160x80 panel (the Duo
 * Ultimate's ST7735, PICO_TINY_SCREEN). Plain C, no Arduino: the PC harness
 * (tools/host) runs the same code.
 *
 * The source is the 320x240 snapshot of colour indices a8_display.cpp takes
 * (the middle 320 columns of Screen_atari, all 240 rows). Across, 320 -> 160
 * is exactly 2:1 - most Atari graphics are 160 colour clocks wide, so that
 * loses nothing for them. Down, lines 8-223 (the 192-line playfield and the
 * extra lines some games use above and below it) are shrunk 2.7:1 to 80
 * rows, or with CROP the middle 160 lines are shown 2:1. The modes (MENU tap
 * in a game on this build):
 *
 *   SMOOTH  each row averages the lines it covers, by how much (default)
 *   SHARP   each row averages the two lines nearest its middle
 *   DROP    each row is the line at its middle, each pixel the left one of
 *           its pair: sharpest and fastest, but one-line details can vanish
 *   CROP    lines 36-195 at 2:1 both ways: no blur, but the lines above
 *           and below (often the score) are cut
 */
#ifndef A8_SCALE_H
#define A8_SCALE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { A8_SCALE_SMOOTH = 0, A8_SCALE_SHARP, A8_SCALE_DROP, A8_SCALE_CROP, A8_SCALE_MODES };

#define A8_TINY_W 160
#define A8_TINY_H 80
#define A8_SCALE_SRC_W 320
#define A8_SCALE_SRC_H 240

extern const char *const a8_scale_names[A8_SCALE_MODES];

/* This build's screen's Scale modes (Settings > Scale, Game Settings,
 * the Next scale action). The 160x80 panel: the modes above. The 320x240
 * panel (Quad, Duo 5200; a8_display.cpp, on core 1, in the snapshot):
 *
 *   NORMAL    the middle 320 x 240, 1:1 - an Atari colour clock (160 a
 *             line) is 2 pixels wide (default)
 *   TV        240 wide, centred: a colour clock 1.5 pixels (as wide as on
 *             a TV, ~1.7), the middle pixel of each 3 blended
 *   TV SHARP  the same, no blending (colour clocks 2, 1, 2, 1 ... wide)
 *   PIXEL     160 wide, centred: a colour clock is 1 pixel (hi-res detail,
 *             80-column text, loses every second pixel)
 *   STRETCH   320 wide, the 192-line playfield (lines 24-215) stretched to
 *             all 240 lines (the border above / below is cut)
 *
 * The side bands of TV / PIXEL are black. */
#ifdef PICO_TINY_SCREEN
#define A8_SCREEN_SCALES A8_SCALE_MODES
#define a8_screen_scale_names a8_scale_names
#else
enum { A8_BIG_NORMAL = 0, A8_BIG_TV, A8_BIG_TV_SHARP, A8_BIG_PIXEL, A8_BIG_STRETCH, A8_BIG_SCALES };
#define A8_SCREEN_SCALES A8_BIG_SCALES
extern const char *const a8_screen_scale_names[A8_SCREEN_SCALES];
#endif

/* The 256 Atari colours as RGB565 (not byte-swapped): once per palette. */
void a8_scale_palette(const uint16_t pal565[256]);

/* src: 320x240 colour indices; dst: output rows oy0 .. oy1-1 (160 pixels
 * each, from dst[0]) as RGB565, byte-swapped for the panel if swap. */
void a8_scale_rows(const uint8_t *src, uint16_t *dst, int mode, int swap, int oy0, int oy1);
/* The whole 160x80 frame. */
void a8_scale_frame(const uint8_t *src, uint16_t *dst, int mode, int swap);

#ifdef __cplusplus
}
#endif

#endif
