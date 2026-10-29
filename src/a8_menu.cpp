/* a8_menu.cpp - the game list.
 *
 * Lists the Atari files on the ARCADE ROMS drive (root, ATARI and ROMS
 * folders): .xex .com .exe executables, .car .rom .bin cartridges and .atr
 * .xfd disk images, by their long names, sorted. Up/down scroll (left/right
 * a page), fire / START runs, MENU goes back to the Quad.
 */
#include <Arduino.h>
#include <strings.h>
#include "a8_menu.h"
#include "a8_display.h"
#include "a8_input.h"
#include "pico_romfs.h"
#include "pico_handoff.h"

static const int MAX_GAMES = 128;
struct Game { char path[24]; char name[40]; };
static Game games[MAX_GAMES];
static int game_count = 0;

static bool supported(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    static const char *const exts[] = { ".xex", ".com", ".exe", ".car", ".rom",
                                        ".bin", ".atr", ".xfd" };
    for (const char *e : exts) if (strcasecmp(dot, e) == 0) return true;
    return false;
}

static bool add_game(const char *path, const char *display, uint32_t size, void *) {
    if (!supported(path) || size == 0) return true;
    if (game_count >= MAX_GAMES) return false;
    Game &g = games[game_count++];
    strncpy(g.path, path, sizeof(g.path) - 1);
    g.path[sizeof(g.path) - 1] = 0;
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
    tft.print("FIRE / START: play     MENU: back");
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
    tft.print(games[idx].name);
}

static void draw_counter(int sel) {
    TFT_eSPI &tft = a8_tft();
    tft.setTextFont(2);
    tft.setTextColor(COL_HINT, COL_BG);
    tft.fillRect(240, 6, 80, 16, COL_BG);
    tft.setCursor(250, 6);
    tft.printf("%d / %d", sel + 1, game_count);
}

static void draw_list(int top, int sel) {
    for (int i = 0; i < ROWS; i++) draw_row(top + i, top, top + i == sel);
    draw_counter(sel);
}

static void draw_empty(void) {
    TFT_eSPI &tft = a8_tft();
    tft.setTextFont(2);
    tft.setTextColor(COL_TEXT, COL_BG);
    int y = 60;
    tft.setCursor(12, y);       tft.print("No Atari games found.");
    tft.setTextColor(COL_HINT, COL_BG);
    tft.setCursor(12, y += 28); tft.print("Copy .xex .car .rom or .atr files to");
    tft.setCursor(12, y += 18); tft.print("the ARCADE ROMS drive (root or an");
    tft.setCursor(12, y += 18); tft.print("ATARI folder): on the Quad, open");
    tft.setCursor(12, y += 18); tft.print("Mini-Games > Arcade ROM Drive.");
}

/* ---- the list -------------------------------------------------------- */

const char *a8_menu_pick(void) {
    game_count = 0;
    romfs_list(add_game, nullptr);
    qsort(games, game_count, sizeof(Game), by_name);
    Serial.printf("[menu] %d games\n", game_count);

    draw_frame();
    if (game_count == 0) draw_empty();
    int sel = 0, top = 0;
    if (game_count) draw_list(top, sel);

    /* Wait for everything to be released (the press that started us). */
    for (uint32_t t0 = millis(); a8_input_menu() && millis() - t0 < 1000; ) delay(10);

    uint8_t prev = A8M_OK | A8M_BACK;   /* a button still down isn't a new press */
    uint32_t held_since = 0, last_repeat = 0;
    for (;;) {
        uint8_t b = a8_input_menu();
        uint8_t pressed = b & ~prev;
        uint32_t now = millis();
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
            if (pressed & A8M_LEFT)  sel = sel >= ROWS ? sel - ROWS : 0;
            if (pressed & A8M_RIGHT) sel = sel + ROWS < game_count ? sel + ROWS : game_count - 1;
            if (sel < top) top = sel;
            if (sel >= top + ROWS) top = sel - ROWS + 1;
            if (top != old_top) draw_list(top, sel);
            else if (sel != old_sel) {
                draw_row(old_sel, top, false);
                draw_row(sel, top, true);
                draw_counter(sel);
            }
            if (pressed & A8M_OK) {
                while (a8_input_menu() & A8M_OK) delay(10);
                Serial.printf("[menu] run %s (%s)\n", games[sel].path, games[sel].name);
                return games[sel].path;
            }
        }
        delay(10);
    }
}
