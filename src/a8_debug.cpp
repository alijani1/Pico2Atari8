/* a8_debug.cpp - show what atari800 says and does when it gives up.
 *
 * Wrapped at link time (platformio.ini):
 *   printf / puts  the core prints a few things straight to stdout (e.g.
 *                  "Fatal error: out of memory"), which goes nowhere on the
 *                  Pico - send them to the serial monitor instead.
 *   exit           the core exits on fatal errors; on the Pico that just
 *                  stops. Report it (with the stage and the free heap) and
 *                  keep reporting, so it can't look like a silent freeze.
 *
 * And a HardFault handler: a crash on core 0 records where it happened
 * (the faulting PC and LR) and parks that core; core 1's heartbeat prints
 * it. Look the address up with
 *   arm-none-eabi-addr2line -f -e .pio/build/quad/firmware.elf <pc>
 */
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "a8_platform.h"
#include "hardware/exception.h"

extern "C" {
int __wrap_printf(const char *fmt, ...);
int __wrap_puts(const char *s);
void __wrap_exit(int code);
}

volatile uint32_t a8_fault_pc = 0, a8_fault_lr = 0, a8_fault_cfsr = 0;

extern "C" void __attribute__((used)) a8_fault_c(uint32_t *frame) {
    a8_fault_lr = frame[5];
    a8_fault_pc = frame[6];
    a8_fault_cfsr = *(volatile uint32_t *)0xE000ED28;      /* SCB->CFSR */
    for (;;) tight_loop_contents();
}

/* The stacked exception frame is on MSP or PSP (bit 2 of EXC_RETURN). */
extern "C" void __attribute__((naked)) a8_fault_isr(void) {
    asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b a8_fault_c\n");
}

void a8_debug_init(void) {
    exception_set_exclusive_handler(HARDFAULT_EXCEPTION, a8_fault_isr);
}

extern "C" int __wrap_printf(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    Serial.printf("[stdout] %s", line);
    return n;
}

extern "C" int __wrap_puts(const char *s) {
    Serial.printf("[stdout] %s\n", s);
    return 1;
}

extern "C" void __wrap_exit(int code) {
    for (;;) {
        Serial.printf("[a8] exit(%d) during %s - heap free %d bytes\n",
                      code, (const char *)a8_stage, rp2040.getFreeHeap());
        delay(2000);
    }
}
