/*
 * retail_image — bytes of the user's own retail exe, by retail VA.
 *
 * Some engine reads run past a table into the exe's constant sections (.data1,
 * .rsrc): FUN_00490e56's init pass reads item "kinds" 424 records past the item
 * table.  Those bytes are the user's file, read at run time and never stored in
 * the repo.  The exe: $OPENRECET_RETAIL_EXE, else recettear.exe, else
 * recettear.unpacked.exe in the working directory (SteamStub encrypts .text
 * only, so either file serves the data sections).
 */
#ifndef RETAIL_IMAGE_H
#define RETAIL_IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* The image byte at `va` (a section's file contents, 0 past its raw data):
 * 1, or 0 if no exe is readable / `va` lies in no section. */
int retail_image_byte(uint32_t va, uint8_t *out);

/* The image's little-endian dword at `va`: 1, or 0 as retail_image_byte. */
int retail_image_u32(uint32_t va, uint32_t *out);

/* The NUL-terminated string at `va` into buf (at most cap−1 bytes, always
 * terminated): 1, or 0 (buf = "") if no exe is readable / `va` is unmapped. */
int retail_image_cstr(uint32_t va, char *buf, size_t cap);

#endif
