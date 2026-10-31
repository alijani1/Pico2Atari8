/* main.cpp - Pico2Atari8: Atari 8-bit computer emulation (atari800) on the
 * Quad Ultimate CX.
 *
 * Core 0: the game list, then the emulator (atari800, paced by the audio
 * ring). Core 1: pushes each finished frame to the TFT (a8_display.cpp).
 *
 * The emulator runs on its own 32 KB stack (emu_stack): setup()/loop() get
 * core 0's 4 KB scratch bank, and atari800 goes far deeper than that - it
 * keeps FILENAME_MAX (1 KB) path buffers on the stack, several calls deep,
 * when it starts a machine (tools/host measured ~29 KB on the PC, where
 * FILENAME_MAX is 4 KB). Overflowing it froze the Quad when a game started.
 *
 * Chain-booted by the Quad firmware (see platformio.ini): MENU in the game
 * list returns to the Quad; MENU held in a game reboots into the game list
 * (pico_handoff), which also starts each game from a clean machine.
 */
#include <Arduino.h>
#include "a8_display.h"
#include "a8_input.h"
#include "a8_menu.h"
#include "a8_platform.h"
#include "pico_audio.h"
#include "pico_board.h"
#include "pico_drive.h"
#include "pico_handoff.h"
#include "pico_romfs.h"
#include "a8_zip.h"
#include "a8_flash.h"
#include "a8_settings.h"

extern "C" {
#include "atari.h"
#include "cpu.h"
#include "platform.h"
}

static volatile bool g_emulating = false;
static volatile bool g_heartbeat = false;
static volatile uint32_t g_frames = 0;
volatile const char *a8_stage = "menu";

/* atari800's Log_print and our own debug lines. */
extern "C" void a8_log_write(const char *line) {
    Serial.printf("[a8] %s\n", line);
}

extern volatile uint32_t a8_fault_pc, a8_fault_lr, a8_fault_cfsr;
void a8_debug_init(void);

extern "C" uint64_t a8_time_us(void) { return time_us_64(); }
extern "C" void a8_idle(void) { tight_loop_contents(); }

/* ---- the emulator's stack ------------------------------------------- */

static const uint32_t EMU_STACK_WORDS = 8192;           /* 32 KB */
static const uint32_t STACK_FILL = 0xA8A8A8A8u;
static uint32_t emu_stack[EMU_STACK_WORDS] __attribute__((aligned(8)));

/* Run fn() with the stack pointer on emu_stack, then switch back. r4 keeps
 * the old SP (callee-saved, so fn leaves it alone). */
static void __attribute__((noinline)) on_emu_stack(void (*fn)(void)) {
    void *top = emu_stack + EMU_STACK_WORDS;
    asm volatile(
        "mov r4, sp\n"
        "mov sp, %1\n"
        "blx %0\n"
        "mov sp, r4\n"
        :
        : "r"(fn), "r"(top)
        : "r0", "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc");
}

/* Bytes of emu_stack ever used (the fill pattern still intact below). */
static uint32_t emu_stack_used(void) {
    uint32_t i = 0;
    while (i < EMU_STACK_WORDS && emu_stack[i] == STACK_FILL) i++;
    return (EMU_STACK_WORDS - i) * 4;
}

/* ---- start-up --------------------------------------------------------- */

static char game_file[96];
static bool start_as_800 = false;
static bool start_as_5200 = false;

/* An 800XL with 64 KB (or an 800 with 48 KB for OS-B games), NTSC, BASIC
 * off (as if OPTION were held at power-on), running the chosen file.
 * AltirraOS is built in; real Atari ROM files on the drive (ATARIXL.ROM,
 * ATARIOSB.ROM - any folder, pico_romfs.cpp) are used instead when present: some
 * early games jump into the original OS and crash on AltirraOS.
 * A 5200 game runs on a 5200 (16 KB, NTSC only): its BIOS from 5200.ROM if
 * the drive has it, else the built-in Altirra 5200 OS. (A .car names its
 * type, and the core switches to the 5200 for a 5200 one by itself.) */
