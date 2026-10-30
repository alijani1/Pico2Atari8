/* a8_menu.cpp - the game list.
 *
 * Lists the Atari files on the ARCADE ROMS drive (root, ATARI and ROMS
 * folders): .xex .com .exe executables, .car .rom .bin cartridges and .atr
 * .xfd disk images and .zip files holding one of those (a8_zip.cpp), by
 * their long names, sorted, then "Settings" (a8_settings.cpp) and
 * "ROM Drive (USB)",
 * which reboots into USB drive mode to copy games over (pico_drive.cpp).
 * Up/down scroll, fire / START picks (an 800XL), SELECT picks as a
 * 400/800 (four joysticks, OS-B), MENU goes back to the Quad.
 */
#include <Arduino.h>
#include <strings.h>
#include "a8_menu.h"
#include "a8_display.h"
#include "a8_input.h"
#include "pico_romfs.h"
#include "pico_handoff.h"
#include "pico_drive.h"
#include "a8_settings.h"

static const int MAX_GAMES = 128;      /* + the Settings and ROM drive rows */
struct Game { char path[24]; char name[40]; bool osb; char kind[5]; };

/* What the file is, shown at the right of its row: two files can share a
 * name ("Slime" = SLIME.BIN and SLIME.ZIP). */
static const char *kind_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "";
    if (!strcasecmp(dot, ".zip")) return "ZIP";
    if (!strcasecmp(dot, ".atr") || !strcasecmp(dot, ".xfd")) return "DISK";
    if (!strcasecmp(dot, ".car") || !strcasecmp(dot, ".rom") || !strcasecmp(dot, ".bin")) return "CART";
    return "XEX";
}
static bool picked_alt = false, picked_osb = false;
bool a8_menu_picked_alt(void) { return picked_alt; }
bool a8_menu_picked_osb(void) { return picked_osb; }
static Game games[MAX_GAMES + 2];
static int found = 0;                  /* games on the drive; then Settings, then the drive */
static int game_count = 0;

/* Atari OS / BASIC ROM files (used by the emulator, not games) */
static bool system_rom(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    static const char *const prefixes[] = { "ATARIOS", "ATARIXL", "ATARIBAS", "5200" };
    for (const char *p : prefixes) if (strncasecmp(name, p, strlen(p)) == 0) return true;
    return false;
}

static bool supported(const char *path) {
    if (system_rom(path)) return false;
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    static const char *const exts[] = { ".xex", ".com", ".exe", ".car", ".rom",
                                        ".bin", ".atr", ".xfd", ".zip" };
    for (const char *e : exts) if (strcasecmp(dot, e) == 0) return true;
    return false;
}

static bool add_game(const char *path, const char *display, uint32_t size, void *) {
    if (!supported(path) || size == 0) return true;
    if (game_count >= MAX_GAMES) return false;
    Game &g = games[game_count++];
    strncpy(g.path, path, sizeof(g.path) - 1);
    g.path[sizeof(g.path) - 1] = 0;
    /* "-osb" / "(OS-B)" / "[400-800]" in the name: a 400/800 game */
    char low[64];
    size_t n = strlen(display);
    if (n >= sizeof(low)) n = sizeof(low) - 1;
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)display[i]);
    low[n] = 0;
    g.osb = strstr(low, "osb") || strstr(low, "os-b") || strstr(low, "400-800");
    strncpy(g.kind, kind_of(path), sizeof(g.kind) - 1);
    g.kind[sizeof(g.kind) - 1] = 0;
    Serial.printf("[menu] %-22s %-5s %7lu  %s\n", path, g.kind, (unsigned long)size, display);
    /* shown without its extension */
    strncpy(g.name, display, sizeof(g.name) - 1);
    g.name[sizeof(g.name) - 1] = 0;
    char *dot = strrchr(g.name, '.');
    if (dot && dot != g.name) *dot = 0;
    return true;
}

static int by_name(const void *a, const void *b) {
    return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name);
}

/* ---- drawing --------------------------------------------------------- */

static const int ROW_H = 18, LIST_Y = 30, ROWS = 10;
static const uint16_t COL_BG = TFT_BLACK, COL_TEXT = TFT_WHITE, COL_SEL_BG = 0x2A69,
                      COL_TITLE = 0xFD20, COL_HINT = TFT_DARKGREY;

static void draw_frame(void) {
    TFT_eSPI &tft = a8_tft();
    tft.fillScreen(COL_BG);
    tft.setTextFont(2);
    tft.setTextColor(COL_TITLE, COL_BG);
    tft.setCursor(8, 6);
    tft.print("ATARI 8-BIT");
    tft.drawFastHLine(0, 25, 320, COL_HINT);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(8, 222);
    tft.print(a8_settings.machine_800 ? "FIRE: play   SELECT: as 800XL   MENU: back"
                                      : "FIRE: play   SELECT: as 400/800   MENU: back");
}

static void draw_row(int idx, int top, bool sel) {
    TFT_eSPI &tft = a8_tft();
    int y = LIST_Y + (idx - top) * ROW_H;
    uint16_t bg = sel ? COL_SEL_BG : COL_BG;
    tft.fillRect(0, y, 320, ROW_H, bg);
    if (idx >= game_count) return;
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, bg);
    tft.setCursor(12, y + 1);
    if (idx >= found) tft.setTextColor(sel ? COL_TEXT : 0x07FF, bg);   /* Settings, drive */
    tft.print(games[idx].name);
    if (idx < found && games[idx].kind[0]) {
        tft.setTextColor(sel ? COL_TEXT : COL_HINT, bg);
        tft.setCursor(320 - 8 - tft.textWidth(games[idx].kind), y + 1);
        tft.print(games[idx].kind);
    }
}

