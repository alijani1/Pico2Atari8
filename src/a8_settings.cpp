/* a8_settings.cpp - the Settings screen and where its choices are kept.
 *
 *   Keyboard         Pause+Auto Resume / Auto-pause / Keep running
 *                                                (on-screen keyboard)
 *   FPS display      Off / On
 *   Volume           0 - 100 %                   (shared with the Quad: it
 *                                                 passes its volume in and
 *                                                 takes it back on exit)
 *   Default machine  800XL / 400/800             (FIRE / START; SELECT in
 *                                                 the game list starts the
 *                                                 other one)
 *   TV system        NTSC / PAL
 *   Paddle sens.     Very low / Low / Medium / High / Very high
 *                                                (Quad; pico_paddles.cpp)
 *   Controller test  every control live (a8_ctltest.cpp)
 *
 * Saved in a 4 KB flash sector of our own (A8_SETTINGS_PHYS), written only
 * when something changed. The same sector holds, after them, the games set
 * to use the Joy-Con as a paddle (a hash of each one's path).
 */
#include <Arduino.h>
#include "a8_settings.h"
#include "a8_version.h"
#include "a8_display.h"
#include "a8_flash.h"
#include "a8_input.h"
#include "a8_ctltest.h"
#include "pico_audio.h"
#include "pico_handoff.h"

static const uint32_t MAGIC = 0x53384133u;    /* "3A8S" (2: no paddles; 1: keyboard was a yes/no) */

struct Stored {
    uint32_t magic;
    uint8_t keyboard, fps, machine_800, pal, paddles, spare[3];
    uint32_t check;
};

A8Settings a8_settings = { A8_KBD_ONE_KEY, false, false, false, A8_PADDLES_MEDIUM };

static const char *const KEYBOARD_NAMES[A8_KBD_MODES] = { "Pause+Auto Resume", "Auto-pause", "Keep running" };
static const char *const PADDLES_NAMES[A8_PADDLES_LEVELS] = { "Very low", "Low", "Medium", "High", "Very high" };

/* The games whose Joy-Con is a paddle, at GAMES_AT in the sector. */
static const uint32_t GAMES_MAGIC = 0x50594F4Au;  /* "JOYP" */
static const int MAX_GAMES = 250, GAMES_AT = 64;
struct StoredGames {
    uint32_t magic, count;
    uint32_t hash[MAX_GAMES];
    uint32_t check;
};
static uint32_t game_hash[MAX_GAMES];
static int game_count = 0;
static uint32_t this_game = 0;            /* hash of the game being played */
static bool games_dirty = false;

static uint32_t games_check(const uint32_t *h, uint32_t n) {
    uint32_t c = GAMES_MAGIC ^ n;
    for (uint32_t i = 0; i < n; i++) c = (c << 5 | c >> 27) ^ h[i];
    return c;
}

static uint32_t checksum(const Stored &s) {
    return s.magic ^ (s.keyboard << 1) ^ (s.fps << 3) ^ (s.machine_800 << 5) ^ (s.pal << 7) ^
           (s.paddles << 9) ^ 0xA5A5A5A5u;
}

static void load_games(void) {
    const StoredGames *g = (const StoredGames *)(a8_flash_read(A8_SETTINGS_PHYS) + GAMES_AT);
    if (g->magic != GAMES_MAGIC || g->count > (uint32_t)MAX_GAMES || g->check != games_check(g->hash, g->count))
        return;
    game_count = (int)g->count;
    memcpy(game_hash, g->hash, game_count * sizeof(uint32_t));
    Serial.printf("[settings] %d games with the Joy-Con as a paddle\n", game_count);
}

void a8_settings_load(void) {
    load_games();
    Stored s;
    memcpy(&s, a8_flash_read(A8_SETTINGS_PHYS), sizeof(s));
    if (s.magic != MAGIC || s.check != checksum(s)) {
        Serial.println("[settings] none saved: defaults");
        return;
    }
    a8_settings.keyboard = s.keyboard < A8_KBD_MODES ? s.keyboard : A8_KBD_ONE_KEY;
    a8_settings.fps = s.fps != 0;
    a8_settings.machine_800 = s.machine_800 != 0;
    a8_settings.pal = s.pal != 0;
    a8_settings.paddles = s.paddles < A8_PADDLES_LEVELS ? s.paddles : A8_PADDLES_MEDIUM;
    Serial.printf("[settings] keyboard %s, fps %s, machine %s, %s, paddles %s\n",
                  KEYBOARD_NAMES[a8_settings.keyboard],
                  a8_settings.fps ? "on" : "off", a8_settings.machine_800 ? "400/800" : "800XL",
                  a8_settings.pal ? "PAL" : "NTSC", PADDLES_NAMES[a8_settings.paddles]);
}

