/* a8_osk.h - the on-screen keyboard (layout and cursor; see a8_osk.cpp). */
#ifndef A8_OSK_H
#define A8_OSK_H

#define A8_OSK_ROWS 5
#define A8_OSK_COLS 10          /* grid columns; a key spans 1 or more */
#define A8_OSK_DRIVE (-100)     /* the DRV "key": disk drive 1 off / on */

struct A8Key {
    const char *label;
    int code;                   /* AKEY_* */
    int span;                   /* grid columns */
};

bool a8_osk_is_open(void);
void a8_osk_open(void);
void a8_osk_close(void);
void a8_osk_move(int dx, int dy);     /* -1 / 0 / +1, wraps round */
int  a8_osk_key(void);                /* AKEY_* under the cursor */
const A8Key *a8_osk_row(int r, int *count);
void a8_osk_cursor(int *row, int *idx);

#endif
