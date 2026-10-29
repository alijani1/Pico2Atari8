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

static char game_file[32];

/* An 800XL with 64 KB, NTSC, BASIC off (as if OPTION were held at power-on),
 * running the chosen file. AltirraOS is built in. */
static void emu_start(void) {
    static char arg0[] = "atari800", xl[] = "-xl", ntsc[] = "-ntsc", nobasic[] = "-nobasic";
    char *argv[] = { arg0, xl, ntsc, nobasic, game_file, nullptr };
    int argc = 5;
    a8_stage = "Atari800_Initialise";
    if (!Atari800_Initialise(&argc, argv))
        Serial.println("[a8] Atari800_Initialise failed");
    a8_stage = "initialised";
}

static void emu_frame(void) {
    if (g_frames == 0) a8_stage = "first frame";
    a8_frame();
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
    int vol = handoff_volume_read();
    pico_audio_set_volume_pct(vol >= 0 ? vol : 50);
    board_pins_load();                 /* audio / amp pins (Quad profile) */
    romfs_init();
    a8_input_init();
    a8_display_init();
    if (pico_drive_requested()) pico_drive_run();      /* never returns */

    const char *path = a8_menu_pick();
    strncpy(game_file, path, sizeof(game_file) - 1);
    log_file(game_file);

    Serial.println("[a8] starting audio");
    pico_audio_init(A8_SAMPLE_RATE);
    Serial.println("[a8] audio running");

    for (uint32_t i = 0; i < EMU_STACK_WORDS; i++) emu_stack[i] = STACK_FILL;
    Serial.printf("[a8] heap free %d bytes before start\n", rp2040.getFreeHeap());
    g_heartbeat = true;
    on_emu_stack(emu_start);
    Serial.printf("[a8] started, emulator stack %lu bytes used, heap free %d bytes\n",
                  (unsigned long)emu_stack_used(), rp2040.getFreeHeap());
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
        Serial.printf("[a8] %lu frames, %.2f ms/frame (incl. audio wait), shown %lu skipped %lu, "
                      "push %.2f ms (%lu rows), stack %lu\n",
                      (unsigned long)frames, busy / 1000.0f / frames,
                      (unsigned long)shown, (unsigned long)skipped,
                      push_us / 1000.0f, (unsigned long)rows, (unsigned long)emu_stack_used());
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
