/* a8_zip.h - zipped games, unpacked into a flash area before they start. */
#ifndef A8_ZIP_H
#define A8_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* Unpack the first Atari file in the zip at `zip_path` (a drive path) into
 * the unzip area; its name goes to inner_name. Call before the game starts
 * (uses the emulator's idle buffers). */
bool a8_unzip(const char *zip_path, char *inner_name, size_t inner_cap);

/* The unpacked file in flash, or nullptr if nothing is unpacked. */
const uint8_t *a8_unzipped(size_t *size);

/* Paths the core opens for the unpacked file start with this. */
#define A8_UNZIP_PREFIX "/@UNZIP/"

#endif
