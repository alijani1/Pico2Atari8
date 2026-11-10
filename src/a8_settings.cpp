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
 * From 2.1 that is each game's Game Settings (a8_gamecfg.cpp): what Red,
 * Green, Yellow, the click and the side buttons do, and the Joy-Con as
 * a paddle.
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

A8Settings a8_settings = { A8_KBD_ONE_KEY, false, false, false, A8_PADDLES_MEDIUM, A8_SCALE_SMOOTH };

static const char *const KEYBOARD_NAMES[A8_KBD_MODES] = { "Pause+Auto Resume", "Auto-pause", "Keep running" };
static const char *const PADDLES_NAMES[A8_PADDLES_LEVELS] = { "Very low", "Low", "Medium", "High", "Very high" };

/* The games whose Joy-Con is a paddle, at GAMES_AT in the sector. */
/* (Pico2Atari8 2.0: only that list, "JOYP". From 2.1 each game has a
 * Game Settings record instead, "GCF1" - the button mapping, the Joy-Con
 * as a paddle - and a 2.0 list is turned into records at load.) */
static const uint32_t GAMES_MAGIC = 0x50594F4Au;  /* "JOYP" */
static const int MAX_GAMES = 250, GAMES_AT = 64;
struct StoredGames {
    uint32_t magic, count;
    uint32_t hash[MAX_GAMES];
    uint32_t check;
};

static uint32_t games_check(const uint32_t *h, uint32_t n) {
    uint32_t c = GAMES_MAGIC ^ n;
    for (uint32_t i = 0; i < n; i++) c = (c << 5 | c >> 27) ^ h[i];
    return c;
}

/* Game Settings, one record a game that isn't on its defaults (a hash of
 * its path; the oldest goes when the list is full). 12 bytes each. */
static const uint32_t CFG_MAGIC = 0x31464347u;    /* "GCF1" */
static const int MAX_CFGS = 300;
enum { CFG_PADDLE = 0x01,                         /* the Joy-Con is paddle 1 */
       CFG_5200 = 0x02 };                         /* btn[] is for a 5200 game */
struct GameCfg {
    uint32_t hash;
    uint8_t flags;
    uint8_t btn[A8_GB_COUNT];                     /* A8_ACT_*; A8_ACT_DEFAULT = as shipped */
    uint8_t spare;
};
struct StoredCfgs {
    uint32_t magic, count;
    GameCfg cfg[MAX_CFGS];
    uint32_t check;
};
static_assert(sizeof(GameCfg) == 12, "GameCfg is 12 bytes");
static_assert(GAMES_AT + sizeof(StoredCfgs) <= 4096, "Game Settings fit the sector");
/* The records are read where they are in flash (no RAM copy: RAM is
 * tight); only the game being played / set up is kept here, and save()
 * merges it into the list it writes. */
static GameCfg cur;                       /* the game being played / set up */
static bool cur_set = false, cur_dirty = false;
static uint32_t this_game = 0;            /* hash of the game being played */
static bool games_dirty = false;

static uint32_t cfgs_check(const GameCfg *g, uint32_t n) {
    uint32_t c = CFG_MAGIC ^ n;
    const uint8_t *b = (const uint8_t *)g;
    for (uint32_t i = 0; i < n * sizeof(GameCfg); i++) c = (c << 5 | c >> 27) ^ b[i];
    return c;
}

/* What each button does when the game has no setting of its own. */
static const uint8_t DEFAULT_800[A8_GB_COUNT] = {
    A8_ACT_FIRE, A8_ACT_OPTION, 0x21 /* AKEY_SPACE */, A8_ACT_FIRE, A8_ACT_NONE, 0x0c /* AKEY_RETURN */ };
static const uint8_t DEFAULT_5200[A8_GB_COUNT] = {
    A8_ACT_FIRE, A8_ACT_TOP_FIRE, 0x27 /* AKEY_5200_ASTERISK */, A8_ACT_FIRE, A8_ACT_NONE, A8_ACT_NONE };

