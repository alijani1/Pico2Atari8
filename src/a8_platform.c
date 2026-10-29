/* a8_platform.c - the atari800 platform layer (PLATFORM_* in platform.h).
 *
 * Modelled on upstream's libatari800 target: the core asks for the keyboard,
 * the joysticks and the console keys once a frame and hands over each
 * finished screen. Here the answers come from a8_input (filled from the
 * device's controls by main.cpp), the screen goes to a8_display_frame()
 * (blitted to the TFT on core 1) and the sound to the PWM ring.
 *
 * Upstream's generic sound.c (a floating-point sync buffer for desktop
 * audio APIs) is replaced by the few Sound_* functions below: POKEY renders
 * one frame of samples straight into the PWM ring, waiting for room - which
 * also paces the emulator to real time, so PLATFORM_Sleep does nothing.
 */
#include "config.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "atari.h"
#include "akey.h"
#include "cartridge.h"
#include "input.h"
#include "log.h"
#include "platform.h"
#include "pokeysnd.h"
#include "sound.h"
#include "ui.h"
#include "screen.h"
#include "a8_platform.h"
#include "pico_audio.h"

a8_input_t a8_input;

/* ---- lifecycle --------------------------------------------------------- */

/* The Atari screen (384 x 240 colour indices): static, so it can't fail
 * to allocate at run time (Screen_Initialise allocates one only if the
 * platform hasn't set Screen_atari). */
static ULONG screen_buf[Screen_WIDTH * Screen_HEIGHT / sizeof(ULONG)];

int PLATFORM_Initialise(int *argc, char *argv[])
{
	a8_stage = "PLATFORM_Initialise";
	Screen_atari = screen_buf;
	return Sound_Initialise(argc, argv);   /* as libatari800 does */
}

/* ---- log (replaces upstream log.c: no 8 KB stack buffer, no printf) ---- */

void Log_print(const char *format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	a8_log_write(line);
}

void Log_flushlog(void) {}

int PLATFORM_Exit(int run_monitor)
{
	(void)run_monitor;
	return 0;          /* no monitor: always exit */
}

double PLATFORM_Time(void)
{
	return (double)a8_time_us() / 1e6;
}

void PLATFORM_Sleep(double s)
{
	(void)s;           /* paced by the audio ring (Sound_Update) */
}

/* ---- input ------------------------------------------------------------- */

int PLATFORM_Keyboard(void)
{
	INPUT_key_consol = INPUT_CONSOL_NONE;
	if (a8_input.start)  INPUT_key_consol &= ~INPUT_CONSOL_START;
	if (a8_input.select) INPUT_key_consol &= ~INPUT_CONSOL_SELECT;
	if (a8_input.option) INPUT_key_consol &= ~INPUT_CONSOL_OPTION;
	INPUT_key_shift = a8_input.shift;
	return a8_input.key >= 0 ? a8_input.key : AKEY_NONE;
}

/* Sticks: bit set = direction held (0x01 up, 0x02 down, 0x04 left,
 * 0x08 right); the hardware reads active low. PORTA = sticks 0/1, PORTB
 * = sticks 2/3 (the 400/800 only - XL/XE use PORTB for memory banking). */
int PLATFORM_PORT(int num)
{
	if (num == 0)
		return ((a8_input.stick[0] & 0x0f) | ((a8_input.stick[1] & 0x0f) << 4)) ^ 0xff;
	if (num == 1)
		return ((a8_input.stick[2] & 0x0f) | ((a8_input.stick[3] & 0x0f) << 4)) ^ 0xff;
	return 0xff;
}

int PLATFORM_TRIG(int num)
{
	if (num < 0 || num > 3) return 1;
	return a8_input.trig[num] ? 0 : 1;
}

/* ---- one frame ---------------------------------------------------------- */

/* What upstream's ports do in their main loops: the core reads neither the
 * keyboard nor shows the screen by itself. */
void a8_frame(void)
{
	INPUT_key_code = PLATFORM_Keyboard();
	Atari800_Frame();
	if (Atari800_display_screen)
		PLATFORM_DisplayScreen();
}

/* ---- display ----------------------------------------------------------- */

void PLATFORM_DisplayScreen(void)
{
	a8_stage = "running";
	a8_display_frame();
}

/* ---- sound ------------------------------------------------------------- */

#define A8_MAX_FRAME_SAMPLES 512

int Sound_enabled = TRUE;
static int sound_paused = FALSE;
static int16_t sound_buf[A8_MAX_FRAME_SAMPLES];
static unsigned sound_acc;           /* sample-rate x 1000 owed to the next frame */

