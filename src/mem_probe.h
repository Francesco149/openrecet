/*
 * mem_probe.h — the port's address-space / heap probe (diagnostic, stderr).
 *
 * The 32-bit port has 2 GB of address space; dungeon3-floors (199,626 frames)
 * ran it out at ~f145000, after which loads failed and a loader thread could
 * not even be created (CreateThread error 8) — the run went nondeterministic.
 * mem_probe() prints "mem: fN WHAT …MB used heap …" and the CRT-heap block
 * sizes whose counts grew most since the previous probe; rhost fetches the
 * port's stderr as port.err.  No-op off Win32.
 */
#ifndef OPENRECET_MEM_PROBE_H
#define OPENRECET_MEM_PROBE_H

void mem_probe(const char *what);

#endif
