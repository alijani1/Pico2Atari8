/* a8_menu.h - the game list. */
#ifndef A8_MENU_H
#define A8_MENU_H

/* Show the list; returns the chosen file's drive path ("/ATARI/GAME.XEX").
 * MENU returns to the host instead (never comes back). */
const char *a8_menu_pick(void);
/* Start the picked game as a 400/800: picked with SELECT, or its name says
 * so ("-osb", "(OS-B)", "[400-800]"). */
bool a8_menu_picked_800(void);

#endif
