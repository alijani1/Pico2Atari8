/* a8_menu.h - the game list. */
#ifndef A8_MENU_H
#define A8_MENU_H

/* Show the list; returns the chosen file's drive path ("/ATARI/GAME.XEX").
 * MENU returns to the host instead (never comes back). */
const char *a8_menu_pick(void);
/* Picked with SELECT (start it on the other machine than the default). */
bool a8_menu_picked_alt(void);
/* Its name says it's a 400/800 game ("-osb", "(OS-B)", "[400-800]"). */
bool a8_menu_picked_osb(void);

#endif
