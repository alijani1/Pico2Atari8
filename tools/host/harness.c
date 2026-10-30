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

/* ---- stack depth (build with EXTRA_CFLAGS=-finstrument-functions) ------ */

static char *stack_top;
static size_t stack_max;
static const char *stack_max_at = "";
__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *fn, void *site) {
    (void)fn; (void)site;
    char here;
    if (stack_top && (size_t)(stack_top - &here) > stack_max) stack_max = (size_t)(stack_top - &here);
}
__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *fn, void *site) { (void)fn; (void)site; }

/* ---- A8_MAPPED_CARTS (build with EXTRA_CFLAGS=-DA8_MAPPED_CARTS): files
 * "in flash" are read into this buffer once and handed out in place. ---- */
static unsigned char flash_sim[1 << 20];
static const char *flash_name;
static int flash_len = -1;
const unsigned char *A8_MapFile(const char *filename, int *len) {
    if (flash_len < 0 || !flash_name || strcmp(filename, flash_name) != 0) {
        FILE *f = fopen(filename, "rb");
        if (!f) return NULL;
        flash_len = (int)fread(flash_sim, 1, sizeof(flash_sim), f);
        fclose(f);
        flash_name = filename;
    }
    *len = flash_len;
    return flash_sim;
}
int A8_IsMapped(const void *p) {
    return (const unsigned char *)p >= flash_sim && (const unsigned char *)p < flash_sim + sizeof(flash_sim);
}

/* ---- the Pico side, stubbed ------------------------------------------- */

static long frame_no;
static uint32_t last_hash;
static int same_count;

uint64_t a8_time_us(void) { return (uint64_t)clock() * 1000000ull / CLOCKS_PER_SEC; }
void a8_idle(void) {}
volatile const char *a8_stage = "";
void a8_log_write(const char *line) { printf("[a8] %s\n", line); }

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

/* atari800 exit()s when the 6502 hits a crash opcode (it would open its
 * monitor); report where. */
static void at_exit(void) {
    printf("EXIT at frame %ld, 6502 PC %04x, crash opcode seen: %s\n",
           frame_no, CPU_regPC, CPU_cim_encountered ? "yes" : "no");
}

int main(int argc, char **argv) {
    atexit(at_exit);
    if (argc < 2) { fprintf(stderr, "usage: %s <file> [frames] [out.ppm]\n", argv[0]); return 1; }
    long frames = argc > 2 ? atol(argv[2]) : 600;
    const char *out = argc > 3 ? argv[3] : "frame.ppm";
    long start_at = getenv("START_AT") ? atol(getenv("START_AT")) : -1;
    long fire_at = getenv("FIRE_AT") ? atol(getenv("FIRE_AT")) : -1;

    char top; stack_top = &top;
    /* as main.cpp */
    /* MACHINE=-atari (400/800) or -xl (default, as the device) */
    char *machine = getenv("MACHINE") ? getenv("MACHINE") : "-xl";
    /* A8_ARGS="-osb_rom /path/ATARIOSB.ROM -800-rev b-ntsc": more options */
    char *a8argv[32] = { "atari800", machine, "-ntsc", "-nobasic", "-colors-preset", "vibrant" };
    int a8argc = 6;
    static char extra[512];
    if (getenv("A8_ARGS")) {
        strncpy(extra, getenv("A8_ARGS"), sizeof(extra) - 1);
        for (char *t = strtok(extra, " "); t && a8argc < 30; t = strtok(NULL, " ")) a8argv[a8argc++] = t;
    }
    a8argv[a8argc++] = argv[1];
    a8argv[a8argc] = NULL;
    if (!Atari800_Initialise(&a8argc, a8argv)) { printf("Atari800_Initialise failed\n"); return 2; }
    if (getenv("PALETTE")) {          /* the 256 colours as a 16 x 16 grid (hue x lum) */
        FILE *pf = fopen(getenv("PALETTE"), "wb");
        fprintf(pf, "P6\n256 256\n255\n");
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                int c = Colours_table[(y / 16) * 16 + x / 16];
                fputc((c >> 16) & 0xff, pf); fputc((c >> 8) & 0xff, pf); fputc(c & 0xff, pf);
            }
        fclose(pf);
    }
    printf("initialised: machine %d, %d KB RAM, OS %d, heap %zu KB (peak %zu KB)\n",
           Atari800_machine_type, MEMORY_ram_size, Atari800_os_version,
           heap_now / 1024, heap_peak / 1024);

    for (frame_no = 0; frame_no < frames; frame_no++) {
        memset(&a8_input, 0, sizeof(a8_input));
        a8_input.key = -1;
        if (start_at >= 0 && frame_no >= start_at && frame_no < start_at + 10) a8_input.start = 1;
        if (fire_at >= 0 && frame_no >= fire_at && frame_no < fire_at + 10) a8_input.trig[0] = 1;
        if (getenv("DRIVE_OFF_AT") && frame_no == atol(getenv("DRIVE_OFF_AT"))) a8_toggle_drive1();
        if (getenv("DUMP_AT") && frame_no == atol(getenv("DUMP_AT"))) {  /* RAM -> ram.bin */
            FILE *df = fopen("ram.bin", "wb");
            fwrite(MEMORY_mem, 1, 65536, df);
            fclose(df);
        }
        a8_frame();                                              /* as main.cpp */
        if (a8_crashed && !a8_handle_crash(argv[1])) { printf("crashed on the 800 too\n"); break; }
    }
    if (same_count > 30) printf("  (frame unchanged for the last %d frames)\n", same_count);
    printf("deepest stack: %zu bytes below main (x86-64)\n", stack_max);
    printf("done: %ld frames, heap now %zu KB, peak %zu KB\n", frames, heap_now / 1024, heap_peak / 1024);
    save_ppm(out);
    return 0;
}
