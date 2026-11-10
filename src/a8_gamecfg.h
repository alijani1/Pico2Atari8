/* a8_gamecfg.h - a game's own settings screen (Game Settings). */
#ifndef A8_GAMECFG_H
#define A8_GAMECFG_H

/* The Game Settings screen for the game a8_settings_set_game() named: what
 * Red, Green, Yellow, the click (Duo: the 5-way's press) and the Quad's
 * side buttons do, the Joy-Con as joystick 1 or paddle 1 (400/800/XL games)
 * and, in a game, disk drive 1 on / off. Opened from the game list (Green)
 * or in a game (the on-screen keyboard's gear key, with the display
 * stopped). Changes are kept by a8_settings; the caller saves them
 * (a8_settings_save_game) once the display is stopped. */
void a8_gamecfg_screen(const char *name, bool is5200, bool in_game);

#endif