static bool cur_is_default(void);

static uint32_t checksum(const Stored &s) {
    return s.magic ^ (s.keyboard << 1) ^ (s.fps << 3) ^ (s.machine_800 << 5) ^ (s.pal << 7) ^
           (s.paddles << 9) ^ (s.spare[0] << 12) ^ 0xA5A5A5A5u;   /* spare[0]: scale (0 before) */
}

/* The saved records in flash, or nullptr. */
static const StoredCfgs *flash_cfgs(void) {
    const StoredCfgs *c = (const StoredCfgs *)(a8_flash_read(A8_SETTINGS_PHYS) + GAMES_AT);
    if (c->magic == CFG_MAGIC && c->count <= (uint32_t)MAX_CFGS && c->check == cfgs_check(c->cfg, c->count))
        return c;
    return nullptr;
}

/* 2.0's list of games with the Joy-Con as a paddle, or nullptr. */
static const StoredGames *flash_joyp(void) {
    const StoredGames *g = (const StoredGames *)(a8_flash_read(A8_SETTINGS_PHYS) + GAMES_AT);
    if (g->magic == GAMES_MAGIC && g->count <= (uint32_t)MAX_GAMES && g->check == games_check(g->hash, g->count))
        return g;
    return nullptr;
}

/* The i-th saved record (of n), from either form. */
static int saved_count(void) {
    if (const StoredCfgs *c = flash_cfgs()) return (int)c->count;
    if (const StoredGames *g = flash_joyp()) return (int)g->count;
    return 0;
}
static GameCfg saved_cfg(int i) {
    if (const StoredCfgs *c = flash_cfgs()) return c->cfg[i];
    GameCfg r;
    r.hash = flash_joyp()->hash[i];          /* 2.0: the Joy-Con as a paddle */
    r.flags = CFG_PADDLE;
    memset(r.btn, A8_ACT_DEFAULT, sizeof(r.btn));
    r.spare = 0;
    return r;
}

static void load_games(void) {
    if (flash_cfgs()) Serial.printf("[settings] %d games with Game Settings\n", saved_count());
    else if (flash_joyp()) {
        Serial.printf("[settings] %d games with the Joy-Con as a paddle (2.0 list)\n", saved_count());
        games_dirty = saved_count() > 0;     /* saved in the new form when a game ends */
    }
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
    a8_settings.scale = s.spare[0] < A8_SCALE_MODES ? s.spare[0] : A8_SCALE_SMOOTH;
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
    s.spare[0] = (uint8_t)a8_settings.scale;
    s.check = checksum(s);
    memcpy(sector, &s, sizeof(s));
    /* the saved records, but the current game's as it is now (at the end:
     * the newest; left out if back on its defaults; the oldest goes when
     * the list is full) */
    StoredCfgs *g = (StoredCfgs *)(sector + GAMES_AT);
    bool add = cur_set && cur_dirty && !cur_is_default();
    int n = 0, total = saved_count();
    for (int i = 0; i < total; i++) {
        GameCfg r = saved_cfg(i);
        if (cur_set && cur_dirty && r.hash == cur.hash) continue;
        g->cfg[n++] = r;
    }
    if (add) {
        if (n == MAX_CFGS) { memmove(g->cfg, g->cfg + 1, (MAX_CFGS - 1) * sizeof(GameCfg)); n--; }
        g->cfg[n++] = cur;
    }
    g->magic = CFG_MAGIC;
    g->count = (uint32_t)n;
    g->check = cfgs_check(g->cfg, g->count);
    cur_dirty = false;
    pico_audio_suspend();                    /* its DMA can't ride out a flash write */
    a8_flash_write_sector(A8_SETTINGS_PHYS, sector);
    pico_audio_resume();
    Serial.println("[settings] saved");
}

/* ---- per game ------------------------------------------------------------ */

