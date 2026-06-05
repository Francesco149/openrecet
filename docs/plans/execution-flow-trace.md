# Plan — Execution + dataflow trace (the divergence drill-in)

**Status:** core tooling LANDED 2026-06-05 (increments 1–3). Now growing field
coverage along the Phase-2 sweep (increments 4–5).
**Why:** the d3d render-diff (`render_diff.py --explain`) names the *wrong draw*; it
does not name the *logic cascade* that produced the wrong state. The call-trace
(`call_trace_diff.py`) names *which functions ran* but is **data-blind, order-blind, and
reports an unordered set diff** (see the gap analysis below). This layer is the **primary
way to drill into a divergence**: per frame, on both sides, match the call chain AND the
data moving through it, and name the **first call (in execution order) whose inputs matched
but whose output/state diverged**, attributed to a parity pillar.

Target model (user's words): *map each retail function call to our equivalent, with a few
clearly-documented special cases; the call chain and the data used/moved should match each
frame, besides the occasional benign divergence (memory layout etc.).*

## Gap analysis of the existing tooling (what we keep / what's missing)

KEEP — the bones are right:
- `CALL_TRACE_ENTER(0x4xxxxx)` annotation = the explicit retail→port map (the `va` is the
  join key; false-positives impossible). `port-ledger.json` is its comprehensive form.
- Frida hooks the full engine call graph (~1979 VAs); `diff_test.py` (E.4) already reads
  retail args/globals/retval via Frida bit-exact — that's the data-capture machinery.
- `call_trace.c` frame/window gate + the segtrace `{calltrace}` windowing.

MISSING — why it's not good enough as the drill-in:
1. **Data-blind (decisive).** The live trace records only *that* a function ran
   (`{va, ret_va, ts}`), never the data in/out. Identical call set + divergent value
   cascade ⇒ `call_trace_diff` shows all-green. `diff_test.py` has the data half but only
   offline, on isolated leaves — never in the live per-frame trace.
2. **Call SET, not CHAIN.** `call_trace_diff` uses `Counter[va]` — order + nesting thrown
   away. "The call chain matches" is not actually checked.
3. **Verdict not root-cause-ordered.** Unordered set diff, no "first divergent call in
   execution order", no pillar attribution (logic / phase / RNG / upstream).

## Design — extend `call_trace`, don't duplicate

Shared gate (`g_f`, `g_emit_this_frame`, `g_cur_frame`, windows) stays; add a
field-bearing event built by a BEGIN/FIELD/END block and a per-frame `seq` counter for
execution order. The retail→port payload is **declared once per function on each side,
joined by `(va, field-name)`** — the port emits its own C values (exact, free); a retail
JSON spec tells Frida how to read the same-named values.

### Schema (both sides, one JSONL row per traced call)
```jsonc
{"va":4744975, "frame":N, "seq":S, "depth":D,        // seq = per-frame call order
 "ret_va":R, "f":{"col":-7, "rng0":0.5, "f_idx":3}}  // f = declared payload (omitted if none)
```
`seq` gives execution order; `depth` (enter++/exit--) reconstructs the chain/tree. The
legacy `{va,ret_va,frame}` rows stay valid (no `f`).

### Port API (`call_trace.h`) — buffered, atomic per event
```c
CALL_TRACE_BEGIN(0x48670f);        // opens event: va + ret_va + seq++, into a static buf
CALL_TRACE_I32("col", col);        // append typed fields (i32/u32/f32/f64/hex)
CALL_TRACE_F32("rng0", rng0);
CALL_TRACE_END();                  // fwrite the whole line atomically
```
Discipline: emit fields at entry (the inputs) BEFORE any traced sub-call, then END — so
BEGIN..END is atomic and nesting can't interleave the shared buffer (a nested BEGIN while
an event is open is dropped + counted). `CALL_TRACE_ENTER(va)` stays = `BEGIN;END` (no
fields). Exit/retval = a second `phase:"exit"` event at the return point (later increment).

