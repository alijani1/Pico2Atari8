/* pico_5200ctl.cpp - see pico_5200ctl.h.
 *
 * Sticks. Each axis is a pot charging a 22 nF cap (1.5 k in series) on GPIO
 * 40-43 (port 1 x/y, port 2 x/y). Parked OUTPUT LOW the cap drains; released
 * to INPUT it charges through the pot and the pin reads HIGH after a time
 * that grows with the pot's resistance - the Atari's own RC read. As in
 * GameAdapterRGBPico (src/pio_rc.cpp), a PIO state machine per pin counts
 * the time, so interrupts can't skew it: one count = 0.8 us (2.5 MHz PIO
 * clock, 2 instructions a loop). A 5200 stick reads about 400 counts (left
 * / up) to 3335 (right / down) or more, resting near 2300 (1.9 ms); 8000 =
 * no controller. Here the counters are started one frame and read the next,
 * so the read never blocks the emulator.
 *
 * Fire A / Fire B are GPIO 10 / 11 (port 1) and 12 / 13 (port 2), active LOW.
 *
 * Keypads: a PCA9555 on I2C1 (SDA 2, SCL 3), one 8-bit port per controller:
 * bits 0-3 rows KBR4..KBR1 (inputs), bits 4-7 columns KBC4..KBC1 (driven LOW
 * one at a time). Key map (GameAdapterRGBPico include/keypad5200.hpp):
 *          C1    C2     C3     C4
 *     R1:  -     Start  Pause  Reset
 *     R2:  *     1      4      7
 *     R3:  0     2      5      8
 *     R4:  #     3      6      9
 */
#include "pico_5200ctl.h"

#ifdef PICO_5200

#include <Arduino.h>
#include <Wire.h>
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

/* ---- sticks ------------------------------------------------------------ */

static const uint8_t FIRE_PINS[2][2] = { { 10, 11 }, { 12, 13 } };   /* Fire A, Fire B */

static const uint8_t RC_PINS[4] = { 40, 41, 42, 43 };   /* p1 x, p1 y, p2 x, p2 y */
static const uint32_t RC_MAX  = 8000;       /* timeout (counts, 6.4 ms): no controller. 4500 (the
                                               host's) was too short: a stick pushed hard reads more */
static const uint32_t RC_LOW  = 400;        /* full left / up, a first guess (only grows outward) */
static const uint32_t RC_HIGH = 3000;       /* full right / down, likewise (reads up to ~3335) */
static const uint32_t RC_REST_DEAD = 20;    /* counts either side of rest = exactly the centre */
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

struct Axis { uint32_t rest, lo, hi, last; int16_t pot; };
struct Port { Axis ax[2]; int seen, missed; bool present; };
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

/* A count as a pot value: rest = 114, the travel seen either side spread
 * over 6..114 and 114..220. */
static int16_t to_pot(const Axis &a, uint32_t c) {
    int32_t d = (int32_t)c - (int32_t)a.rest;
    if (d >= -(int32_t)RC_REST_DEAD && d <= (int32_t)RC_REST_DEAD) return 114;
    int32_t p;
    if (d > 0) {
        int32_t span = (int32_t)a.hi - (int32_t)a.rest - (int32_t)RC_REST_DEAD;
        p = span > 0 ? 114 + (d - (int32_t)RC_REST_DEAD) * 106 / span : 220;
    } else {
        int32_t span = (int32_t)a.rest - (int32_t)a.lo - (int32_t)RC_REST_DEAD;
        p = span > 0 ? 114 + (d + (int32_t)RC_REST_DEAD) * 108 / span : 6;
    }
    if (p < 6) p = 6;
    if (p > 220) p = 220;
    return (int16_t)p;
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
        uint32_t x = c[2 * n], y = c[2 * n + 1];
        pt.ax[0].last = x;
        pt.ax[1].last = y;
        bool there = x < RC_MAX && y < RC_MAX;
        /* plugged in: half a second of readings, then its rest is where it
         * sits (the stick springs back to the middle); unplugged: a few
         * timeouts in a row */
        if (!there) {
            if (pt.present && ++pt.missed >= 10) {
                pt.present = false;
                Serial.printf("[5200] controller %d unplugged\n", n + 1);
            }
            pt.seen = 0;
            continue;
        }
        pt.missed = 0;
        if (!pt.present) {
            if (++pt.seen < 30) continue;
            pt.present = true;
            for (int a = 0; a < 2; a++) {
                pt.ax[a].rest = a ? y : x;
                pt.ax[a].lo = RC_LOW;
                pt.ax[a].hi = RC_HIGH;
            }
            Serial.printf("[5200] controller %d: rest %lu, %lu\n", n + 1, (unsigned long)x, (unsigned long)y);
        }
        for (int a = 0; a < 2; a++) {
            Axis &ax = pt.ax[a];
            uint32_t v = a ? y : x;
            if (v < ax.lo) ax.lo = v;
            if (v > ax.hi) ax.hi = v;
            ax.pot = to_pot(ax, v);
        }
    }
}

