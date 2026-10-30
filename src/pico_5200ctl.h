/* pico_5200ctl.h - the Duo 5200 Ultimate's two Atari 5200 controller ports
 * (GameAdapterRGBPico data/duo5200.json, src/pio_rc.cpp, src/keypad5200.cpp):
 * analog sticks, Fire A / Fire B, and the keypads. PICO_5200 only. */
#ifndef PICO_5200CTL_H
#define PICO_5200CTL_H

#include <stdint.h>

#ifdef PICO_5200

void ctl5200_init(void);
/* Call once a frame: collects the last pot reading and starts the next, and
 * scans one port's keypad (the other one the next frame). */
void ctl5200_poll(void);

bool    ctl5200_present(int port);           /* a controller is plugged in */
/* The stick as a 5200 pot: 6 (left / up) .. 114 (rest) .. 220 (right /
 * down); -1 if no controller. axis 0 = x, 1 = y. */
int16_t ctl5200_pot(int port, int axis);
bool    ctl5200_fire_a(int port);            /* bottom button */
bool    ctl5200_fire_b(int port);            /* top button */
/* Keypad: 0-9 for the digits, 10 = *, 11 = #, 12 START, 13 PAUSE, 14 RESET;
 * -1 for none (the lowest pressed on either controller). */
int     ctl5200_key(void);

/* Controller test: an axis's last count (0.8 us each; 8000 = timed out), its
 * rest and the range seen; a controller's keys held (bit k = key k). */
void     ctl5200_diag(int port, int axis, uint32_t *count, uint32_t *rest, uint32_t *lo, uint32_t *hi);
uint16_t ctl5200_keys(int port);
bool     ctl5200_keypad_found(void);

#endif
#endif
