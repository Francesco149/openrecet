/* Port-side call tracer — per-frame JSONL of which port functions ran.
 *
 * Counterpart of the Frida `call_trace.jsonl` emitter in
 * tools/frida/openrecet-agent.js.  Together they let
 * tools/call_trace_diff.py walk both sides of a HOUSE/title-screen
 * frame and report:
 *
 *   • which engine functions retail called that we never reached
 *     (= unported functions our scene-state code skips today)
 *   • which we called that retail didn't (= structural divergence)
 *   • which both sides called (= overlap, candidates for I/O diff)
 *
 * Schema (matches the Frida agent's call_trace event, modulo ts):
 *
 *   {"va": <ghidra_va>, "ret_va": <module_relative>, "frame": <N>}
 *
 *   • va        — the engine Ghidra-VA the port function corresponds
 *                 to.  Annotation-driven (CALL_TRACE_ENTER(0x4xxxxx))
 *                 rather than auto-discovered: the manual probe is
 *                 explicit, lossless, and doubles as port↔engine
 *                 documentation.  False positives impossible.
 *   • ret_va    — caller's PC, module-relative.  Add IMAGE_BASE
 *                 (0x00400000) to map to a Ghidra VA.  Identical
 *                 convention to the Frida agent.
 *   • frame     — sim-frame index from g_tick.frame_count.  Same
 *                 numbering as d3d_trace + scenario captures.
 *
 * Wiring (in src/main.c, mirrors d3d_trace):
 *
 *   1. parse_cmdline absorbs --call-trace <path> + optional
 *      --call-trace-frames i,j,k.  call_trace_init_from_cli stashes.
 *   2. At the top of sim/render dispatch each frame, call
 *      call_trace_begin_frame(N) with the upcoming sim frame.
 *   3. After the frame, call_trace_end_frame() fflushes the file.
 *   4. On shutdown, call_trace_shutdown() closes it.
 *
 * Cost when not enabled: every CALL_TRACE_ENTER is a single null-check
 * on a static FILE pointer.  Output saturates fast when traced
 * functions are hot, so pair with --call-trace-frames for non-title
 * scenarios.  Probe annotations live alongside the port functions:
 * each one declares the Ghidra VA it implements.  Adding new probes
 * is one line per ported function.
 */

#ifndef OPENRECET_CALL_TRACE_H
#define OPENRECET_CALL_TRACE_H

#include <stddef.h>
#include <stdint.h>

void call_trace_init_from_cli(const char *path,
                              const unsigned *frames, size_t n_frames);
void call_trace_begin_frame(unsigned frame);
void call_trace_end_frame(void);
void call_trace_shutdown(void);

/* Emit one JSONL row.  ret_addr is captured by the probe macro so the
 * value is the caller's PC, not the macro expansion's.  `stub` carries
 * forward into the emitted JSON as `"stub": true` when nonzero — used
 * by tools/call_trace_diff.py to distinguish "matched-count-AND-fully-
 * ported" rows from "matched-count-but-port-body-is-a-stub" rows.
 * Without the marker, pure call-count parity can hide a stubbed body
 * (= the engine fires the function, our port also fires SOMETHING at
 * the same VA, but our SOMETHING returns immediately or only emits the
 * preamble — see docs feedback_mark_stubbed_ports memory for the
 * motivating incident). */
void call_trace_enter(uint32_t ghidra_va, const void *ret_addr, int stub);

/* Probe macro for a FULLY PORTED function — body matches the engine's
 * behavioural contract end-to-end (subject to documented divergences).
 * Compiles to a single null-check + fprintf gate when --call-trace is
 * off.  `ghidra_va` is the engine VA the function corresponds to. */
#define CALL_TRACE_ENTER(ghidra_va) \
    call_trace_enter((uint32_t)(ghidra_va), __builtin_return_address(0), 0)

/* Probe macro for a PARTIALLY PORTED or STUB function.  Use when:
 *   - Function body is a stub that returns immediately (or only fires
 *     a state-write preamble like render_quad_state_setup) and the
 *     real work is deferred to a future chip.
 *   - Function body is partially ported — wraps real work AND
 *     unimplemented sub-stubs, AND the unimplemented portion is
 *     load-bearing for the current scene's behaviour.
 * Do NOT use when:
 *   - Body is fully ported but happens to hit a no-op branch in the
 *     current scenario (e.g., dungeon_clear_banner with counter==0 —
 *     the body is complete, the gate is just BSS-zero today).
 * The marker propagates into JSONL as `"stub": true`; call_trace_diff
 * surfaces those rows as `≈` (count-parity but body-not-complete)
 * distinct from `=` (full parity) and `≠` (count mismatch). */
#define CALL_TRACE_ENTER_STUB(ghidra_va) \
    call_trace_enter((uint32_t)(ghidra_va), __builtin_return_address(0), 1)

#endif /* OPENRECET_CALL_TRACE_H */