/* The PWM output itself is started by main.cpp before the emulator (as
 * Pico2Arcade does, on the normal stack); here there's nothing left to do. */
int Sound_Initialise(int *argc, char *argv[])
{
	(void)argc; (void)argv;
	return TRUE;
}

/* Called by Atari800_Initialise once the machine is set up. */
int Sound_Setup(void)
{
	a8_stage = "Sound_Setup";
	POKEYSND_enable_new_pokey = FALSE;   /* the classic engine: MZ is too heavy */
	POKEYSND_Init(POKEYSND_FREQ_17_EXACT, A8_SAMPLE_RATE, 1, POKEYSND_BIT16);
	Sound_enabled = TRUE;
	return TRUE;
}

void Sound_Exit(void) {}
void Sound_Pause(void) { sound_paused = TRUE; }
void Sound_Continue(void) { sound_paused = FALSE; }
double Sound_AdjustSpeed(void) { return 1.0; }
int Sound_ReadConfig(char *option, char *ptr) { (void)option; (void)ptr; return FALSE; }
void Sound_WriteConfig(FILE *fp) { (void)fp; }

/* One frame of POKEY output. NTSC is 59.92 frames/s, PAL 49.86: the
 * fraction is carried over so the average rate is exact. */
void Sound_Update(void)
{
	if (!Sound_enabled || sound_paused) return;
	/* samples this frame = rate / fps, in fixed point (fps x 1000) */
	unsigned fps1000 = Atari800_tv_mode == Atari800_TV_PAL ? 49860 : 59923;
	sound_acc += (unsigned)A8_SAMPLE_RATE * 1000u;
	unsigned n = sound_acc / fps1000;
	sound_acc -= n * fps1000;
	if (n > A8_MAX_FRAME_SAMPLES) n = A8_MAX_FRAME_SAMPLES;
	POKEYSND_Process(sound_buf, (int)n);
	/* Wait for room: this is what holds the emulator to real time. */
	const uint8_t *p = (const uint8_t *)sound_buf;
	size_t left = n * 2;
	while (left) {
		size_t done = pico_audio_push_s16(p, left);
		p += done; left -= done;
		if (left) a8_idle();
	}
}

/* ---- UI stubs (upstream's text UI isn't built) ------------------------- */

int UI_is_active = FALSE;
int UI_alt_function = -1;
int UI_current_function = -1;
int UI_crash_code;
UWORD UI_crash_address;
UWORD UI_crash_afterCIM;
char UI_atari_files_dir[UI_MAX_DIRECTORIES][FILENAME_MAX];
char UI_saved_files_dir[UI_MAX_DIRECTORIES][FILENAME_MAX];
int UI_n_atari_files_dir;
int UI_n_saved_files_dir;
int UI_show_hidden_files = FALSE;

int UI_Initialise(int *argc, char *argv[])
{
	(void)argc; (void)argv;
	a8_stage = "chips (ANTIC/GTIA/PIA/POKEY), then the file";
	return TRUE;
}
void UI_Run(void) {}

/* A cartridge whose type can't be told from its size: a .car header names
 * it; a raw .rom of an ambiguous size is the commonest type of that size. */
int UI_SelectCartType(int k)
{
	int t = a8_guess_cart_type(k);
	Log_print("raw cartridge, %d KB: type %d", k, t);
	return t;
}

int UI_SelectCartTypeBetween(int *types)
{
	return types && types[0] ? types[0] : CARTRIDGE_NONE;
}

/* Raw cartridge images (.rom/.bin) carry no type: pick the commonest one
 * for the size (KB). The .car format names the type and never gets here. */
int a8_guess_cart_type(int kb)
{
	switch (kb) {
	case 2:    return CARTRIDGE_STD_2;
	case 4:    return CARTRIDGE_STD_4;
	case 8:    return CARTRIDGE_STD_8;
	case 16:   return CARTRIDGE_STD_16;
	case 32:   return CARTRIDGE_XEGS_32;
	case 40:   return CARTRIDGE_BBSB_40;
	case 64:   return CARTRIDGE_XEGS_07_64;
	case 128:  return CARTRIDGE_XEGS_128;
	case 256:  return CARTRIDGE_XEGS_256;
	case 512:  return CARTRIDGE_XEGS_512;
	case 1024: return CARTRIDGE_XEGS_1024;
	default:   return CARTRIDGE_NONE;
	}
}