static void draw_counter(int sel) {
    TFT_eSPI &tft = a8_tft();
    tft.setTextFont(2);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.fillRect(240, 6, 80, 16, COL_BG);
    tft.setCursor(250, 6);
    if (sel < found) tft.printf("%d / %d", sel + 1, found);
}

static void draw_list(int top, int sel) {
    for (int i = 0; i < ROWS; i++) draw_row(top + i, top, top + i == sel);
    draw_counter(sel);
}

static void draw_empty(void) {
    TFT_eSPI &tft = a8_tft();
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, COL_BG);
    int y = LIST_Y + 2 * ROW_H + 24;
    tft.setCursor(12, y);       tft.print("No Atari games found.");
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(12, y += 24); tft.print("Pick ROM Drive (USB) and copy");
    tft.setCursor(12, y += 18); tft.print(".xex .car .rom or .atr files onto the");
    tft.setCursor(12, y += 18); tft.print("drive from your computer (root or");
    tft.setCursor(12, y += 18); tft.print("an ATARI folder).");
}

/* ---- the list -------------------------------------------------------- */

const char *a8_menu_pick(void) {
    game_count = 0;
    romfs_list(add_game, nullptr);
    qsort(games, game_count, sizeof(Game), by_name);
    found = game_count;
    Serial.printf("[menu] %d games\n", found);
    Game &settings = games[game_count++];
    settings.path[0] = 0;
    settings.kind[0] = 0;
    strcpy(settings.name, "Settings");
    Game &drive = games[game_count++];
    drive.path[0] = 0;
    drive.kind[0] = 0;
    strcpy(drive.name, "ROM Drive (USB)");

    draw_frame();
    int sel = 0, top = 0;
    draw_list(top, sel);
    if (found == 0) draw_empty();

    /* Wait for everything to be released (the press that started us). */
    for (uint32_t t0 = millis(); a8_input_menu() && millis() - t0 < 1000; ) delay(10);

    uint8_t prev = A8M_OK | A8M_BACK | A8M_ALT;   /* a button still down isn't a new press */
    uint32_t held_since = 0, last_repeat = 0;
    /* A self-centring stick overshoots when let go: UP released can read as
     * a short DOWN. A new press of the opposite direction within this long
     * of a release is ignored. */
    static const uint32_t OVERSHOOT_MS = 200;
    uint32_t released_at = 0;
    uint8_t released_dir = 0;
    for (;;) {
        uint8_t b = a8_input_menu() & ~(A8M_LEFT | A8M_RIGHT);   /* up / down only */
        uint8_t pressed = b & ~prev;
        uint32_t now = millis();
        uint8_t let_go = prev & ~b & (A8M_UP | A8M_DOWN | A8M_LEFT | A8M_RIGHT);
        if (let_go) { released_dir = let_go; released_at = now; }
        if (now - released_at < OVERSHOOT_MS) {
            uint8_t opposite = 0;
            if (released_dir & A8M_UP)    opposite |= A8M_DOWN;
            if (released_dir & A8M_DOWN)  opposite |= A8M_UP;
            if (released_dir & A8M_LEFT)  opposite |= A8M_RIGHT;
            if (released_dir & A8M_RIGHT) opposite |= A8M_LEFT;
            pressed &= ~opposite;
            b &= ~opposite;            /* nor does it count as held */
        }
        /* auto-repeat for held directions */
        uint8_t dirs = b & (A8M_UP | A8M_DOWN | A8M_LEFT | A8M_RIGHT);
        if (dirs && dirs == (prev & dirs)) {
            if (!held_since) held_since = now;
            if (now - held_since > 400 && now - last_repeat > 70) { pressed |= dirs; last_repeat = now; }
        } else {
            held_since = 0;
        }
        prev = b;

        if (pressed & A8M_BACK) {
            /* MENU: back to the Quad on release */
            while (a8_input_menu() & A8M_BACK) delay(10);
            handoff_exit_to_cartreader();
        }
        if (game_count) {
            int old_sel = sel, old_top = top;
            if (pressed & A8M_UP)    sel = sel > 0 ? sel - 1 : game_count - 1;
            if (pressed & A8M_DOWN)  sel = sel < game_count - 1 ? sel + 1 : 0;
            if (sel < top) top = sel;
            if (sel >= top + ROWS) top = sel - ROWS + 1;
            if (top != old_top) draw_list(top, sel);
            else if (sel != old_sel) {
                draw_row(old_sel, top, false);
                draw_row(sel, top, true);
                draw_counter(sel);
            }
            if (pressed & (A8M_OK | A8M_ALT)) {
                bool as_800 = (pressed & A8M_ALT) != 0;
                while (a8_input_menu() & (A8M_OK | A8M_ALT)) delay(10);
                if (sel == found) {                         /* Settings */
                    a8_settings_screen();
                    draw_frame();
                    draw_list(top, sel);
                    if (found == 0) draw_empty();
                    prev = 0xFF;
                    continue;
                }
                if (sel == found + 1) pico_drive_request();  /* reboots into drive mode */
                picked_alt = as_800;
                picked_osb = games[sel].osb;
                Serial.printf("[menu] run %s (%s)%s\n", games[sel].path, games[sel].name,
                              as_800 ? " - the other machine" : "");
                return games[sel].path;
            }
        }
        delay(10);
    }
}
