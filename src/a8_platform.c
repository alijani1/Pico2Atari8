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
#include <setjmp.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "atari.h"
#include "afile.h"
#include "util.h"
#include "akey.h"
#include "memory.h"
#include "cartridge.h"
#include "cpu.h"
#include "input.h"
#include "log.h"
#include "platform.h"
#include "pokeysnd.h"
#include "sound.h"
#include "ui.h"
#include "screen.h"
#include "colours.h"
#include "colours_ntsc.h"
#include "sio.h"
#include "a8_platform.h"
#include "pico_audio.h"

a8_input_t a8_input;

/* ---- lifecycle --------------------------------------------------------- */

/* The Atari screen (384 x 240 colour indices): static, so it can't fail
 * to allocate at run time (Screen_Initialise allocates one only if the
 * platform hasn't set Screen_atari). */
static ULONG screen_buf[Screen_WIDTH * Screen_HEIGHT / sizeof(ULONG)];

/* Free to use before the game starts (a8_zip: inflate dictionary). */
uint8_t *a8_screen_scratch(size_t *size)
{
	*size = sizeof(screen_buf);
	return (uint8_t *)screen_buf;
}

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

/* ---- crashes ----------------------------------------------------------- */

/* When the 6502 hits a crash (CIM) opcode, atari800 "enters the monitor":
 * Atari800_Exit(TRUE) -> PLATFORM_Exit(TRUE). There's no monitor here, and
 * returning "carry on" would run the same opcode forever, so (as
 * libatari800 does) we leave the frame with a longjmp and flag it; main
 * then restarts the game on another machine (a8_handle_crash). */
static jmp_buf frame_jmp;
static volatile int in_frame = FALSE;
volatile int a8_crashed = FALSE;

int PLATFORM_Exit(int run_monitor)
{
	if (run_monitor && in_frame) {
		a8_crashed = TRUE;
		longjmp(frame_jmp, 1);
	}
	return 0;          /* no monitor: exit */
}

/* Restart `path` on a 400/800 (48 KB): the OS-B ROM file if the drive has
 * one (SYSROM picks a real ROM over AltirraOS when it can), else AltirraOS. */
static void restart_as_800(const char *path)
{
	Atari800_SetMachineType(Atari800_MACHINE_800);
	MEMORY_ram_size = 48;
	Atari800_InitialiseMachine();
	int r = AFILE_OpenFile(path, TRUE, 1, TRUE);
	if ((r & 0xff) == AFILE_ROM && (r >> 8) > 0) {
		/* a raw cartridge: pick its type, as Atari800_Initialise does */
		CARTRIDGE_SetType(&CARTRIDGE_main, UI_SelectCartType(r >> 8));
		Atari800_Coldstart();
	}
}

int a8_handle_crash(const char *path)
{
	a8_crashed = FALSE;
	if (Atari800_machine_type == Atari800_MACHINE_XLXE) {
		Log_print("6502 crashed on the XL (PC %04X): restarting as an 800", CPU_regPC);
		restart_as_800(path);
		return TRUE;
	}
	Log_print("6502 crashed on the 800 too (PC %04X)", CPU_regPC);
	return FALSE;
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
	if (setjmp(frame_jmp)) {          /* the 6502 crashed: a8_crashed is set */
		in_frame = FALSE;
		return;
	}
	in_frame = TRUE;
	Atari800_Frame();
	in_frame = FALSE;
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

/* ---- palettes ------------------------------------------------------------
 * atari800's generated NTSC palette with different settings, plus a
 * PALETTE.ACT file (768 bytes R,G,B - what Altirra and other emulators
 * export) when the drive has one. green_pct trims the panel's green.
 *
 * The stock palette draws $1x "gold" with more green than red (104,123,8)
 * and its reds brownish. Rotating the hues (tint +0.10, colour delay 25)
 * was the best fit to the GTIA colour names over a sweep of tint / delay
 * (tools/host, PALETTE=): gold (160,150,0), red $46 (183,57,111) - all 15
 * hues turn together, so gold can't get warmer without red turning pinker. */
static const struct {
	int preset;
	double saturation, tint, delay;
	int green_pct;
} palettes[] = {
	{ COLOURS_PRESET_STANDARD, 0.0,  0.0,  26.8, 100 },   /* 1 atari800 standard */
	{ COLOURS_PRESET_VIBRANT,  0.26, 0.0,  26.8, 100 },   /* 2 vibrant */
	{ COLOURS_PRESET_VIBRANT,  0.6,  0.0,  26.8, 100 },   /* 3 vibrant, more saturation */
	{ COLOURS_PRESET_VIBRANT,  0.6,  0.10, 25.0, 100 },   /* 4 ... hues turned: gold, red */
	{ COLOURS_PRESET_VIBRANT,  0.6,  0.10, 25.0,  85 },   /* 5 ... and less green */
};
#define N_PALETTES ((int)(sizeof(palettes) / sizeof(palettes[0])))
#define A8_PALETTE_FILE "/PALETTE.ACT"
static int palette_file_ok = -1;     /* -1 not checked yet */

int a8_palette_count(void)
{
	if (palette_file_ok < 0) {
		FILE *f = fopen(A8_PALETTE_FILE, "rb");
		palette_file_ok = f != NULL;
		if (f) fclose(f);
	}
	return N_PALETTES + palette_file_ok;
}

/* n = 1 .. a8_palette_count(); returns the panel green gain (%). */
int a8_set_palette(int n)
{
	if (n < 1 || n > a8_palette_count()) n = 1;
	if (n > N_PALETTES) {                              /* the file */
		Util_strlcpy(COLOURS_NTSC_external.filename, A8_PALETTE_FILE,
		             sizeof(COLOURS_NTSC_external.filename));
		COLOURS_NTSC_external.adjust = FALSE;
		if (!COLOURS_EXTERNAL_Read(&COLOURS_NTSC_external)) n = 1;
		else { Colours_Update(); Log_print("palette %d: %s", n, A8_PALETTE_FILE); return 100; }
	}
	COLOURS_EXTERNAL_Remove(&COLOURS_NTSC_external);
	Colours_SetPreset((Colours_preset_t)palettes[n - 1].preset);
	Colours_setup->saturation = palettes[n - 1].saturation;
	Colours_setup->hue = palettes[n - 1].tint;
	Colours_setup->color_delay = palettes[n - 1].delay;
	Colours_Update();
	Log_print("palette %d", n);
	return palettes[n - 1].green_pct;
}

/* Disk drive 1 off (as if switched off: it no longer answers) or back on. */
int a8_toggle_drive1(void)
{
	static SIO_UnitStatus was = SIO_READ_ONLY;
	if (SIO_drive_status[0] == SIO_OFF) {
		SIO_drive_status[0] = was;
		Log_print("disk drive 1 on");
		return TRUE;
	}
	was = SIO_drive_status[0];
	SIO_drive_status[0] = SIO_OFF;
	Log_print("disk drive 1 off");
	return FALSE;
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