static int game_index(void);

void a8_settings_set_game(const char *path) {
    uint32_t h = 2166136261u;                     /* FNV-1a, case-blind */
    for (const char *p = path; *p; p++) h = (h ^ (uint8_t)tolower((unsigned char)*p)) * 16777619u;
    this_game = h;
    int i = game_index();
    cur_set = true;
    cur_dirty = false;
    if (i >= 0) {
        cur = saved_cfg(i);
    } else {
        cur.hash = h;
        cur.flags = 0;
        memset(cur.btn, A8_ACT_DEFAULT, sizeof(cur.btn));
        cur.spare = 0;
    }
}

/* this_game's saved record (saved_cfg), or -1 */
static int game_index(void) {
    int n = saved_count();
    for (int i = 0; i < n; i++) if (saved_cfg(i).hash == this_game) return i;
    return -1;
}

static bool cur_is_default(void) {
    if (cur.flags & CFG_PADDLE) return false;
    for (uint8_t b : cur.btn) if (b != A8_ACT_DEFAULT) return false;
    return true;
}

/* `cur` changed: save() puts it into the list (at the end, the newest),
 * or leaves it out if it's back on its defaults. Saved when the game
 * ends / the screen closes. */
static void cur_changed(void) {
    cur_dirty = true;
    games_dirty = true;
}

bool a8_settings_game_paddle(void) { return (cur.flags & CFG_PADDLE) != 0; }

void a8_settings_set_game_paddle(bool on) {
    if (on == a8_settings_game_paddle()) return;
    cur.flags = on ? (cur.flags | CFG_PADDLE) : (cur.flags & ~CFG_PADDLE);
    cur_changed();
}

/* A button's mapping set for the other kind of game (a zip set up from the
 * game list as an 800 game that holds a 5200 one) doesn't count. */
static bool btn_kind_ok(bool is5200) { return ((cur.flags & CFG_5200) != 0) == is5200; }

uint8_t a8_settings_game_action(int slot, bool is5200) {
    if (slot < 0 || slot >= A8_GB_COUNT) return A8_ACT_NONE;
    uint8_t a = btn_kind_ok(is5200) ? cur.btn[slot] : A8_ACT_DEFAULT;
    if (a != A8_ACT_DEFAULT) return a;
    return is5200 ? DEFAULT_5200[slot] : DEFAULT_800[slot];
}

uint8_t a8_settings_default_action(int slot, bool is5200) {
    if (slot < 0 || slot >= A8_GB_COUNT) return A8_ACT_NONE;
    return is5200 ? DEFAULT_5200[slot] : DEFAULT_800[slot];
}

void a8_settings_set_game_action(int slot, bool is5200, uint8_t act) {
    if (slot < 0 || slot >= A8_GB_COUNT) return;
    if (!btn_kind_ok(is5200)) {                  /* the other kind's mapping goes */
        memset(cur.btn, A8_ACT_DEFAULT, sizeof(cur.btn));
        cur.flags = is5200 ? (cur.flags | CFG_5200) : (cur.flags & ~CFG_5200);
    }
    cur.btn[slot] = act == a8_settings_default_action(slot, is5200) ? A8_ACT_DEFAULT : act;
    cur_changed();
}

bool a8_settings_game_buttons_custom(bool is5200) {
    if (!btn_kind_ok(is5200)) return false;
    for (uint8_t b : cur.btn) if (b != A8_ACT_DEFAULT) return true;
    return false;
}

void a8_settings_game_reset(void) {
    cur.flags = 0;
    memset(cur.btn, A8_ACT_DEFAULT, sizeof(cur.btn));
    cur_changed();
}

/* Scale (160x80 panel): the next mode, at once; saved when the game ends. */
void a8_settings_next_scale(void) {
    a8_settings.scale = (a8_settings.scale + 1) % A8_SCALE_MODES;
    games_dirty = true;
}

