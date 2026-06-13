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

/* 1 if ANY kept frame binds a render target (SetRenderTarget) ⇒ the container has
 * cross-frame RT content (e.g. the pause backdrop's captured/blurred screen) and
 * MUST be reconstructed with orv3_replay_render_history, not the per-frame
 * orv3_replay_render (which shows RT-bound samples black/garbage). 0 ⇒ per-frame
 * render is exact and far cheaper. */
int orv3_replay_has_rt(const OrV3Replay *r);

/* draw/call counts for kept frame `idx` (parsed at open) — the viewer's per-frame
 * "state". -1 if idx out of range. */
int orv3_replay_draws(const OrV3Replay *r, int idx);
int orv3_replay_calls(const OrV3Replay *r, int idx);

/* Render kept frame `idx` and read back the backbuffer. Returns a pointer to an
 * internal tightly-packed w*h*4 BGRA buffer, valid until the next render/close, or
 * NULL on failure. Device + resources are reused (resident) — only this frame's
 * call section is issued. */
const uint8_t *orv3_replay_render(OrV3Replay *r, int idx);

/* Like orv3_replay_render but issue only the FIRST `max_draws` draw calls of the
 * frame (every state/clear/scene call is still issued, so the partial frame is
 * well-formed). max_draws < 0 ⇒ all draws (== orv3_replay_render). This is the
 * draw-isolation primitive: render the prefix [0,max_draws) to watch a frame build
 * up draw-by-draw, find overdraw, or (binary-searched) pick the draw under a pixel. */
const uint8_t *orv3_replay_render_upto(OrV3Replay *r, int idx, int max_draws);

/* Render frame `idx` issuing only the draws with index in [lo, hi) (hi < 0 ⇒ to the
 * end), every state/clear/scene call still issued. [0,K) is the prefix (== _upto K);
 * [J,J+1) is a SINGLE draw in ISOLATION over the clear with its correct device state
 * — the solo-draw view that shows exactly what one draw paints (e.g. a divergent draw
 * the other side omits). Reads back as orv3_replay_render. */
const uint8_t *orv3_replay_render_range(OrV3Replay *r, int idx, int lo, int hi);

/* Render kept frame `idx` with cross-frame RENDER-TARGET content correct: replay
 * frames [0..idx] cumulatively on the resident device (no readback/RT-reset between
 * them) so an RT filled at an earlier frame is still populated here. Needed for
 * captured-screen backdrops / blur transitions / post-processing (e.g. the pause
 * menu [0], whose RT is filled at the open ramp and sampled every resting frame) —
 * the per-frame `orv3_replay_render` shows those black. For an RT-free container
 * the result is identical to `orv3_replay_render`. O(idx) per call. */
const uint8_t *orv3_replay_render_history(OrV3Replay *r, int idx);

void orv3_replay_close(OrV3Replay *r);

#ifdef __cplusplus
}
#endif
#endif
