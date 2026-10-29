/* main.cpp - Pico2Atari8: Atari 8-bit computer emulation (atari800) on the
 * Quad Ultimate CX.
 *
 * Core 0: the game list, then the emulator (atari800, paced by the audio
 * ring). Core 1: pushes each finished frame to the TFT (a8_display.cpp).
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
#include "pico_handoff.h"
#include "pico_romfs.h"

extern "C" {
#include "atari.h"
}

static volatile bool g_emulating = false;

extern "C" uint64_t a8_time_us(void) { return time_us_64(); }
extern "C" void a8_idle(void) { tight_loop_contents(); }

void setup() {
    Serial.begin(115200);
    int vol = handoff_volume_read();
    pico_audio_set_volume_pct(vol >= 0 ? vol : 50);
    board_pins_load();                 /* audio / amp pins (Quad profile) */
    romfs_init();
    a8_input_init();
    a8_display_init();

    const char *path = a8_menu_pick();

    /* An 800XL with 64 KB, NTSC, BASIC off (as if OPTION were held at
     * power-on), running the chosen file. AltirraOS is built in. */
    static char arg0[] = "atari800", xl[] = "-xl", ntsc[] = "-ntsc", nobasic[] = "-nobasic";
    static char file[32];
    strncpy(file, path, sizeof(file) - 1);
    char *argv[] = { arg0, xl, ntsc, nobasic, file, nullptr };
    int argc = 5;
    if (!Atari800_Initialise(&argc, argv)) {
        Serial.println("[a8] Atari800_Initialise failed");
    }
    a8_display_start();
    pico_audio_amp_on();
    g_emulating = true;
}

void loop() {
    if (a8_input_frame()) {
        /* MENU held: back to the game list */
        pico_audio_amp_off();
        a8_display_stop();
        handoff_volume_write(pico_audio_get_volume_pct());
        handoff_relaunch_arcade();
    }
    uint32_t t0 = time_us_32();
    Atari800_Frame();
    static uint32_t busy = 0, frames = 0, last = 0;
    busy += time_us_32() - t0;
    frames++;
    uint32_t now = millis();
    if (now - last >= 5000) {
        uint32_t shown, skipped;
        a8_display_stats(&shown, &skipped);
        Serial.printf("[a8] %lu frames, %.2f ms/frame (incl. audio wait), shown %lu skipped %lu\n",
                      (unsigned long)frames, busy / 1000.0f / frames,
                      (unsigned long)shown, (unsigned long)skipped);
        busy = frames = 0;
        last = now;
    }
}

/* Core 1: the display. */
void setup1() {}
void loop1() {
    if (g_emulating) a8_display_core1_loop();
}