static void save(void) {
    size_t cap;
    uint8_t *sector = a8_display_scratch(&cap);   /* idle in the menu: no game yet */
    memset(sector, 0xFF, 4096);
    Stored s;
    s.magic = MAGIC;
    s.keyboard = (uint8_t)a8_settings.keyboard;
    s.fps = a8_settings.fps;
    s.machine_800 = a8_settings.machine_800;
    s.pal = a8_settings.pal;
    s.paddles = (uint8_t)a8_settings.paddles;
    memset(s.spare, 0, sizeof(s.spare));
    s.check = checksum(s);
    memcpy(sector, &s, sizeof(s));
    StoredGames *g = (StoredGames *)(sector + GAMES_AT);
    g->magic = GAMES_MAGIC;
    g->count = (uint32_t)game_count;
    memcpy(g->hash, game_hash, game_count * sizeof(uint32_t));
    g->check = games_check(g->hash, g->count);
    pico_audio_suspend();                    /* its DMA can't ride out a flash write */
    a8_flash_write_sector(A8_SETTINGS_PHYS, sector);
    pico_audio_resume();
    Serial.println("[settings] saved");
}

/* ---- per game ------------------------------------------------------------ */

void a8_settings_set_game(const char *path) {
    uint32_t h = 2166136261u;                     /* FNV-1a, case-blind */
    for (const char *p = path; *p; p++) h = (h ^ (uint8_t)tolower((unsigned char)*p)) * 16777619u;
    this_game = h;
}

static int game_index(void) {
    for (int i = 0; i < game_count; i++) if (game_hash[i] == this_game) return i;
    return -1;
}

bool a8_settings_game_paddle(void) { return game_index() >= 0; }

void a8_settings_set_game_paddle(bool on) {
    int i = game_index();
    if (on && i < 0) {
        if (game_count == MAX_GAMES) {           /* full: the oldest goes */
            memmove(game_hash, game_hash + 1, (MAX_GAMES - 1) * sizeof(uint32_t));
            game_count--;
        }
        game_hash[game_count++] = this_game;
        games_dirty = true;
    } else if (!on && i >= 0) {
        memmove(game_hash + i, game_hash + i + 1, (game_count - i - 1) * sizeof(uint32_t));
        game_count--;
        games_dirty = true;
    }
}

/* The display must be stopped: its frame buffer is the sector buffer. */
void a8_settings_save_game(void) {
    if (!games_dirty) return;
    save();
    games_dirty = false;
}

/* ---- the screen -------------------------------------------------------- */

#ifdef PICO_QUAD
enum { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_PADDLES, ROW_TEST, ROW_BACK, ROWS };
static const char *const NAMES[ROWS] = { "Keyboard", "FPS display", "Volume", "Default machine",
                                         "TV system", "Paddle sensitivity", "Controller test", "Back" };
#else
enum { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_TEST, ROW_BACK, ROWS, ROW_PADDLES = -1 };
static const char *const NAMES[ROWS] = { "Keyboard", "FPS display", "Volume", "Default machine",
                                         "TV system", "Controller test", "Back" };
#endif
static const int ROW_H = 22, LIST_Y = 34;
static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_SEL_BG = 0x2A69,
                      COL_TITLE = 0xFD20, COL_HINT = TFT_DARKGREY, COL_VALUE = 0x07FF;

static void value_text(int row, char *buf, size_t cap) {
    switch (row) {
    case ROW_KEYBOARD: snprintf(buf, cap, "%s", KEYBOARD_NAMES[a8_settings.keyboard]); break;
    case ROW_FPS:      snprintf(buf, cap, "%s", a8_settings.fps ? "On" : "Off"); break;
    case ROW_VOLUME:   snprintf(buf, cap, "%d%%", pico_audio_get_volume_pct()); break;
    case ROW_MACHINE:  snprintf(buf, cap, "%s", a8_settings.machine_800 ? "400/800" : "800XL"); break;
    case ROW_TV:       snprintf(buf, cap, "%s", a8_settings.pal ? "PAL" : "NTSC"); break;
    case ROW_PADDLES:  snprintf(buf, cap, "%s", PADDLES_NAMES[a8_settings.paddles]); break;
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
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(320 - 8 - tft.textWidth("v" A8_VERSION), 6);
    tft.print("v" A8_VERSION);
    tft.drawFastHLine(0, 25, 320, COL_HINT);
    for (int r = 0; r < ROWS; r++) draw_row(r, r == sel);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print("LEFT / RIGHT: change    MENU: back");
}

/* left / right on a row (dir -1 / +1) */
static bool change(int row, int dir) {
    switch (row) {
    case ROW_KEYBOARD: a8_settings.keyboard = (a8_settings.keyboard + dir + A8_KBD_MODES) % A8_KBD_MODES; return true;
    case ROW_FPS:      a8_settings.fps = !a8_settings.fps; return true;
    case ROW_MACHINE:  a8_settings.machine_800 = !a8_settings.machine_800; return true;
    case ROW_TV:       a8_settings.pal = !a8_settings.pal; return true;
    case ROW_PADDLES: {                      /* stops at the ends */
        int v = a8_settings.paddles + dir;
        if (v < 0 || v >= A8_PADDLES_LEVELS) return false;
        a8_settings.paddles = v;
        return true;
    }
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
            else if (sel == ROW_TEST) {
                if (pressed & A8M_OK) {
                    while (a8_input_menu() & A8M_OK) delay(10);
                    a8_ctltest_screen();
                    draw_screen(sel);
                    prev = 0xFF;                 /* MENU still down: not a "back" */
                    continue;
                }
            } else {
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
