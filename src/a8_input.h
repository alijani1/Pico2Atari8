/* a8_input.h - the device's controls, for the game list and for the game. */
#ifndef A8_INPUT_H
#define A8_INPUT_H

#include <stddef.h>
#include <stdint.h>

/* Game list buttons (a8_input_menu) */
#define A8M_UP    0x01
#define A8M_DOWN  0x02
#define A8M_LEFT  0x04
#define A8M_RIGHT 0x08
#define A8M_OK    0x10
#define A8M_BACK  0x20
#define A8M_ALT   0x40     /* SELECT: start the game as a 400/800 */

void    a8_input_init(void);
/* Fill a8_input for the next frame. True = MENU held: leave the game. */
bool    a8_input_frame(void);
/* The on-screen keyboard is open and no key is being typed: hold the game. */
bool    a8_input_osk_paused(void);
uint8_t a8_input_menu(void);

/* Controller test (a8_ctltest.cpp): the Joy-Con's raw reading, its centre
 * and the range seen so far, and as a 5200 pot (A8_POT_NONE at rest); the
 * device buttons; a port's directions and fire. */
enum { A8B_RED, A8B_GREEN, A8B_BLUE, A8B_YELLOW, A8B_START, A8B_SELECT, A8B_CLICK, A8B_MENU, A8B_COUNT };
void    a8_input_joycon_diag(int raw[2], int centre[2], int lo[2], int hi[2], int16_t pot[2]);
bool    a8_input_button(int which);
uint8_t a8_input_port_dirs(int n, bool *fire);
int     a8_input_num_ports(void);

#endif
