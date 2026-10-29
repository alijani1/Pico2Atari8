/* a8_debug.cpp - show what atari800 says and does when it gives up.
 *
 * Wrapped at link time (platformio.ini):
 *   printf / puts  the core prints a few things straight to stdout (e.g.
 *                  "Fatal error: out of memory"), which goes nowhere on the
 *                  Pico - send them to the serial monitor instead.
 *   exit           the core exits on fatal errors; on the Pico that just
 *                  stops. Report it (with the stage and the free heap) and
 *                  keep reporting, so it can't look like a silent freeze.
 */
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "a8_platform.h"

extern "C" {
int __wrap_printf(const char *fmt, ...);
int __wrap_puts(const char *s);
void __wrap_exit(int code);
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
