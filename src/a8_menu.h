/* a8_menu.h - the game list. */
#ifndef A8_MENU_H
#define A8_MENU_H

/* Show the list; returns the chosen file's drive path ("/ATARI/GAME.XEX").
 * MENU returns to the host instead (never comes back). */
const char *a8_menu_pick(void);

#endif
