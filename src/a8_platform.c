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
#include <strings.h>
#include "atari.h"
#include "afile.h"
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
	if (Atari800_machine_type == Atari800_MACHINE_5200) {
		Log_print("6502 crashed on the 5200 (PC %04X)", CPU_regPC);
		return FALSE;
	}
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

/* 5200 analog sticks (input.c, A8_POT_INPUT): the Joy-Con's position, or
 * the centre - then input.c falls back to the stick's directions. Paddles
 * on the 400/800/XL aren't connected (228, as upstream without a pot). */
int Atari_POT(int num)
{
	if (Atari800_machine_type != Atari800_MACHINE_5200)
		return 228;
	if (num < 0 || num > 7 || a8_input.pot[num] == A8_POT_NONE)
		return INPUT_joy_5200_center;
	return a8_input.pot[num];
}

int a8_is_5200(void)
{
	return Atari800_machine_type == Atari800_MACHINE_5200;
}

/* An 800 cartridge ends in its run address, $00 at $BFFC ("a cartridge is
 * here") and an option byte of bits 0, 2 and 7 at $BFFD, then its init
 * address. A 5200 one ends in the last character of its title, the year's
 * last digit as a screen code ($FF: no title screen) at $BFFD, and its
 * start address, somewhere in $4000-$BFFF. Raw 5200 carts are 4-40 KB. */
int a8_raw_cart_is_5200(const uint8_t *tail, long size)
{
	if (size > 40 * 1024)
		return 0;
	if (tail[2] == 0x00 && (tail[3] & ~0x85) == 0)
		return 0;
	if (tail[5] >= 0x40 && tail[5] < 0xc0)
		return 1;
	return -1;
}

int a8_file_is_5200(const char *path, int unknown_5200)
{
	const char *dot = strrchr(path, '.');
	if (dot == NULL)
		return FALSE;
	if (strcasecmp(dot, ".a52") == 0)
		return TRUE;
	if (strcasecmp(dot, ".bin") != 0 && strcasecmp(dot, ".rom") != 0)
		return FALSE;
	{
		FILE *f = fopen(path, "rb");
		uint8_t tail[6];
		long size;
		int r;
		if (f == NULL)
			return FALSE;
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		r = size >= 4096 && fseek(f, size - 6, SEEK_SET) == 0 && fread(tail, 1, 6, f) == 6
		    ? a8_raw_cart_is_5200(tail, size) : 0;
		fclose(f);
		return r < 0 ? unknown_5200 : r;
	}
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
/* Samples in the next frame: rate / fps, in fixed point (fps x 1000). */
static unsigned frame_samples(void)
{
	unsigned fps1000 = Atari800_tv_mode == Atari800_TV_PAL ? 49860 : 59923;
	sound_acc += (unsigned)A8_SAMPLE_RATE * 1000u;
	unsigned n = sound_acc / fps1000;
	sound_acc -= n * fps1000;
	return n > A8_MAX_FRAME_SAMPLES ? A8_MAX_FRAME_SAMPLES : n;
}

/* Send n samples of sound_buf, waiting for room: this is what holds the
 * emulator to real time. */
static void push_samples(unsigned n)
{
	const uint8_t *p = (const uint8_t *)sound_buf;
	size_t left = n * 2;
	while (left) {
		size_t done = pico_audio_push_s16(p, left);
		p += done; left -= done;
		if (left) a8_idle();
	}
}

void Sound_Update(void)
{
	if (!Sound_enabled || sound_paused) return;
	unsigned n = frame_samples();
	POKEYSND_Process(sound_buf, (int)n);
	push_samples(n);
}

/* Paused: a frame of silence, at the same pace (an empty PWM ring clicks). */
void a8_silent_frame(void)
{
	unsigned n = frame_samples();
	memset(sound_buf, 0, n * sizeof(sound_buf[0]));
	push_samples(n);
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

/* A raw 5200 cartridge. Sizes say it all but for 16 KB, which comes as one
 * chip (NS: $8000-$BFFF) or, more often, two (EE: the first 8 KB at $4000
 * and $6000, the second at $A000). The top 8 KB (the vectors) is at $A000
 * either way, so look at where the first 8 KB's own JMPs and JSRs go: its
 * code calls itself at $4000-$7FFF on a two-chip board, at $8000-$9FFF on a
 * one-chip one. */
static int guess_5200_cart_type(int kb, const UBYTE *image)
{
	switch (kb) {
	case 4:   return CARTRIDGE_5200_4;
	case 8:   return CARTRIDGE_5200_8;
	case 32:  return CARTRIDGE_5200_32;
	case 40:  return CARTRIDGE_5200_40;
	case 64:  return CARTRIDGE_5200_SUPER_64;
	case 128: return CARTRIDGE_5200_SUPER_128;
	case 256: return CARTRIDGE_5200_SUPER_256;
	case 512: return CARTRIDGE_5200_SUPER_512;
	case 16: {
		int low = 0, high = 0, i;
		if (image == NULL)
			return CARTRIDGE_5200_EE_16;
		for (i = 0; i + 2 < 0x2000; i++) {
			if (image[i] == 0x4c || image[i] == 0x20) {          /* JMP / JSR abs */
				UBYTE hi = image[i + 2];
				if (hi >= 0x40 && hi < 0x80) low++;
				else if (hi >= 0x80 && hi < 0xa0) high++;
			}
		}
		Log_print("16 KB 5200 cartridge: %d calls to $4000-$7FFF, %d to $8000-$9FFF", low, high);
		return high > low ? CARTRIDGE_5200_NS_16 : CARTRIDGE_5200_EE_16;
	}
	default:  return CARTRIDGE_NONE;
	}
}

/* A cartridge whose type can't be told from its size: a .car header names
 * it; a raw .rom of an ambiguous size is the commonest type of that size. */
int UI_SelectCartType(int k)
{
	int t = Atari800_machine_type == Atari800_MACHINE_5200
	        ? guess_5200_cart_type(k, CARTRIDGE_main.image) : a8_guess_cart_type(k);
	Log_print("raw cartridge, %d KB: type %d", k, t);
	return t;
}

int UI_SelectCartTypeBetween(int *types)
{
	return types && types[0] ? types[0] : CARTRIDGE_NONE;
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
