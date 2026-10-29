/* harness.c - run Pico2Atari8's emulator on the PC.
 *
 * The same lib/atari800 core, src/a8_platform.c and src/a8_stubs.c the
 * device runs; only the Pico side (display, audio, time) is stubbed here.
 * Starts the machine exactly as main.cpp does (800XL, NTSC, no BASIC, the
 * file), runs N frames and reports:
 *   - a hash of each frame's 320x240 picture, so a hang ("same frame
 *     forever") or a crash is visible, plus where the 6502 is (PC);
 *   - the peak heap in use, to compare with the Pico's free heap;
 *   - the last frame as a PPM picture (the device's crop and palette).
 *
 * Usage: a8_harness <file> [frames=600] [out.ppm]
 *        env START_AT=n  hold START from frame n for 10 frames
 *            FIRE_AT=n   hold fire on stick 1 from frame n for 10 frames
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <time.h>

#include "atari.h"
#include "cpu.h"
#include "memory.h"
#include "screen.h"
#include "colours.h"
#include "platform.h"
#include "a8_platform.h"

/* ---- heap accounting (what the Pico would need) ------------------------ */

#ifdef _WIN32
#define block_size(p) _msize(p)
#else
#define block_size(p) malloc_usable_size(p)
#endif

static size_t heap_now, heap_peak;
static void *track(void *p, size_t n) {
    if (p) { heap_now += block_size(p); if (heap_now > heap_peak) heap_peak = heap_now; }
    (void)n;
    return p;
}
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) { return track(__real_malloc(n), n); }
void *__wrap_calloc(size_t a, size_t b) { return track(__real_calloc(a, b), a * b); }
void *__wrap_realloc(void *p, size_t n) {
    if (p) heap_now -= block_size(p);
    return track(__real_realloc(p, n), n);
}
void __wrap_free(void *p) { if (p) heap_now -= block_size(p); __real_free(p); }

/* ---- the Pico side, stubbed ------------------------------------------- */

static long frame_no;
static uint32_t last_hash;
static int same_count;

uint64_t a8_time_us(void) { return (uint64_t)clock() * 1000000ull / CLOCKS_PER_SEC; }
void a8_idle(void) {}

void pico_audio_init(int rate) { (void)rate; }
size_t pico_audio_push_s16(const void *d, size_t n) { (void)d; return n; }

/* the device shows the middle 320 columns (a8_display.cpp) */
static uint32_t frame_hash(void) {
    const uint8_t *s = (const uint8_t *)Screen_atari;
    uint32_t h = 2166136261u;
    for (int y = 0; y < 240; y++)
        for (int x = 32; x < 352; x++) h = (h ^ s[y * Screen_WIDTH + x]) * 16777619u;
    return h;
}

void a8_display_frame(void) {
    uint32_t h = frame_hash();
    if (h == last_hash) same_count++;
    else {
        if (same_count > 30) printf("  (frame unchanged for %d frames)\n", same_count);
        same_count = 0;
    }
    if (frame_no < 5 || frame_no % 60 == 0 || h != last_hash)
        if (frame_no < 5 || frame_no % 60 == 0)
            printf("frame %4ld  hash %08x  PC %04x  heap %zu KB\n",
                   frame_no, h, CPU_regPC, heap_now / 1024);
    last_hash = h;
}

static void save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    const uint8_t *s = (const uint8_t *)Screen_atari;
    for (int y = 0; y < 240; y++)
        for (int x = 32; x < 352; x++) {
            int c = Colours_table[s[y * Screen_WIDTH + x]];
            fputc((c >> 16) & 0xff, f); fputc((c >> 8) & 0xff, f); fputc(c & 0xff, f);
        }
    fclose(f);
    printf("saved %s\n", path);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <file> [frames] [out.ppm]\n", argv[0]); return 1; }
    long frames = argc > 2 ? atol(argv[2]) : 600;
    const char *out = argc > 3 ? argv[3] : "frame.ppm";
    long start_at = getenv("START_AT") ? atol(getenv("START_AT")) : -1;
    long fire_at = getenv("FIRE_AT") ? atol(getenv("FIRE_AT")) : -1;

    /* as main.cpp */
    char *a8argv[] = { "atari800", "-xl", "-ntsc", "-nobasic", argv[1], NULL };
    int a8argc = 5;
    if (!Atari800_Initialise(&a8argc, a8argv)) { printf("Atari800_Initialise failed\n"); return 2; }
    printf("initialised: machine %d, %d KB RAM, OS %d, heap %zu KB (peak %zu KB)\n",
           Atari800_machine_type, MEMORY_ram_size, Atari800_os_version,
           heap_now / 1024, heap_peak / 1024);

    for (frame_no = 0; frame_no < frames; frame_no++) {
        memset(&a8_input, 0, sizeof(a8_input));
        a8_input.key = -1;
        if (start_at >= 0 && frame_no >= start_at && frame_no < start_at + 10) a8_input.start = 1;
        if (fire_at >= 0 && frame_no >= fire_at && frame_no < fire_at + 10) a8_input.trig[0] = 1;
        Atari800_Frame();
        if (Atari800_display_screen) PLATFORM_DisplayScreen();   /* as main.cpp */
    }
    if (same_count > 30) printf("  (frame unchanged for the last %d frames)\n", same_count);
    printf("done: %ld frames, heap now %zu KB, peak %zu KB\n", frames, heap_now / 1024, heap_peak / 1024);
    save_ppm(out);
    return 0;
}
