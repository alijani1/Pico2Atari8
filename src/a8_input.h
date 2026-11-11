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
#define A8M_GREEN 0x80     /* Green: the game's Game Settings (a8_gamecfg.cpp) */

void    a8_input_init(void);
/* Fill a8_input for the next frame. True = MENU held: leave the game. */
bool    a8_input_frame(void);
/* The on-screen keyboard is open and no key is being typed: hold the game. */
bool    a8_input_osk_paused(void);
uint8_t a8_input_menu(void);

/* The on-screen keyboard's gear key was picked: open Game Settings (once). */
bool    a8_input_take_game_settings(void);
/* Game Settings: this device has the button (A8_GB_*) / a Joy-Con. */
bool    a8_input_has_slot(int slot);
bool    a8_input_has_joycon(void);

/* Controller test (a8_ctltest.cpp): the Joy-Con's raw reading, its centre
 * and the range seen so far, and as a 5200 pot (A8_POT_NONE at rest); the
 * device buttons; a port's directions and fire. */
enum { A8B_RED, A8B_GREEN, A8B_BLUE, A8B_YELLOW, A8B_START, A8B_SELECT, A8B_CLICK, A8B_MENU,
       A8B_SIDE_L, A8B_SIDE_R,     /* Quad only */
       A8B_COUNT };
void    a8_input_joycon_diag(int raw[2], int centre[2], int lo[2], int hi[2], int16_t pot[2]);
bool    a8_input_button(int which);
#ifdef A8_USB_HOST
/* The Fruit Jam's USB host ports (a8_input.cpp): the task, from core 1's
 * loop (the first call starts the host); and for Controller test, what is
 * plugged in and held. */
void    a8_input_usb_task(void);
enum { A8U_UP = 1, A8U_DOWN = 2, A8U_LEFT = 4, A8U_RIGHT = 8, A8U_A = 16, A8U_B = 32, A8U_X = 64,
       A8U_Y = 128, A8U_L = 256, A8U_R = 512, A8U_SELECT = 1024, A8U_START = 2048 };
struct A8UsbDiag {
    bool pad, keyboard;
    uint16_t vid, pid;            /* the pad's */
    uint32_t buttons;             /* the pad's A8U_* */
    uint8_t raw[16], raw_len;     /* its last report */
    uint8_t kbd[8];               /* the keyboard's last report */
    int key;                      /* the Atari key it types now, or -1 */
};
void    a8_input_usb_diag(A8UsbDiag *d);
#endif
uint8_t a8_input_port_dirs(int n, bool *fire);
int     a8_input_num_ports(void);
/* The Duo Ultimate's 5-way (A8_UP ... directions; *press = its press);
 * 0 / false elsewhere. */
uint8_t a8_input_five_way(bool *press);
#if defined(PICO_FRUITJAM) && defined(A8_HEADLESS)
/* The Fruit Jam's button 3 was pressed: go to the next game (headless). */
bool    a8_input_next_game(void);
#endif

#endif
