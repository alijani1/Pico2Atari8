/* a8_settings.h - the Settings screen and the choices it keeps. */
#ifndef A8_SETTINGS_H
#define A8_SETTINGS_H

/* On-screen keyboard modes */
enum { A8_KBD_ONE_KEY = 0,        /* paused; the first key typed is sent and closes it */
       A8_KBD_AUTO_PAUSE,         /* paused; each key runs the game briefly; Blue closes */
       A8_KBD_KEEP_RUNNING,       /* the game keeps running */
       A8_KBD_MODES };

struct A8Settings {
    int  keyboard;                /* A8_KBD_* */
    bool fps;                     /* FPS overlay */
    bool machine_800;             /* default machine: false = 800XL, true = 400/800 */
    bool pal;                     /* TV system: false = NTSC */
};

extern A8Settings a8_settings;

void a8_settings_load(void);      /* from flash (defaults if none): at boot */
void a8_settings_screen(void);    /* the Settings screen; saves on the way out if changed */

#endif
