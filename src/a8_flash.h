/* a8_flash.h - our own flash areas: read through ATU window 1, written a
 * 4 KB sector at a time (see a8_flash.cpp). Offsets are physical. */
#ifndef A8_FLASH_H
#define A8_FLASH_H

#include <stdint.h>

#ifndef A8_UNZIP_PHYS
#define A8_UNZIP_PHYS    0x240000u   /* Quad 1.1: reserved below CX-W's LittleFS (platformio.ini) */
#endif
#ifndef A8_UNZIP_SIZE
#define A8_UNZIP_SIZE    0x20000u    /* 128 KB */
#endif
#ifndef A8_SETTINGS_PHYS
#define A8_SETTINGS_PHYS 0x2C0000u   /* Quad 1.1: 4 KB after the Atari 8-bit slot (docs/FLASH_LAYOUT.md) */
#endif

void a8_flash_init(void);                                  /* map window 1 (at boot) */
const uint8_t *a8_flash_read(uint32_t phys);               /* readable address */
void a8_flash_write_sector(uint32_t phys, const uint8_t *data);   /* 4 KB, erased + programmed */

#endif
