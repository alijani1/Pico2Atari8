/* a8_settings.h - the Settings screen and the choices it keeps. */
#ifndef A8_SETTINGS_H
#define A8_SETTINGS_H

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

#endif
