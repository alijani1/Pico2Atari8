/* pico_paddles.cpp - see pico_paddles.h.
 *
 * Each paddle is a 1 M pot from +5 V (pin 7, wired on the Quad) charging a
 * cap on pin 5 or 9 (GPIO 42 / 43 port 1, 40 / 41 port 2). Parked OUTPUT LOW
 * the cap drains; released to INPUT it charges through the pot and the pin
 * reads HIGH after a time that grows with the pot's resistance - the Atari's
 * own POKEY read. As for the 5200 sticks (pico_5200ctl.cpp, after
 * GameAdapterRGBPico src/pio_rc.cpp) a PIO state machine per pin counts the
 * time: one count = 0.8 us. A paddle reads about 200 (turned right) to 8000
 * (left); with nothing on the pin (a joystick, or no controller) it never
 * charges, so 12000 = no paddle. The counters are started one call and read
 * a later one, so nothing waits.
 *
 * The count is proportional to the resistance, as the Atari's pot value is.
 * It's clipped at the source before it's mapped: the low end (the lowest
 * count seen, plus a margin - a pot's last few percent are unreliable) and
 * the high end at a share of the turn (~8000 counts end to end, less a
 * clip) set by Settings > Paddle sensitivity: all of it (Very low), 75, 50
 * (Medium, the default), 40 or 30 %. Atari games use half the paddle or
 * less, which made them quick and sensitive. What's left is spread over the
 * whole 1..228 the game reads.
 */
#include "pico_paddles.h"

#ifdef PICO_QUAD

#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "a8_display.h"
#include "a8_settings.h"

static const uint8_t RC_PINS[4] = { 42, 43, 40, 41 };   /* p1 A, p1 B, p2 A, p2 B */
static const uint32_t RC_MAX  = 12000;      /* timeout (counts, 9.6 ms): no paddle */
static const uint32_t RC_LOW  = 200;        /* turned right, a first guess (only grows down) */
static const uint32_t SRC_TRIM = 100;       /* clipped off above the lowest count seen */
static const uint32_t SRC_FULL = 7800;      /* the whole turn (~8000) less a clip */
static const uint8_t  USE_PCT[A8_PADDLES_LEVELS] = { 100, 75, 50, 40, 30 };   /* A8_PADDLES_* */
static const float    RC_PIO_HZ = 2500000.0f;

/* GameAdapterRGBPico src/pio_rc.cpp: count loops until the pin reads HIGH.
 *   pull block / mov x, osr / loop: jmp pin done / jmp x-- loop /
 *   done: mov isr, x / push  -> RX gets maxCount - elapsed (0xFFFFFFFF on timeout) */
static const uint16_t rc_prog_instr[] = { 0x80a0, 0xa027, 0x00c4, 0x0042, 0xa0c1, 0x8020 };
static const struct pio_program rc_prog = { rc_prog_instr, 6, -1 };

static PIO  rc_pio = nullptr;
static uint rc_sm[4], rc_offset;
static bool rc_running = false;
static uint32_t rc_started_us;

struct Paddle { uint32_t lo, hi, last, prev; int16_t pot; };
struct Port { Paddle pd[2]; int seen, missed; bool present; };
static Port ports[2];

static bool rc_pio_init(void) {
    PIO candidates[] = {
#if NUM_PIOS > 2
        pio2,                        /* the only PIO that reaches GPIO 40-43 */
#endif
        pio0, pio1 };
    for (PIO pio : candidates) {
#if PICO_PIO_USE_GPIO_BASE
        if (pio_get_gpio_base(pio) != 16 && pio_set_gpio_base(pio, 16) != PICO_OK) continue;
#endif
        if (!pio_can_add_program(pio, &rc_prog)) continue;
        int got = 0;
        for (; got < 4; got++) {
            int sm = pio_claim_unused_sm(pio, false);
            if (sm < 0) break;
            rc_sm[got] = (uint)sm;
        }
        if (got < 4) { while (got--) pio_sm_unclaim(pio, rc_sm[got]); continue; }
        rc_offset = pio_add_program(pio, &rc_prog);
        rc_pio = pio;
        return true;
    }
    return false;
}

static void rc_park(void) {                 /* OUTPUT LOW: drain the caps */
    for (uint8_t p : RC_PINS) { gpio_put(p, 0); gpio_set_dir(p, GPIO_OUT); }
}

