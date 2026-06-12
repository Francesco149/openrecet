/* OpenRecet Trace Studio v3 — resident replay core (shared: replay.exe CLI + the
 * native viewer).
 *
 * The replayer logic, factored so the DEVICE + ALL RESOURCES are created ONCE and
 * any kept frame renders on demand (issue that frame's call section + read back) —
 * the resident model the native viewer needs to scrub at replay speed. The old
 * replay.c re-opened the container + recreated every resource per invocation
 * (~620 ms cold, dominated by the 26 MB resource set); open-once makes per-frame
 * render the tail of that (issue calls + readback only).
 *
 * Plain C, real d3d8 (CINTERFACE/COBJMACROS) — compiled with mingw gcc, linked into
 * the C++ viewer (extern "C"). The whole container is read into memory at open so
 * frames index by byte range and resource data is referenced zero-copy.
 */
#ifndef ORV3_REPLAY_CORE_H
#define ORV3_REPLAY_CORE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrV3Replay OrV3Replay;

/* Open a container: read it in, create the d3d8 device with the captured params,
 * create every (dedup'd) resource, and index each kept frame's byte range. Returns
 * NULL on failure (err, if non-NULL, gets a short reason). */
OrV3Replay *orv3_replay_open(const char *cap_path, char *err, int errlen);

int orv3_replay_count (const OrV3Replay *r);   /* kept-frame count */
int orv3_replay_width (const OrV3Replay *r);
int orv3_replay_height(const OrV3Replay *r);

/* draw/call counts for kept frame `idx` (parsed at open) — the viewer's per-frame
 * "state". -1 if idx out of range. */
int orv3_replay_draws(const OrV3Replay *r, int idx);
int orv3_replay_calls(const OrV3Replay *r, int idx);

/* Render kept frame `idx` and read back the backbuffer. Returns a pointer to an
 * internal tightly-packed w*h*4 BGRA buffer, valid until the next render/close, or
 * NULL on failure. Device + resources are reused (resident) — only this frame's
 * call section is issued. */
const uint8_t *orv3_replay_render(OrV3Replay *r, int idx);

void orv3_replay_close(OrV3Replay *r);

#ifdef __cplusplus
}
#endif
#endif