static void emu_start(void) {
    static char arg0[] = "atari800", xl[] = "-xl", atari[] = "-atari", a5200[] = "-5200",
                ntsc[] = "-ntsc", pal[] = "-pal",
                nobasic[] = "-nobasic", colors[] = "-colors-preset", vibrant[] = "vibrant",
                osb_opt[] = "-osb_rom", osb_file[] = "/ATARIOSB.ROM",
                xl_opt[] = "-xlxe_rom", xl_file[] = "/ATARIXL.ROM",
                bios_opt[] = "-5200_rom", bios_file[] = "/5200.ROM";
    char *argv[16];
    int argc = 0;
    argv[argc++] = arg0;
    argv[argc++] = start_as_5200 ? a5200 : start_as_800 ? atari : xl;
    argv[argc++] = a8_settings.pal && !start_as_5200 ? pal : ntsc;
    argv[argc++] = nobasic;
    argv[argc++] = colors;          /* atari800's vibrant palette: its stock one */
    argv[argc++] = vibrant;         /* looked dull on the panel (brownish reds) */
    if (romfs_exists(osb_file)) { argv[argc++] = osb_opt; argv[argc++] = osb_file; }
    if (romfs_exists(xl_file))  { argv[argc++] = xl_opt;  argv[argc++] = xl_file; }
    if (romfs_exists(bios_file)) { argv[argc++] = bios_opt; argv[argc++] = bios_file; }
    argv[argc++] = game_file;
    argv[argc] = nullptr;
    Serial.printf("[a8] %s %s, OS-B ROM file %s, XL ROM file %s, 5200 BIOS file %s\n",
                  start_as_5200 ? "5200" : start_as_800 ? "800" : "800XL", argv[2] == pal ? "PAL" : "NTSC",
                  romfs_exists(osb_file) ? "yes" : "no",
                  romfs_exists(xl_file) ? "yes" : "no",
                  romfs_exists(bios_file) ? "yes" : "no");
    a8_stage = "Atari800_Initialise";
    if (!Atari800_Initialise(&argc, argv))
        Serial.println("[a8] Atari800_Initialise failed");
    a8_stage = "initialised";
}

static volatile bool g_gave_up = false;


static void emu_frame(void) {
    if (g_frames == 0) a8_stage = "first frame";
    a8_frame();
    if (a8_crashed && !a8_handle_crash(game_file)) g_gave_up = true;
    g_frames++;
}