/* ---- keypads ------------------------------------------------------------ */

static const uint8_t PCA_INPUT0 = 0x00, PCA_OUTPUT0 = 0x02, PCA_CONFIG0 = 0x06;
static const uint8_t COL_BIT[4] = { 7, 6, 5, 4 };   /* KBC1..KBC4 */
static const uint8_t ROW_BIT[4] = { 3, 2, 1, 0 };   /* KBR1..KBR4 */
/* [row][col] -> ctl5200_key value, -1 = no key there */
static const int8_t KEY_AT[4][4] = {
    { -1, 12, 13, 14 },
    { 10,  1,  4,  7 },
    {  0,  2,  5,  8 },
    { 11,  3,  6,  9 },
};
static uint8_t pca_addr = 0;
static uint16_t keys_down[2];             /* bit k = key k held, per controller */
static int scan_port = 0;

static void pca_write(uint8_t reg, uint8_t v) {
    Wire1.beginTransmission(pca_addr);
    Wire1.write(reg);
    Wire1.write(v);
    Wire1.endTransmission();
}

static uint8_t pca_read(uint8_t reg) {
    Wire1.beginTransmission(pca_addr);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return 0xFF;
    if (Wire1.requestFrom(pca_addr, (uint8_t)1) != 1) return 0xFF;
    return Wire1.read();
}

static void keypad_init(void) {
    Wire1.setSDA(2);
    Wire1.setSCL(3);
    Wire1.setClock(400000);
    Wire1.begin();
    for (uint8_t a = 0x20; a <= 0x27; a++) {      /* the address straps vary */
        Wire1.beginTransmission(a);
        if (Wire1.endTransmission() == 0) { pca_addr = a; break; }
    }
    if (!pca_addr) { Serial.println("[5200] keypad expander not found"); return; }
    for (int p = 0; p < 2; p++) {
        pca_write(PCA_CONFIG0 + p, 0x0F);            /* rows in, columns out */
        pca_write(PCA_OUTPUT0 + p, 0xFF);            /* columns idle HIGH */
    }
    Serial.printf("[5200] keypad expander at 0x%02x\n", pca_addr);
}

static void keypad_scan(int p) {
    if (!pca_addr) return;
    uint16_t down = 0;
    for (int c = 0; c < 4; c++) {
        pca_write(PCA_OUTPUT0 + p, (uint8_t)~(1 << COL_BIT[c]));   /* this column LOW */
        delayMicroseconds(40);
        uint8_t in = pca_read(PCA_INPUT0 + p);
        for (int r = 0; r < 4; r++)
            if (!(in & (1 << ROW_BIT[r])) && KEY_AT[r][c] >= 0) down |= 1u << KEY_AT[r][c];
    }
    pca_write(PCA_OUTPUT0 + p, 0xFF);
    keys_down[p] = down;
}

/* ---- the lot -------------------------------------------------------------- */

void ctl5200_init(void) {
    for (auto &port : FIRE_PINS) for (uint8_t p : port) pinMode(p, INPUT_PULLUP);
    for (uint8_t p : RC_PINS) { gpio_init(p); gpio_disable_pulls(p); }
    rc_park();
    if (!rc_pio_init()) Serial.println("[5200] no free PIO for the sticks");
    keypad_init();
}

void ctl5200_poll(void) {
    if (rc_pio) {
        if (rc_running && (time_us_32() - rc_started_us) >= 7000) rc_collect();   /* > 6.4 ms: all done */
        if (!rc_running) {
            delayMicroseconds(200);           /* drain the caps (1.5 k x 22 nF) */
            rc_start();
        }
    }
    keypad_scan(scan_port);
    scan_port ^= 1;
}

bool ctl5200_present(int port) { return port >= 0 && port < 2 && ports[port].present; }

int16_t ctl5200_pot(int port, int axis) {
    if (!ctl5200_present(port)) return -1;
    return ports[port].ax[axis & 1].pot;
}

bool ctl5200_fire_a(int port) { return digitalRead(FIRE_PINS[port & 1][0]) == LOW; }
bool ctl5200_fire_b(int port) { return digitalRead(FIRE_PINS[port & 1][1]) == LOW; }

int ctl5200_key(void) {
    uint16_t d = keys_down[0] | keys_down[1];
    for (int k = 0; k < 15; k++) if (d & (1u << k)) return k;
    return -1;
}

void ctl5200_diag(int port, int axis, uint32_t *count, uint32_t *rest, uint32_t *lo, uint32_t *hi) {
    const Axis &a = ports[port & 1].ax[axis & 1];
    *count = a.last;
    *rest = ports[port & 1].present ? a.rest : 0;
    *lo = a.lo;
    *hi = a.hi;
}

uint16_t ctl5200_keys(int port) { return keys_down[port & 1]; }
bool ctl5200_keypad_found(void) { return pca_addr != 0; }

#endif