### Retail spec (`tools/flow/retail_fields.json`) — drives the Frida reader
```jsonc
{ "4744975": {                       // engine VA (decimal or 0x)
    "fields": [
      {"name":"col",  "src":"arg", "index":0, "type":"i32"},     // index 0 = FIRST param
      {"name":"rng0", "src":"global", "va":"0x5ce3c4", "type":"f32"},
      {"name":"f_idx","src":"argderef", "index":0, "off":0, "type":"i32"}
    ] } }
```
`src` ∈ arg | global | argderef | retval (exit). Reuses `diff_test.py`'s register/memory
readers. Field NAMES must match the port's `CALL_TRACE_*` names — that's the data join key.
**`index` is Frida's 0-based `InvocationArguments` — index 0 is the first cdecl param**
(validated 2026-06-05 on `FUN_00404efc`; an earlier draft of this doc said 1-based — it
was wrong, no spec had exercised `arg`/`argderef` until then).

### Diff (`tools/flow_diff.py`) — the root-cause verdict
Walk both frames in `seq` order; align the call chains (by `va` sequence, tolerant of
benign one-sided calls flagged structural). For each aligned call, compare `f` (float eps,
int/hex exact). Report the **FIRST** divergence in order:
- chain: retail called X, port called Y / skipped (structural), or
- data: call matches, field F differs — `va FUN_x  field "col": retail -7  port -5`.
Attribute to a pillar (phase/RNG via the existing `phase_probe` logic; else logic) and
stop — that's the cascade root. `call_trace_diff.py` (Counter view) stays for coarse
coverage; `flow_diff.py` is the ordered+data drill-in.

## Incremental landing (commit as you go)
1. ✅ **Port C: BEGIN/FIELD/END + `seq`** in `call_trace.c/.h`; seed probe `fade_tick`.
   Validated on a title capture (`seq:45, f:{phase,counter,duration,mode}`). `depth`
   deferred (port lacks exit hooks; `seq` order is enough for chain alignment now).
2. ✅ **Retail spec + Frida reader**: `tools/flow/retail_fields.json` + `agent.js`
   `flowReadField` (global/arg/argderef) + per-frame `ctNextSeq()`. Validated live: retail
   `fade_tick` reads `f:{phase:0,counter:0,duration:170,mode:0}` from its globals.
3. ✅ **`flow_diff.py`**: seq-ordered chain align + field compare + first-divergence
   ([chain]/[data]) + `--mapped-only`. Validated synthetic + real (names
   `duration: retail=170 port=0`). Pillar attribution = the `benign` field mark (full
   tagging deferred).
4. **Seed one real path** end-to-end on a SYNCED segtrace capture (`export_trace
   --d3d-trace-verts` + `frida_capture` with the field spec): declare fields down one chain
   (candidate: the player controller, or `FUN_0048670f` HOUSE update → sparkle emitter) and
   root-cause a live divergence. ← next
5. **Grow coverage with the Phase-2 sweep**: every function the frame-0-forward sweep
   touches gets its fields declared (port `CALL_TRACE_*` + a `retail_fields.json` entry) —
   coverage tracks the sweep, not a big-bang instrumentation.

## Workflow (once both sides carry fields)
```sh
# synced capture (segtrace window), BOTH sides with call-trace + the field spec:
python3 tools/export_trace.py tests/scenarios/<scn>/trace.jsonl --caprange S,N \
    --run-dir runs/<x>/port --call-trace            # port emits CALL_TRACE_* payloads
python3 tools/frida_capture.py --remote cutestation.soy:27042 \
    --run-dir runs/<x>/retail --input-segtrace runs/<x>/port/trace.work.jsonl \
    --call-trace --turbo --silent-audio             # retail reads retail_fields.json
# root-cause: first call (in order) whose inputs matched but output/state diverged
python3 tools/flow_diff.py --mapped-only \
    --retail runs/<x>/retail/call_trace.jsonl --port runs/<x>/port/call_trace.jsonl
```

## Special cases (the "documented exceptions")
Benign divergences are declared, not silently tolerated: raw pointers / heap addresses
(memory layout), phase-origin counters (`db054` etc., per `{phasepin}`), RNG seed origin
(`{rngseed}`). A field marked benign in the spec is compared structurally (present/shape)
not by value. Everything else must match — full port, not MVP.