/* The display must be stopped: its frame buffer is the sector buffer. */
void a8_settings_save_game(void) {
    if (!games_dirty) return;
    save();
    games_dirty = false;
}

/* ---- the screen -------------------------------------------------------- */

/* The rows, in the order shown; which there are depends on the device. */
enum { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_PADDLES, ROW_SCALE, ROW_TEST, ROW_BACK };
#if defined(PICO_TINY_SCREEN)
static const int ROW_LIST[] = { ROW_SCALE, ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV,
                                ROW_PADDLES, ROW_TEST, ROW_BACK };
#elif defined(PICO_QUAD)
static const int ROW_LIST[] = { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_PADDLES,
                                ROW_TEST, ROW_BACK };
#else
static const int ROW_LIST[] = { ROW_KEYBOARD, ROW_FPS, ROW_VOLUME, ROW_MACHINE, ROW_TV, ROW_TEST, ROW_BACK };
#endif
static const int ROWS = sizeof(ROW_LIST) / sizeof(ROW_LIST[0]);
#ifdef PICO_TINY_SCREEN
/* 160x80: the 6x8 font, 10 px rows, 7 at a time (the list scrolls), names
 * short enough to leave room for the value (26 characters a row). */
static const char *const NAMES[] = { "Keyboard", "FPS display", "Volume", "Machine", "TV system",
                                     "Paddles", "Scale", "Controller test", "Back" };
static const int ROW_H = 10, LIST_Y = 10, SHOWN = 7, PANEL_W = 160, FONT = 1, TEXT_X = 2;
#else
static const char *const NAMES[] = { "Keyboard", "FPS display", "Volume", "Default machine", "TV system",
                                     "Paddle sensitivity", "Scale", "Controller test", "Back" };
static const int ROW_H = 22, LIST_Y = 34, SHOWN = ROWS, PANEL_W = 320, FONT = 2, TEXT_X = 16;
#endif
static int first_shown = 0;              /* the list's top row (scrolls on the 160x80 panel) */
static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_SEL_BG = 0x2A69,
                      COL_TITLE = 0xFD20, COL_HINT = TFT_DARKGREY, COL_VALUE = 0x07FF;

static const char *const SCALE_NAMES[A8_SCALE_MODES] = { "Smooth", "Sharp", "Drop", "Crop" };

static void value_text(int row, char *buf, size_t cap) {
    switch (row) {
    case ROW_KEYBOARD: snprintf(buf, cap, "%s", KEYBOARD_NAMES[a8_settings.keyboard]); break;
    case ROW_FPS:      snprintf(buf, cap, "%s", a8_settings.fps ? "On" : "Off"); break;
    case ROW_VOLUME:   snprintf(buf, cap, "%d%%", pico_audio_get_volume_pct()); break;
    case ROW_MACHINE:  snprintf(buf, cap, "%s", a8_settings.machine_800 ? "400/800" : "800XL"); break;
    case ROW_TV:       snprintf(buf, cap, "%s", a8_settings.pal ? "PAL" : "NTSC"); break;
    case ROW_PADDLES:  snprintf(buf, cap, "%s", PADDLES_NAMES[a8_settings.paddles]); break;
    case ROW_SCALE:    snprintf(buf, cap, "%s", SCALE_NAMES[a8_settings.scale]); break;
    default:           buf[0] = 0; break;
    }
}

/* list position i (an index into ROW_LIST) */
static void draw_row(int i, bool sel) {
    if (i < first_shown || i >= first_shown + SHOWN) return;
    TFT_eSPI &tft = a8_tft();
    const int row = ROW_LIST[i];
    int y = LIST_Y + (i - first_shown) * ROW_H;
    uint16_t bg = sel ? COL_SEL_BG : COL_BG;
    tft.fillRect(0, y, PANEL_W, ROW_H, bg);
    tft.setTextFont(FONT);
    tft.setTextColor(COL_TEXT, bg);
    tft.setCursor(TEXT_X, y + (FONT == 1 ? 1 : 5));
    tft.print(NAMES[row]);
    char v[24];
    value_text(row, v, sizeof(v));
    if (v[0]) {
        tft.setTextColor(sel ? COL_TEXT : COL_VALUE, bg);
        tft.setCursor(PANEL_W - TEXT_X - tft.textWidth(v), y + (FONT == 1 ? 1 : 5));
        tft.print(v);
    }
}

