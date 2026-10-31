/* config.h - atari800 build configuration for Pico2Atari8 (RP2350, no OS).
 *
 * Upstream generates this with autoconf; this hand-written one keeps the
 * emulation core (CPU, ANTIC, GTIA, POKEY, PIA, SIO, cartridges, XEX
 * loader) and leaves out everything a microcontroller has no use for:
 * the monitor, the text UI, H:/R: host devices, recording, PBI devices.
 * The platform layer (PLATFORM_*) is src/a8_platform.c.
 */
#ifndef CONFIG_H_
#define CONFIG_H_

#define PACKAGE_VERSION "pico2atari8"

/* --- features ---------------------------------------------------------- */
#define SOUND 1                     /* POKEY sound (pokeysnd.c) */
#define POKEYSND_SIGNED_SAMPLES 1   /* 16-bit signed output for the PWM ring */
#define SUPPORTS_PLATFORM_TIME 1    /* PLATFORM_Time / PLATFORM_Sleep: a8_platform.c */
#define SUPPORTS_PLATFORM_SLEEP 1
#define NO_YPOS_BREAK_FLICKER 1
#define PAGED_ATTRIB 1              /* 256 page handlers instead of a 64 KB attribute table */
#define EMUOS_ALTIRRA 1             /* built-in AltirraOS / Altirra BASIC / 5200 OS (roms/) */
#define A8_POT_INPUT 1              /* 5200 analog sticks: Atari_POT() in a8_platform.c (input.c patch) */
#define A8_SKIP_HIGH_TONES 1        /* pure tones above the sample rate held at their average; no catch-up burst after silence (pokeysnd.c patch) */

/* --- C library ---------------------------------------------------------- */
#define HAVE_STDINT_H 1
#define HAVE_ERRNO_H 1
#define HAVE_STRNCPY 1
#define HAVE_STRCASECMP 1
#define HAVE_STRSTR 1
#define HAVE_STRERROR 1
#define HAVE_VSNPRINTF 1
#define HAVE_REWIND 1
#define HAVE_FFLUSH 1
#define HAVE_ROUND 1
#define HAVE_TIME_H 1
#define HAVE_STRINGS_H 1

#endif /* CONFIG_H_ */
