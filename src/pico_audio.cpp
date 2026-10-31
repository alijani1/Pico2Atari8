/* pico_audio.cpp — PWM audio implementation. */

#include <Arduino.h>
#include <PWMAudio.h>
#include "pico_audio.h"
#include "pico_board.h"

#ifdef PICO_BOARD_TABLE
/* Pins from the runtime table (pico_board.h) — the PWMAudio object is created
 * on first init, once the table is loaded. amp = -1: no enable pin. */
#define AUDIO_PIN  g_pins.audio
#define AMP_EN_PIN g_pins.amp
static PWMAudio *g_audio_p = nullptr;
#define g_audio (*g_audio_p)
#else
/* Wiring — same pads as the PicoCartReader VCS emulator so we can share
 * hardware. GP22 = PWM audio out, GP4 = PAM8302 amp enable (active HIGH). */
static const int AUDIO_PIN = 22;
static const int AMP_EN_PIN = 4;

/* PWMAudio wants pin at construction. Create in mono mode. */
static PWMAudio g_audio(AUDIO_PIN, /*stereo=*/false);
#endif

static bool     g_started    = false;
static int      g_sample_rate = 0;
// Volume 0..100 %, applied as gain = pct² / 10000 — the same perceptual
// curve PicoCartReader uses, so a given % sounds the same in both.
static volatile int g_volume_pct = 50;    /* default 50 %: full is too loud */
static volatile int g_gain       = 10000;   // pct², 0..10000

extern "C" void pico_audio_init(int sample_rate) {
    if (g_started && sample_rate == g_sample_rate) return;

    if (!g_started) {
#ifdef PICO_BOARD_TABLE
        if (AUDIO_PIN < 0) return;                 /* no audio wired */
        Serial.printf("[audio] PWMAudio on GPIO %d\n", AUDIO_PIN);
        g_audio_p = new PWMAudio(AUDIO_PIN, /*stereo=*/false);
#endif
        // 8 buffers × 128 words = 1024 samples. ~46ms of latency at 22kHz;
        // small enough that game→audio lag stays imperceptible, large enough
        // that we survive a few missed refills. Small buffers: the two the
        // DMA holds are only 256 samples, so the ring's level (the stretch,
        // pico_audio_push_s16) is known finely.
        g_audio.setBuffers(8, 128);
        Serial.printf("[audio] begin %d Hz\n", sample_rate);
        bool ok = g_audio.begin(sample_rate);
        Serial.printf("[audio] begin %s\n", ok ? "ok" : "FAILED");
        if (AMP_EN_PIN >= 0) {
            pinMode(AMP_EN_PIN, OUTPUT);
            digitalWrite(AMP_EN_PIN, LOW);    // amp OFF until game starts
        }
        g_started = true;
    } else {
        // Sample-rate switch mid-session (game change).
        g_audio.end();
        g_audio.begin(sample_rate);
    }
    g_sample_rate = sample_rate;
}

/* Flash writes turn interrupts off for tens of ms — longer than one DMA
 * buffer — and the ping-pong DMA's re-arm IRQ gets missed: the two channels
 * end up out of step for good (constant clicking, and the runaway DMA slows
 * everything down). So audio is stopped cleanly around every flash write and
 * restarted afterwards. Call with the other core idled (it pushes samples). */
extern "C" void pico_audio_suspend(void) {
    if (g_started) g_audio.end();
}
extern "C" void pico_audio_resume(void) {
    if (g_started) g_audio.begin(g_sample_rate);
}

extern "C" void pico_audio_set_sample_rate(int sample_rate) {
    pico_audio_init(sample_rate);
}

extern "C" size_t pico_audio_push(const void *data, size_t bytes) {
    if (!g_started || bytes < 2) return 0;
    // Galagino writes UNSIGNED 16-bit samples centered on 0x8000
    // (see galagino.cpp: snd_buffer[i] = 0x8000 + v). We convert
    // to signed by subtracting the DC bias, then apply volume.
    const uint16_t *samples = (const uint16_t *)data;
    size_t nsamp = bytes / 2;
    size_t written = 0;

    if (g_audio.availableForWrite() < 2) return 0;

    for (size_t i = 0; i < nsamp; i++) {
        int32_t centered = (int32_t)samples[i] - 32768;    // now -32768..32767
        int32_t s        = centered * g_gain / 10000;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        if (g_audio.availableForWrite() < 2) break;
        g_audio.write((int16_t)s);
        written++;
    }
    return written * 2;   // bytes actually written
}

/* The emulator is paced by this ring (it waits for room). When it runs
 * slower than real time - a heavy stretch, like Miner 2049er's level start -
 * the ring drains and the DMA plays its silence buffer: a jump to the middle
 * level and back, a click, several a second. So while the ring is under half
 * full the sound is stretched: now and then a sample is played twice, more
 * often the emptier the ring (up to one in two), so it sounds a little
 * slower, as the machine would, instead of breaking up; it settles at about
 * the emulator's shortfall and stops once the ring fills again. And a DC
 * blocker (~3.4 Hz) keeps the output centred on that silence level, so an
 * underrun that still happens is a gap rather than a pop. */
static const int RING_SAMPLES = 8 * 128;               /* setBuffers(8, 128), mono: a word each */
static int32_t  g_dc_x1 = 0, g_dc_y1 = 0;              /* DC blocker: last in, last out (x256) */
static uint32_t g_stretch_acc = 0;                     /* 16.16: due extra samples */

extern "C" size_t pico_audio_push_s16(const void *data, size_t bytes) {
    if (!g_started || bytes < 2) return 0;
    const int16_t *samples = (const int16_t *)data;
    size_t nsamp = bytes / 2, written = 0;
    int queued = RING_SAMPLES - g_audio.availableForWrite() / 4;
    uint32_t step = queued < RING_SAMPLES / 2
        ? (uint32_t)(RING_SAMPLES / 2 - (queued < 0 ? 0 : queued)) * (65536 / 2) / (RING_SAMPLES / 2)
        : 0;
    while (written < nsamp && g_audio.availableForWrite() >= 4) {
        int32_t x = samples[written];
        g_dc_y1 += ((x - g_dc_x1) << 8) - (g_dc_y1 >> 10);   /* y = x - x1 + (1 - 1/1024) y1 */
        g_dc_x1 = x;
        int32_t s = (g_dc_y1 >> 8) * g_gain / 10000;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        g_audio.write((int16_t)s);
        written++;
        g_stretch_acc += step;
        if (g_stretch_acc >= 65536) {
            g_stretch_acc -= 65536;
            if (g_audio.availableForWrite() >= 4) g_audio.write((int16_t)s);
        }
    }
    return written * 2;
}

extern "C" void pico_audio_set_volume_pct(int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    g_volume_pct = pct;
    g_gain       = pct * pct;
}

extern "C" int pico_audio_get_volume_pct(void) {
    return g_volume_pct;
}

// Legacy 0..16 API, mapped onto the % scale.
extern "C" void pico_audio_set_volume(uint8_t vol) {
    if (vol > 16) vol = 16;
    pico_audio_set_volume_pct(vol * 100 / 16);
}

extern "C" uint8_t pico_audio_get_volume(void) {
    return (uint8_t)((g_volume_pct * 16 + 50) / 100);
}

extern "C" void pico_audio_amp_on(void) {
    if (AMP_EN_PIN >= 0) digitalWrite(AMP_EN_PIN, HIGH);
}
extern "C" void pico_audio_amp_off(void) {
    if (AMP_EN_PIN >= 0) digitalWrite(AMP_EN_PIN, LOW);
}
