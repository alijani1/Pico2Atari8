/* a8_settings.h - the Settings screen and the choices it keeps. */
#ifndef A8_SETTINGS_H
#define A8_SETTINGS_H

struct A8Settings {
    bool keyboard_keep_running;   /* on-screen keyboard: false = the game pauses */
    bool fps;                     /* FPS overlay */
    bool machine_800;             /* default machine: false = 800XL, true = 400/800 */
    bool pal;                     /* TV system: false = NTSC */
};

extern A8Settings a8_settings;

void a8_settings_load(void);      /* from flash (defaults if none): at boot */
void a8_settings_screen(void);    /* the Settings screen; saves on the way out if changed */

#endif
