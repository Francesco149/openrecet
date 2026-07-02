# Decompile & probe gotchas — the trap checklist

Every entry below cost a real debugging session at least once. Skim this before
porting a chip and again when a "weird" divergence appears — most "engine
mysteries" in this project's history were one of these. (Consolidated 2026-06-09
from auto-memory per the audit's R7; the memories are archived.)

## Reading Ghidra output (the decompile is a lossy VIEW; asm is the spec)

1. **"Argless" trig isn't argless.** Ghidra drops the FPU QWORD load feeding
   `FUN_00503a44`/`00503994` (sin/cos thunks). Check the asm
   (`objdump`/`r2`) for the `fld`/`fmul` feeding the call before concluding
   anything about the argument.
2. **FPU-stack args get dropped wholesale — at the CALL SITE too, not just the
   callee.** A function that "takes no float" in the decompile may consume
   st(0)/st(1); equally, a CALL printed as `foo()` with no args may be passing
   floats Ghidra dropped. Landed ports with Ghidra-dropped FPU args are queued for
   Frida validation (memory `pending-human-checks`). *Worked example, `d4899bc`:*
   `FUN_0046b00a`'s tail prints `FUN_00469b3a()` (the display-menu description
   panel) with no args, so the port drew it at the callee's default `param_1=0`
   (fixed x=0). The dropped `param_1` is actually the menu slide x-offset
   `640−(b98c<<7)` — the panel SLIDES with the menu. It matched only because at
   the settled state the offset is 0; the bug showed as a 185k-px divergence
   across every slide frame. Lesson: when a fixed-position UI element matches at
   rest but diverges while something animates, suspect a dropped slide/offset arg.
3. **Mis-typed params.** The `flds/fadds/fstps` path has been mis-typed as
   `int*` (e.g. the chr-sprite chip). When float math reads "integer", look at
   the asm.
4. **Enum VALUE vs NAME.** The decompile shows raw constants; D3D enum 8 is
   ADDSIGNED, not MODULATE2X (the skip-prompt −1-LSB gold bug, quirks §104).
   Map constants through the header, never from memory.
5. **Float literals: recover the BIT PATTERN from .rdata via objdump**, don't
   trust the decompile's decimal rendering. Burned us twice: scale
   `0x3b712c27` (≠ 0.003685), title pulse `−128.0` (≠ the guessed 127).
6. **Recompute hex offsets with a calculator/python.** A miscomputed offset
   (0xA0FC vs 0xB0FC) once "proved" a load path didn't write a field. Verify
   the live address with a Frida read before trusting a conclusion built on it.
7. **`r2` for function shape / control flow** (`nix develop --command r2 -q -c
   'af; pdf' …`), **objdump for short spot-checks.** Both read
   `vendor/unpacked/` (never `vendor/original/`, still SteamStub-encrypted).

## Probing retail (Frida/TTD)

8. **Frida hook arg indexing is 0-based** (`args[0]` = first stack arg for
   cdecl). Off-by-one here looks exactly like a real divergence.
9. **Probe via input traces + the call graph, not direct VA calls.** Direct
   `NativeFunction` calls are ONLY for pure-fn oracles (`diff_test.py`) and
   state-forcing hacks — driving gameplay via direct calls skips the upstream
   state the function actually reads.
10. **Sim counters: onEnter logs PRE-increment; the render consumes +1.** When
    reconstructing a render formula from logged sim state, account for the
    seam (flow-trace timing rule, `flow-trace-cheatsheet.md`).
11. **Capture-time state alignment:** a retail watch/probe row is read one
    sim-tick BEFORE the render that shows it. Align SCREENSHOT diffs by the
    capture-time db054 (frames metadata), not by the probe stream — this was
    the entire 2026-06-04 "walk-cell divergence" (no engine bug existed).

## Divergence-hunt order of operations

12. **Look at the frame first.** For any "did X happen on screen" question,
    open the captured PNG before reasoning from globals — a watch on the wrong
    subsystem produces confident nonsense.
13. **Aligned pixel DIFF before theories** (`trace_studio triage`, the studio
    diff, `pixel_diff.py`) — never iterate on side-by-sides. If state matches
    but output diverges, decode the verts/FVF (`render_diff --explain`).
14. **State "looks identical" but the draw is wrong → inherited device state.**
    Per-frame d3d state tracking misses persistent COLOROP/COLORARG/blend set
    in an earlier frame; replay with `d3d_state_at_draw.py` (the white-UI
    COLORARG leak).
15. **Billboard occlusion bugs that ZWRITE toggles don't fix = per-pass
    z_far/projection mismatch** (often a stubbed camera global at 0) — quirks
    §93. Diagnose via synced d3d-trace NDC-z, not Z-state toggles.
16. **If 1:1 needs the whole path, port the whole path** — probe the live call
    graph and match retail structurally; a faithful leaf under a synthetic
    caller still diverges.
17. **Capture retail ground truth with every chip**: args/retval + state-write
    delta via the flow-trace probes (annotate both sides as you port — that IS
    the state tool now).
18. **A loop-local loaded by `fildl` may be a per-iteration COUNTER — check the
    loop tail for `decl/incl` of the same slot before calling a factor
    "per-row/per-call".** The dialogue reveal fade (`FUN_0047d464`) was ported
    per-ROW because `0x47d4d4 mov %eax,-0xc(%ebp)` (the budget INIT from
    param_6) was read as the only write; the real law re-loads `-0xc` per glyph
    (`fildl` 0x47d528) with a `decl -0xc` at 0x47d60e ⇒ per-CHAR trailing ramp.
    One screenshot of retail mid-reveal (ghost trailing char) falsified the
    per-row theory instantly — see gotcha 12.
19. **Never port an MSVC x87 float-equality as a C `==` on floats — GCC x87
    keeps 80-bit excess precision through assignments/clamps at -O2 and the
    equality goes FALSE where MSVC's `fstp dword` spill made it TRUE.**  The
    PFO.4 terminal gate `if (factor == 1.2f)` (post-clamp, structurally
    always-true in the engine asm) never fired on the mingw i686 build — the
    sale coins converged to the target and hovered forever, no landing pulse
    — while the SSE host tests passed (SSE rounds every op to float32).
    Port float32 equality as a BIT-PATTERN compare (`f_to_bits(a) ==
    f_to_bits(b)`), and smoke any FP-compare-bearing chip with
    `-O2 -mfpmath=387` on the host (the tests' default SSE hides this whole
    class).  RE §21.31.3.
