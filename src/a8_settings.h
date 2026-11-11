/* a8_settings.h - the Settings screen and the choices it keeps. */
#ifndef A8_SETTINGS_H
#define A8_SETTINGS_H

#include "a8_scale.h"                /* A8_SCALE_*: Scale on the 160x80 panel */

/* On-screen keyboard modes */
enum { A8_KBD_ONE_KEY = 0,        /* paused; the first key typed is sent and closes it */
       A8_KBD_AUTO_PAUSE,         /* paused; each key runs the game briefly; Blue closes */
       A8_KBD_KEEP_RUNNING,       /* the game keeps running */
       A8_KBD_MODES };

/* Paddle sensitivity (Quad): how much of the paddle's turn spans 1..228 */
enum { A8_PADDLES_VERY_LOW = 0,   /* all of it */
       A8_PADDLES_LOW,            /* 75 % */
       A8_PADDLES_MEDIUM,         /* 50 % */
       A8_PADDLES_HIGH,           /* 40 % */
       A8_PADDLES_VERY_HIGH,      /* 30 % */
       A8_PADDLES_LEVELS };

struct A8Settings {
    int  keyboard;                /* A8_KBD_* */
    bool fps;                     /* FPS overlay */
    bool machine_800;             /* default machine: false = 800XL, true = 400/800 */
    bool pal;                     /* TV system: false = NTSC */
    int  paddles;                 /* A8_PADDLES_*: paddle sensitivity (Quad) */
    int  scale;                   /* A8_SCALE_*: how the picture is shrunk (160x80 panel) */
};

extern A8Settings a8_settings;

void a8_settings_load(void);      /* from flash (defaults if none): at boot */
void a8_settings_screen(void);    /* the Settings screen; saves on the way out if changed */

/* Per game (by its path in the game list): the Joy-Con as paddle 1 instead
 * of joystick 1 (400/800/XL games; Yellow + click switches, a8_input.cpp). */
void a8_settings_set_game(const char *path);   /* the game being started */
bool a8_settings_game_paddle(void);
void a8_settings_set_game_paddle(bool on);     /* kept in RAM until ... */
void a8_settings_save_game(void);              /* ... the game ends (if it changed) */

/* Game Settings (a8_gamecfg.cpp), per game like the Joy-Con mode: what
 * each device button does. Blue, START, SELECT, MENU and the sticks stay
 * fixed. The "click" slot is the Joy-Con's click, or the Duo's 5-way press;
 * the side buttons are the Quad's. */
enum { A8_GB_RED, A8_GB_GREEN, A8_GB_YELLOW, A8_GB_CLICK, A8_GB_SIDE_L, A8_GB_SIDE_R,
       A8_GB_BLUE,                /* (added last: the stored order) - Keyboard by default */
       A8_GB_COUNT };
/* A button's action: an AKEY_* key with no SHIFT / CTRL (0x00 - 0x3F; on a
 * 5200 the keypad's AKEY_5200_*), or one of these. */
#define A8_ACT_NONE     0xF0
#define A8_ACT_FIRE     0xF1      /* joystick 1's fire (paddle 1's button in paddle mode;
                                     a 5200's bottom fire) */
#define A8_ACT_TOP_FIRE 0xF2      /* a 5200's top fire */
#define A8_ACT_START    0xF3      /* 400/800/XL console keys */
#define A8_ACT_SELECT   0xF4
#define A8_ACT_OPTION   0xF5
#define A8_ACT_KEYBOARD 0xF6      /* the on-screen keyboard: open / close (Blue's default) */
#define A8_ACT_SCALE    0xF7      /* the next picture Scale mode (the game's own) */
#define A8_ACT_J2_UP    0xF8      /* joystick 2 (a 5200: controller 2's stick) up ... */
#define A8_ACT_J2_DOWN  0xF9
#define A8_ACT_J2_LEFT  0xFA
#define A8_ACT_J2_RIGHT 0xFB      /* ... right: twin-stick games (Robotron) */
#define A8_ACT_DEFAULT  0xFF      /* stored: the button's default */
uint8_t a8_settings_game_action(int slot, bool is5200);     /* never A8_ACT_DEFAULT */
uint8_t a8_settings_default_action(int slot, bool is5200);
void    a8_settings_set_game_action(int slot, bool is5200, uint8_t act);
bool    a8_settings_game_buttons_custom(bool is5200);       /* any button not on its default */
void    a8_settings_game_reset(void);                       /* everything back to defaults */
/* Disk drive 1 switched off once the game has loaded (a8_platform.c,
 * a8_drive1_auto_off_poll) - for the few disk games that won't start
 * while a drive answers. */
bool    a8_settings_game_drive_off(void);
void    a8_settings_set_game_drive_off(bool on);
/* The machine for this game: A8_MACH_DEFAULT = Settings > Default machine
 * (SELECT in the list still starts the other one). */
enum { A8_MACH_DEFAULT = 0, A8_MACH_XL, A8_MACH_800, A8_MACH_COUNT };
int     a8_settings_game_machine(void);
void    a8_settings_set_game_machine(int m);
/* The Joy-Con paddle's centre (at rest), moved by this much of the pot's
 * range: + = to the right. The pot runs 1 (right) .. 228 (left), centre
 * 114, so -A8_PADDLE_OFS_MAX .. +A8_PADDLE_OFS_MAX.
 * In a 5200 game ("Horizontal center"): controller 1's stick x (6 left ..
 * 114 .. 220 right), + = to the right as well (a8_input.cpp). */
#define A8_PADDLE_OFS_MAX 113
int     a8_settings_game_paddle_ofs(void);
void    a8_settings_set_game_paddle_ofs(int ofs);
/* Picture Scale: Settings > Scale is the default; a game can have its own
 * (Game Settings, and the Next scale action). */
int  a8_settings_scale_now(void);              /* the mode in use: the game's, or the default */
int  a8_settings_game_scale(void);             /* 0 = the default, else mode + 1 */
void a8_settings_set_game_scale(int s);
void a8_settings_next_scale(void);             /* the game's own: the next mode (saved as above) */

#endif
