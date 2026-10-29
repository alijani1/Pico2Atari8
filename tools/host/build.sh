#!/bin/sh
# Build the PC harness from the firmware's own emulator sources.
#   wsl sh tools/host/build.sh  ->  tools/host/a8_harness   (Linux / WSL gcc)
# Unused functions are dropped (--gc-sections), as in the firmware build.
set -e
cd "$(dirname "$0")"
CORE=../../lib/atari800/src
F="-O1 -g -w -std=gnu99 -ffunction-sections -fdata-sections -fno-pie -I$CORE -I../../src $EXTRA_CFLAGS"
mkdir -p obj
rm -f obj/*.o
for f in harness.c ../../src/a8_platform.c ../../src/a8_stubs.c $CORE/*.c $CORE/roms/*.c; do
    gcc $F -c "$f" -o obj/$(basename "$f" .c).o
done
gcc obj/*.o -o a8_harness -lm -no-pie -Wl,--gc-sections \
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
echo built a8_harness
