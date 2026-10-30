/* a8_settings.cpp - the Settings screen and where its choices are kept.
 *
 *   Keyboard         Auto-pause / Keep running   (on-screen keyboard)
 *   FPS display      Off / On
 *   Volume           0 - 100 %                   (shared with the Quad: it
 *                                                 passes its volume in and
 *                                                 takes it back on exit)
 *   Default machine  800XL / 400/800             (FIRE / START; SELECT in
 *                                                 the game list starts the
 *                                                 other one)
 *   TV system        NTSC / PAL
 *
 * Saved in a 4 KB flash sector of our own (A8_SETTINGS_PHYS), written only
 * when something changed.
 */
#include <Arduino.h>
#include "a8_settings.h"
#include "a8_display.h"
#include "a8_flash.h"
#include "a8_input.h"
#include "pico_audio.h"
#include "pico_handoff.h"

static const uint32_t MAGIC = 0x53384131u;    /* "1A8S" */

struct Stored {
    uint32_t magic;
    uint8_t keyboard_keep_running, fps, machine_800, pal;
    uint32_t check;
};

A8Settings a8_settings = { false, false, false, false };

static uint32_t checksum(const Stored &s) {
    return s.magic ^ (s.keyboard_keep_running << 1) ^ (s.fps << 3) ^ (s.machine_800 << 5) ^ (s.pal << 7) ^ 0xA5A5A5A5u;
}

void a8_settings_load(void) {
    Stored s;
    memcpy(&s, a8_flash_read(A8_SETTINGS_PHYS), sizeof(s));
    if (s.magic != MAGIC || s.check != checksum(s)) {
        Serial.println("[settings] none saved: defaults");
        return;
    }
    a8_settings.keyboard_keep_running = s.keyboard_keep_running != 0;
    a8_settings.fps = s.fps != 0;
    a8_settings.machine_800 = s.machine_800 != 0;
    a8_settings.pal = s.pal != 0;
    Serial.printf("[settings] keyboard %s, fps %s, machine %s, %s\n",
                  a8_settings.keyboard_keep_running ? "keep running" : "auto-pause",
                  a8_settings.fps ? "on" : "off", a8_settings.machine_800 ? "400/800" : "800XL",
                  a8_settings.pal ? "PAL" : "NTSC");
}

static void save(void) {
    size_t cap;
    uint8_t *sector = a8_display_scratch(&cap);   /* idle in the menu: no game yet */
    memset(sector, 0xFF, 4096);
    Stored s;
    s.magic = MAGIC;
    s.keyboard_keep_running = a8_settings.keyboard_keep_running;
    s.fps = a8_settings.fps;
    s.machine_800 = a8_settings.machine_800;
    s.pal = a8_settings.pal;
    s.check = checksum(s);
    memcpy(sector, &s, sizeof(s));
    pico_audio_suspend();                    /* its DMA can't ride out a flash write */
    a8_flash_write_sector(A8_SETTINGS_PHYS, sector);
    pico_audio_resume();
    Serial.println("[settings] saved");
}

/* ---- the screen -------------------------------------------------------- */

enum { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_BACK, ROWS };
static const char *const NAMES[ROWS] = { "Keyboard", "FPS display", "Volume", "Default machine",
                                         "TV system", "Back" };
static const int ROW_H = 26, LIST_Y = 40;
static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_SEL_BG = 0x2A69,
                      COL_TITLE = 0xFD20, COL_HINT = TFT_DARKGREY, COL_VALUE = 0x07FF;

static void value_text(int row, char *buf, size_t cap) {
    switch (row) {
    case ROW_KEYBOARD: snprintf(buf, cap, "%s", a8_settings.keyboard_keep_running ? "Keep running" : "Auto-pause"); break;
    case ROW_FPS:      snprintf(buf, cap, "%s", a8_settings.fps ? "On" : "Off"); break;
    case ROW_VOLUME:   snprintf(buf, cap, "%d%%", pico_audio_get_volume_pct()); break;
    case ROW_MACHINE:  snprintf(buf, cap, "%s", a8_settings.machine_800 ? "400/800" : "800XL"); break;
    case ROW_TV:       snprintf(buf, cap, "%s", a8_settings.pal ? "PAL" : "NTSC"); break;
    default:           buf[0] = 0; break;
    }
}

