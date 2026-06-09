/*
 * memsnap.h — dump the port's own writable PE sections to disk (phase census).
 *
 * The phase-state census (docs/audits/2026-06-09-methodology-audit.md T3)
 * diffs two same-side runs with deliberately different pre-anchor timing to
 * find EVERY load-timing-dependent global. The dump is the raw bytes of each
 * writable section (.data/.bss — VirtualSize, so zero-fill arrays included)
 * plus a JSON index mapping section → link-time VA range, so the differ can
 * attribute differing offsets to symbols (`nm` on the exe).
 *
 * Driven by the `{"memsnap":N}` segtrace op (fires once, pre-sim, at the
 * deterministic frame base+N — identical across runs). Win32-only; the host
 * test build gets a stub (the op parsing is host-tested in input_segtrace).
 */
#ifndef OPENRECET_MEMSNAP_H
#define OPENRECET_MEMSNAP_H

#include <stdint.h>

/* Write memsnap_<frame>_<section>.bin per writable section + a
 * memsnap_<frame>.json index into `dir` (a Windows path, same as the frame
 * capture dir). Returns the number of sections dumped (0 on failure). */
int memsnap_dump(const char *dir, uint32_t frame);

#endif /* OPENRECET_MEMSNAP_H */