static void draw_screen(int sel) {
    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(FONT);
    tft.setTextWrap(false);
    tft.setTextColor(COL_TITLE, COL_BG);
#ifdef PICO_TINY_SCREEN
    tft.setCursor(TEXT_X, 0);
    tft.print("SETTINGS");
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(PANEL_W - TEXT_X - tft.textWidth("v" A8_VERSION), 0);
    tft.print("v" A8_VERSION);
    tft.drawFastHLine(0, 8, PANEL_W, COL_HINT);
    for (int i = 0; i < ROWS; i++) draw_row(i, i == sel);
    /* more above / below: a mark at the right edge */
    if (first_shown > 0) tft.fillTriangle(156, LIST_Y + 1, 153, LIST_Y + 4, 159, LIST_Y + 4, COL_HINT);
    if (first_shown + SHOWN < ROWS) {
        int yb = LIST_Y + SHOWN * ROW_H - 2;
        tft.fillTriangle(156, yb, 153, yb - 3, 159, yb - 3, COL_HINT);
    }
#else
    tft.setCursor(8, 6);
    tft.print("SETTINGS");
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(320 - 8 - tft.textWidth("v" A8_VERSION), 6);
    tft.print("v" A8_VERSION);
    tft.drawFastHLine(0, 25, 320, COL_HINT);
    for (int i = 0; i < ROWS; i++) draw_row(i, i == sel);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print("LEFT / RIGHT: change    MENU: back");
#endif
}

/* left / right on a row (dir -1 / +1) */
static bool change(int row, int dir) {
    switch (row) {
    case ROW_KEYBOARD: a8_settings.keyboard = (a8_settings.keyboard + dir + A8_KBD_MODES) % A8_KBD_MODES; return true;
    case ROW_FPS:      a8_settings.fps = !a8_settings.fps; return true;
    case ROW_MACHINE:  a8_settings.machine_800 = !a8_settings.machine_800; return true;
    case ROW_TV:       a8_settings.pal = !a8_settings.pal; return true;
    case ROW_SCALE:    a8_settings.scale = (a8_settings.scale + dir + A8_SCALE_MODES) % A8_SCALE_MODES; return true;
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
    first_shown = 0;
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
        if (sel != old) {
            int top = first_shown;
            if (sel < first_shown) first_shown = sel;
            if (sel >= first_shown + SHOWN) first_shown = sel - SHOWN + 1;
            if (first_shown != top) draw_screen(sel);
            else { draw_row(old, false); draw_row(sel, true); }
        }
        const int row = ROW_LIST[sel];
        if (pressed & (A8M_LEFT | A8M_RIGHT | A8M_OK)) {
            if (row == ROW_BACK) { if (pressed & A8M_OK) break; }
            else if (row == ROW_TEST) {
                if (pressed & A8M_OK) {
                    while (a8_input_menu() & A8M_OK) delay(10);
                    a8_ctltest_screen();
                    draw_screen(sel);
                    prev = 0xFF;                 /* MENU still down: not a "back" */
                    continue;
                }
            } else {
                int dir = (pressed & A8M_LEFT) ? -1 : 1;
                if (change(row, dir)) dirty = true;
                draw_row(sel, true);
            }
        }
        if (pressed & A8M_BACK) break;
        delay(10);
    }
    while (a8_input_menu() & (A8M_BACK | A8M_OK)) delay(10);
    if (dirty) save();
}
