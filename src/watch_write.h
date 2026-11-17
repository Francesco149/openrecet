/* watch_write.h — `--watch-write VA[:FROM:TO]`: name every port writer of one retail dword.
 *
 * x86 debug register DR0 (write, 4 bytes) on the main thread over the port's mirror of VA
 * (state_va_mirror_addr: the retail-layout blocks + work banks); each hit inside ticks
 * FROM..TO prints to stderr
 *   ww f<tick label> eip=<addr> <old>-><new>
 * Symbolize with addr2line -e <the staged port exe> <eip> (the exe keeps -g). Debug
 * registers are set through a vectored handler (works on Windows and wine).  Rhost's
 * `--watch-write` is the retail twin. */
#ifndef OPENRECET_WATCH_WRITE_H
#define OPENRECET_WATCH_WRITE_H

#include <stdint.h>

/* parse "VA[:FROM:TO]" (hex VA, decimal ticks); 0 ok, -1 bad */
int  watch_write_parse(const char *arg);
/* arm once the mirror exists (call at the frame boundary; idempotent) */
void watch_write_tick(uint32_t label);

#endif
