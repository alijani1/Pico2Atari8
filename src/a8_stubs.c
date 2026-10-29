/* a8_stubs.c - atari800 subsystems Pico2Atari8 doesn't build.
 *
 * H:/P:/R: host devices (devices.c - no host filesystem or printer), the
 * monitor, save states, the acid-test harness and the MZ POKEY engine (the classic pokeysnd.c
 * engine is used: a8_platform.c turns POKEYSND_enable_new_pokey off).
 */
#include "config.h"
#include <stdio.h>
#include "atari.h"
#include "devices.h"
#include "monitor.h"
#include "statesav.h"
#include "mzpokeysnd.h"
#include "acidtest.h"

int Devices_enable_h_patch = FALSE;
int Devices_enable_p_patch = FALSE;
int Devices_enable_r_patch = FALSE;
int Devices_enable_b_patch = FALSE;
char Devices_atari_h_dir[4][FILENAME_MAX];
int Devices_h_read_only = TRUE;
char Devices_h_exe_path[FILENAME_MAX];
char Devices_h_device_name = 'H';
char Devices_h_current_dir[4][FILENAME_MAX];
char Devices_print_command[256];

int Devices_Initialise(int *argc, char *argv[]) { (void)argc; (void)argv; return TRUE; }
void Devices_Exit(void) {}
int Devices_PatchOS(void) { return FALSE; }
void Devices_Frame(void) {}
void Devices_UpdatePatches(void) {}
int Devices_SetPrintCommand(const char *command) { (void)command; return FALSE; }

void MONITOR_Exit(void) {}

int StateSav_ReadAtariState(const char *filename, const char *mode)
{
	(void)filename; (void)mode;
	return FALSE;
}

int MZPOKEYSND_Init(ULONG freq17, int playback_freq, UBYTE num_pokeys, int flags, int quality)
{
	(void)freq17; (void)playback_freq; (void)num_pokeys; (void)flags; (void)quality;
	return 1;
}

int ACIDTEST_enabled = FALSE;
void ACIDTEST_Init(const char *filename) { (void)filename; }
void ACIDTEST_Frame(void) {}