static void draw_row(int row, bool sel) {
    TFT_eSPI &tft = a8_tft();
    int y = LIST_Y + row * ROW_H;
    uint16_t bg = sel ? COL_SEL_BG : COL_BG;
    tft.fillRect(0, y, 320, ROW_H, bg);
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, bg);
    tft.setCursor(16, y + 5);
    tft.print(NAMES[row]);
    char v[24];
    value_text(row, v, sizeof(v));
    if (v[0]) {
        tft.setTextColor(sel ? COL_TEXT : COL_VALUE, bg);
        tft.setCursor(320 - 16 - tft.textWidth(v), y + 5);
        tft.print(v);
    }
}

static void draw_screen(int sel) {
    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(2);
    tft.setTextColor(COL_TITLE, COL_BG);
    tft.setCursor(8, 6);
    tft.print("SETTINGS");
    tft.drawFastHLine(0, 25, 320, COL_HINT);
    for (int r = 0; r < ROWS; r++) draw_row(r, r == sel);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print("LEFT / RIGHT: change    MENU: back");
}

/* left / right on a row (dir -1 / +1) */
static bool change(int row, int dir) {
    switch (row) {
    case ROW_KEYBOARD: a8_settings.keyboard_keep_running = !a8_settings.keyboard_keep_running; return true;
    case ROW_FPS:      a8_settings.fps = !a8_settings.fps; return true;
    case ROW_MACHINE:  a8_settings.machine_800 = !a8_settings.machine_800; return true;
    case ROW_TV:       a8_settings.pal = !a8_settings.pal; return true;
    case ROW_VOLUME: {
        int v = pico_audio_get_volume_pct() + dir * 10;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        pico_audio_set_volume_pct(v);
        handoff_volume_write(v);             /* the Quad takes it back on exit */
        return false;                        /* not ours to store */
    }
    default: return false;
    }
}

void a8_settings_screen(void) {
    int sel = 0;
    bool dirty = false;
    draw_screen(sel);
    for (uint32_t t0 = millis(); a8_input_menu() && millis() - t0 < 1000; ) delay(10);
    uint8_t prev = 0xFF;
    uint32_t held_since = 0, last_repeat = 0;
    for (;;) {
        uint8_t b = a8_input_menu();
        uint8_t pressed = b & ~prev;
        uint32_t now = millis();
        uint8_t dirs = b & (A8M_UP | A8M_DOWN | A8M_LEFT | A8M_RIGHT);
        if (dirs && dirs == (prev & dirs)) {
            if (!held_since) held_since = now;
            if (now - held_since > 400 && now - last_repeat > 120) { pressed |= dirs; last_repeat = now; }
        } else {
            held_since = 0;
        }
        prev = b;

        int old = sel;
        if (pressed & A8M_UP)   sel = sel > 0 ? sel - 1 : ROWS - 1;
        if (pressed & A8M_DOWN) sel = sel < ROWS - 1 ? sel + 1 : 0;
        if (sel != old) { draw_row(old, false); draw_row(sel, true); }
        if (pressed & (A8M_LEFT | A8M_RIGHT | A8M_OK)) {
            if (sel == ROW_BACK) { if (pressed & A8M_OK) break; }
            else {
                int dir = (pressed & A8M_LEFT) ? -1 : 1;
                if (change(sel, dir)) dirty = true;
                draw_row(sel, true);
            }
        }
        if (pressed & A8M_BACK) break;
        delay(10);
    }
    while (a8_input_menu() & (A8M_BACK | A8M_OK)) delay(10);
    if (dirty) save();
}