/* Release the caps and start the four counters together. */
static void rc_start(void) {
    float div = (float)F_CPU / RC_PIO_HZ;
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) {
        pio_sm_config c = pio_get_default_sm_config();
        sm_config_set_wrap(&c, rc_offset, rc_offset + 5);
        sm_config_set_clkdiv(&c, div);
        sm_config_set_jmp_pin(&c, RC_PINS[i]);
        pio_sm_init(rc_pio, rc_sm[i], rc_offset, &c);
        pio_sm_put(rc_pio, rc_sm[i], RC_MAX);
        mask |= 1u << rc_sm[i];
    }
    /* release and start in one go: no interrupt can let a cap charge uncounted */
    uint32_t irq = save_and_disable_interrupts();
    for (uint8_t p : RC_PINS) gpio_set_dir(p, GPIO_IN);
    pio_enable_sm_mask_in_sync(rc_pio, mask);
    restore_interrupts(irq);
    rc_started_us = time_us_32();
    rc_running = true;
}

/* The last two counts (a little smoothing), clipped to the window (above),
 * as a pot value; it moves only once past three quarters of a step, so a
 * paddle left alone doesn't flicker between two values. */
static void update_pot(Paddle &p) {
    uint32_t from = p.lo + SRC_TRIM, to = from + (SRC_FULL - from) * USE_PCT[a8_settings.paddles] / 100;
    p.hi = to;                                       /* for the controller test */
    uint32_t c = (p.last + p.prev) / 2;
    if (c < from) c = from;
    if (c > to) c = to;
    int32_t x256 = 256 + (int32_t)((uint64_t)(c - from) * 227 * 256 / (to - from));
    int32_t d = x256 - p.pot * 256;
    if (d > 192 || d < -192) p.pot = (int16_t)((x256 + 128) / 256);
    if (p.pot < 1) p.pot = 1;
    if (p.pot > 228) p.pot = 228;
}

/* The counts are in (or the counters gave up): use them and park again. */
static void rc_collect(void) {
    uint32_t c[4];
    for (int i = 0; i < 4; i++) {
        if (pio_sm_is_rx_fifo_empty(rc_pio, rc_sm[i])) { c[i] = RC_MAX; continue; }
        uint32_t raw = pio_sm_get(rc_pio, rc_sm[i]);
        c[i] = raw > RC_MAX ? RC_MAX : RC_MAX - raw;
    }
    uint32_t mask = 0;
    for (int i = 0; i < 4; i++) mask |= 1u << rc_sm[i];
    pio_set_sm_mask_enabled(rc_pio, mask, false);
    rc_park();
    rc_running = false;

    for (int n = 0; n < 2; n++) {
        Port &pt = ports[n];
        for (int a = 0; a < 2; a++) { pt.pd[a].prev = pt.pd[a].last; pt.pd[a].last = c[2 * n + a]; }
        bool there = c[2 * n] < RC_MAX || c[2 * n + 1] < RC_MAX;
        /* plugged in: half a second of readings; unplugged: a few timeouts
         * in a row */
        if (!there) {
            if (pt.present && ++pt.missed >= 10) {
                pt.present = false;
                a8_display_toast(n ? "PADDLES 2 OUT" : "PADDLES 1 OUT");
                Serial.printf("[paddles] port %d unplugged\n", n + 1);
            }
            pt.seen = 0;
            continue;
        }
        pt.missed = 0;
        if (!pt.present) {
            if (++pt.seen < 30) continue;
            pt.present = true;
            for (Paddle &p : pt.pd) { p.lo = RC_LOW; p.hi = 0; p.pot = 0; }
            Serial.printf("[paddles] port %d: %lu, %lu\n", n + 1,
                          (unsigned long)c[2 * n], (unsigned long)c[2 * n + 1]);
            a8_display_toast(n ? "PADDLES 2 IN" : "PADDLES 1 IN");
        }
        for (Paddle &p : pt.pd) {
            if (p.last >= RC_MAX) continue;          /* this one's missing: keep the last */
            if (p.last < p.lo) p.lo = p.last;
            if (p.prev >= RC_MAX) p.prev = p.last;
            update_pot(p);
        }
    }
}

void paddles_init(void) {
    for (uint8_t p : RC_PINS) { gpio_init(p); gpio_disable_pulls(p); }
    rc_park();
    if (!rc_pio_init()) Serial.println("[paddles] no free PIO");
}

void paddles_poll(void) {
    if (!rc_pio) return;
    if (rc_running && (time_us_32() - rc_started_us) >= 10000) rc_collect();   /* > 9.6 ms: all done */
    if (!rc_running) {
        delayMicroseconds(200);               /* drain the caps */
        rc_start();
    }
}

bool paddles_present(int port) { return port >= 0 && port < 2 && ports[port].present; }

int16_t paddles_pot(int port, int paddle) {
    if (!paddles_present(port)) return -1;
    const Paddle &p = ports[port].pd[paddle & 1];
    return p.pot > 0 ? p.pot : -1;
}

void paddles_diag(int port, int paddle, uint32_t *count, uint32_t *lo, uint32_t *hi) {
    const Paddle &p = ports[port & 1].pd[paddle & 1];
    *count = p.last;
    *lo = ports[port & 1].present ? p.lo : 0;
    *hi = ports[port & 1].present ? p.hi : 0;
}

#endif
