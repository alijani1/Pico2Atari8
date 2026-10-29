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

void    a8_input_init(void);
/* Fill a8_input for the next frame. True = MENU held: leave the game. */
bool    a8_input_frame(void);
uint8_t a8_input_menu(void);
/* Joy-Con readings as one line of text (debug, shown in the game list). */
void    a8_input_joy_debug(char *buf, size_t cap);

#endif
