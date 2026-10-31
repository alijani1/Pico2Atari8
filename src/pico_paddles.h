/* pico_paddles.h - Atari paddles on the Quad Ultimate CX's ports 1 and 2
 * (GameAdapterRGBPico data/quad.json: pins 5 / 9 on GPIO 42 / 43 and 40 / 41,
 * src/pio_rc.cpp). The paddles' buttons are the ports' left / right lines,
 * read with the joystick. PICO_QUAD only. */
#ifndef PICO_PADDLES_H
#define PICO_PADDLES_H

#include <stdint.h>

#ifdef PICO_QUAD

void paddles_init(void);
/* Call often (once a frame or more): collects the last pot reading once it's
 * in and starts the next. */
void paddles_poll(void);

bool    paddles_present(int port);           /* port 0 / 1: paddles plugged in */
/* A paddle as an Atari pot, over the whole 1 (turned right) .. 228; the
 * paddle's own counts are clipped to about half its turn first
 * (pico_paddles.cpp). -1 if none. paddle 0 = A (pin 5, its button is left),
 * 1 = B (pin 9, right). */
int16_t paddles_pot(int port, int paddle);

/* Controller test: a paddle's last count (0.8 us each; 12000 = timed out)
 * and the range seen. */
void    paddles_diag(int port, int paddle, uint32_t *count, uint32_t *lo, uint32_t *hi);

#endif
#endif