/* What the drive gives for the file (rules out a wrong drive offset). */
static void log_file(const char *path) {
    const uint8_t *p;
    size_t size;
    if (romfs_map(path, &p, &size)) {
        Serial.printf("[a8] %s: %u bytes in place, starts %02x %02x %02x %02x %02x %02x %02x %02x\n",
                      path, (unsigned)size, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    } else {
        Serial.printf("[a8] %s: fragmented (copied to RAM on open)\n", path);
    }
}

void setup() {
    Serial.begin(115200);
    a8_debug_init();
    a8_flash_init();
    a8_settings_load();
    int vol = handoff_volume_read();
    pico_audio_set_volume_pct(vol >= 0 ? vol : 50);
    board_pins_load();                 /* audio / amp pins (Quad profile) */
    romfs_init();
    a8_input_init();
    a8_display_init();
    if (pico_drive_requested()) pico_drive_run();      /* never returns */

    const char *path = a8_menu_pick();
    strncpy(game_file, path, sizeof(game_file) - 1);
    a8_settings_set_game(path);        /* its own settings (the zip's path for a zip) */
    /* the default machine, or the other one if picked with SELECT; a
     * 400/800 game by its name always on the 800 */
    start_as_800 = (a8_settings.machine_800 != a8_menu_picked_alt()) || a8_menu_picked_osb();
    log_file(game_file);

    /* A zip: unpack its game into the flash area first (before the audio
     * starts: flash writes stop interrupts) and run it from there. */
    const char *dot = strrchr(path, '.');
    if (dot && strcasecmp(dot, ".zip") == 0) {
        TFT_eSPI &tft = a8_tft();
        tft.fillScreen(TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(110, 110);
        tft.print("Unpacking...");
        char inner[64];
        if (!a8_unzip(path, inner, sizeof(inner))) {
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(40, 100); tft.print("Can't unpack this zip.");
            tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            tft.setCursor(40, 122); tft.print("Press MENU for the game list.");
            while (digitalRead(18) == LOW) delay(10);
            while (digitalRead(18) == HIGH) delay(10);
            handoff_relaunch_arcade();
        }
        snprintf(game_file, sizeof(game_file), "%s%s", A8_UNZIP_PREFIX, inner);
        char low[64];
        size_t n = strlen(inner);
        for (size_t i = 0; i <= n; i++) low[i] = (char)tolower((unsigned char)inner[i]);
        if (strstr(low, "osb") || strstr(low, "os-b") || strstr(low, "400-800")) start_as_800 = true;
    }
    /* a 5200 game: .a52, or a raw cartridge that looks like one (one that
     * can't be told is a 5200 one on the Duo 5200) */
#ifdef PICO_5200
    start_as_5200 = a8_file_is_5200(game_file, 1);
#else
    start_as_5200 = a8_file_is_5200(game_file, 0);
#endif

    Serial.println("[a8] starting audio");
    pico_audio_init(A8_SAMPLE_RATE);
    Serial.println("[a8] audio running");

    for (uint32_t i = 0; i < EMU_STACK_WORDS; i++) emu_stack[i] = STACK_FILL;
    Serial.printf("[a8] heap free %d bytes before start\n", rp2040.getFreeHeap());
    g_heartbeat = true;
    on_emu_stack(emu_start);
    Serial.printf("[a8] started, emulator stack %lu bytes used, heap free %d bytes\n",
                  (unsigned long)emu_stack_used(), rp2040.getFreeHeap());
    a8_display_set_fps(a8_settings.fps);
    a8_display_start();
    pico_audio_amp_on();
    g_emulating = true;
}

void loop() {
    if (a8_input_frame()) {
        /* MENU held: back to the game list */
        pico_audio_amp_off();
        a8_display_stop();
        a8_settings_save_game();       /* Joy-Con mode changed: after the display stops */
        handoff_volume_write(pico_audio_get_volume_pct());
        handoff_relaunch_arcade();
    }
    if (g_gave_up) {
        /* crashed on both machines: say so, MENU returns to the list */
        static bool shown = false;
        if (!shown) {
            shown = true;
            a8_display_stop();
            TFT_eSPI &tft = a8_tft();
            tft.fillRect(20, 90, 280, 60, TFT_BLACK);
            tft.drawRect(20, 90, 280, 60, TFT_RED);
            tft.setTextFont(2);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setCursor(32, 100); tft.print("This game crashed the Atari.");
            tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            tft.setCursor(32, 122); tft.print("Hold MENU for the game list.");
        }
        delay(20);
        return;
    }
    if (a8_input_osk_paused()) {
        /* keyboard open: the game holds still, the keyboard is redrawn */
        a8_display_refresh();
        a8_silent_frame();
        return;
    }
    uint32_t t0 = time_us_32();
    on_emu_stack(emu_frame);
    static uint32_t busy = 0, frames = 0, last = 0;
    busy += time_us_32() - t0;
    frames++;
    uint32_t now = millis();
    if (now - last >= 5000) {
        uint32_t shown, skipped;
        a8_display_stats(&shown, &skipped);
        uint32_t rows;
        uint32_t push_us = a8_display_push_us(&rows);
        static uint32_t last_shown = 0;
        float secs = (now - last) / 1000.0f;
        Serial.printf("[a8] emulator %.1f fps, screen %.1f fps (%lu dropped total), "
                      "push %.2f ms (%lu rows), stack %lu\n",
                      frames / secs, (shown - last_shown) / secs, (unsigned long)skipped,
                      push_us / 1000.0f, (unsigned long)rows, (unsigned long)emu_stack_used());
        last_shown = shown;
        busy = frames = 0;
        last = now;
    }
}

/* Core 1: the display, and once a second a heartbeat with core 0's
 * progress - it keeps coming if core 0 hangs or faults. */
void setup1() {}
void loop1() {
    if (g_emulating) a8_display_core1_loop();
    static uint32_t last = 0;
    uint32_t now = millis();
    if (g_heartbeat && now - last >= 1000) {
        last = now;
        uint32_t shown, skipped;
        a8_display_stats(&shown, &skipped);
        if (a8_fault_pc)
            Serial.printf("[hb] CRASH on core 0: pc %08lx lr %08lx cfsr %08lx%c",
                          (unsigned long)a8_fault_pc, (unsigned long)a8_fault_lr,
                          (unsigned long)a8_fault_cfsr, 10);
        Serial.printf("[hb] %s: frames %lu, 6502 PC %04x, shown %lu, stack %lu\n",
                      (const char *)a8_stage, (unsigned long)g_frames, CPU_regPC,
                      (unsigned long)shown, (unsigned long)emu_stack_used());
    }
}
