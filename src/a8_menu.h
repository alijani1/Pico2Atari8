/* a8_menu.h - the game list. */
#ifndef A8_MENU_H
#define A8_MENU_H

/* Show the list; returns the chosen file's drive path ("/ATARI/GAME.XEX").
 * MENU returns to the host instead (never comes back). */
const char *a8_menu_pick(void);
#ifdef A8_HEADLESS
/* No display: a game without the list - `want` if it's on the drive, else
 * the list's game number *index (by name, as the list; wrapping). *index is
 * set to the one picked. nullptr: no games on the drive. */
const char *a8_menu_auto(const char *want, int *index);
#endif
/* Picked with SELECT (start it on the other machine than the default). */
bool a8_menu_picked_alt(void);
/* Its name says it's a 400/800 game ("-osb", "(OS-B)", "[400-800]"). */
bool a8_menu_picked_osb(void);

#endif
