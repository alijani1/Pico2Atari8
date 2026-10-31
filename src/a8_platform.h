/* a8_platform.h - glue between the atari800 core (C) and the Pico side.
 *
 * main.cpp fills a8_input from the device's controls before each
 * Atari800_Frame(); a8_platform.c answers the core's PLATFORM_* calls from
 * it. The rest is implemented on the Pico side (C++).
 */
#ifndef A8_PLATFORM_H
#define A8_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* POKEY output rate (the PWM ring runs at it) */
#define A8_SAMPLE_RATE 22050

/* Stick bits (set = held) */
#define A8_UP    0x01
#define A8_DOWN  0x02
#define A8_LEFT  0x04
#define A8_RIGHT 0x08

typedef struct {
    uint8_t stick[4];      /* A8_UP | A8_DOWN | A8_LEFT | A8_RIGHT per port */
    uint8_t trig[4];       /* fire button per port, 1 = pressed */
    uint8_t start, select, option;
    int     key;           /* AKEY_* code held, or -1 for none */
    int     shift;         /* SHIFT held with it (5200: the top fire button) */
    int16_t pot[8];        /* 5200 analog sticks, x/y per port (6 left/up ..
                              114 centre .. 220 right/down); A8_POT_NONE = use
                              the stick's directions. 400/800/XL: the paddles,
                              2 per port (1..228); A8_POT_NONE = none (228) */
} a8_input_t;

#define A8_POT_NONE (-1)

extern a8_input_t a8_input;

/* a8_platform.c */
int  a8_guess_cart_type(int kb);
int  a8_is_5200(void);               /* the machine is a 5200 (now) */
/* A raw cartridge (.bin / .rom) of `size` bytes ending in `tail` (its last
 * 6 bytes): 1 = a 5200 one, 0 = a 400/800/XL one, -1 = can't tell. */
int  a8_raw_cart_is_5200(const uint8_t *tail, long size);
/* The file is a 5200 game: .a52, or a raw cartridge that looks like one
 * (unknown ones count as 5200 when unknown_5200). */
int  a8_file_is_5200(const char *path, int unknown_5200);
/* A .car file's header (16 bytes) names a 5200 cartridge type. */
int  a8_car_is_5200(const uint8_t *header);
void a8_frame(void);                 /* keyboard, emulate one frame, show it */
void a8_silent_frame(void);          /* paused: a frame's time of silence */
int  a8_toggle_drive1(void);         /* drive 1 off / back on; true = now on */
/* Set by a8_frame when the 6502 hit a crash opcode. a8_handle_crash then
 * restarts the game (path) on an 800 if it was on the XL: true if it did,
 * false if it was on the 800 already. */
extern volatile int a8_crashed;
int  a8_handle_crash(const char *path);

/* Pico side */
uint64_t a8_time_us(void);
void     a8_idle(void);              /* waiting for the audio ring: keep the rest alive */
void     a8_display_frame(void);     /* Screen_atari is complete: show it */

/* Debug: a line to the serial monitor, and where start-up has got to (the
 * core 1 heartbeat prints it, so it shows even if core 0 stops). */
void     a8_log_write(const char *line);
extern volatile const char *a8_stage;

#ifdef __cplusplus
}
#endif

#endif
