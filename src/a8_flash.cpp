/* a8_flash.cpp - writing our own flash areas (unzip area, settings sector)
 * while running from flash, and reading them back.
 *
 * Reading: the areas are outside the chain-boot window the host maps at
 * 0x10000000, so QMI address-translation window 1 (0x10400000) is pointed
 * at physical flash 0 and an area is read at 0x10400000 + its offset.
 * (The window is 4 MB: it starts at the 4 MB block holding the areas —
 * physical 0 on the Quad / Duo 5200, 0x800000 on the 16 MB Duo Ultimate,
 * whose areas sit at 0x800000 / 0x8E0000.)
 *
 * Writing: on the RP2350 the SDK's flash functions re-enter XIP in the boot
 * ROM's slow default mode (they save/restore only the PSRAM chip select,
 * m[1]): left like that, everything run from flash afterwards - both cores
 * - ran many times slower (games at 1-5 fps). So the flash's read settings
 * (QMI m[0] timing/rcmd/rfmt) are put back right after, in RAM with
 * interrupts off and core 1 parked (it runs from flash).
 */
#include <Arduino.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/structs/qmi.h>
#include "a8_flash.h"

static const uint32_t WINDOW1 = 0x10400000u;
static const uint32_t WINDOW1_BASE = A8_UNZIP_PHYS & ~0x3FFFFFu;   /* physical start of window 1 */
static_assert((A8_SETTINGS_PHYS & ~0x3FFFFFu) == WINDOW1_BASE,
              "unzip area and settings sector must be in the same 4 MB of flash");

void a8_flash_init(void) {
    /* 4 MB from WINDOW1_BASE (BASE field in 4 KB units) */
    qmi_hw->atrans[1] = (0x400u << QMI_ATRANS1_SIZE_LSB) | (WINDOW1_BASE >> 12);
}

const uint8_t *a8_flash_read(uint32_t phys) {
    return (const uint8_t *)(WINDOW1 + (phys - WINDOW1_BASE));
}

static void __no_inline_not_in_flash_func(program_sector)(uint32_t ofs, const uint8_t *data) {
    uint32_t timing = qmi_hw->m[0].timing, rcmd = qmi_hw->m[0].rcmd, rfmt = qmi_hw->m[0].rfmt;
    flash_range_erase(ofs, FLASH_SECTOR_SIZE);
    flash_range_program(ofs, data, FLASH_SECTOR_SIZE);
    qmi_hw->m[0].timing = timing;
    qmi_hw->m[0].rcmd = rcmd;
    qmi_hw->m[0].rfmt = rfmt;
    __compiler_memory_barrier();
}

void a8_flash_write_sector(uint32_t phys, const uint8_t *data) {
    rp2040.idleOtherCore();
    uint32_t irq = save_and_disable_interrupts();
    program_sector(phys, data);
    restore_interrupts(irq);
    rp2040.resumeOtherCore();
}
