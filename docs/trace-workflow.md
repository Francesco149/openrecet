# Trace workflow — the canonical reference for frame-by-frame TAS work

This is the single operational doc for trace work on openrecet. If you want the
**why / where it's going**, see `docs/plans/tas-framework.md` (the vision). This
doc is the **how**.

A *trace* is a deterministic input recording the engine can replay bit-for-bit.
Replay is anchored to in-engine **events** (anchors), not absolute frame numbers,
so it survives the huge run-to-run boot/load timing jitter (a new-game load that
takes 6 frames live takes ~2700 under `--turbo`). Two replays of the same
anchored, RNG-pinned trace are bit-identical.

---

## The two workflows

### A) I author a synthetic trace, push it, iterate

1. Write/derive a `.jsonl` segtrace (ops below). For free-roam work, the proven
   boot prefix is the `house-wall-collide` scenario (`tests/scenarios/house-wall-collide/trace.jsonl`)
   — it A-spams title→new-game and the prologue dialogue **auto-completes**
   (reaches `FREEROAM_START` ~frame 4283 with no skips needed).
2. Export a contiguous capture window (anchor-relative):
   ```
   nix develop --command python3 tools/export_trace.py <trace.jsonl> \
       --caprange START,COUNT --run-dir runs/trace-export/<name> --name "label" --max-frames N
   ```
   `--caprange START,COUNT` = COUNT frames starting START frames after the trace's
   **final `wait` anchor** (anchor-relative; jitter-immune). `--max-frames` must
   exceed where the window lands in turbo (anchors stretch — give headroom, e.g. 4500).

   **A RAW recording is auto anchor-gated** (`emit_anchor_segments`) when it carries
   `{anchor}` rows — every recorded anchor becomes a `{wait}` sync point, so the
   caprange resolves to `last_anchor_frame + START`, not boot. (Before 2026-06-03
   export_trace flat-distilled raws → the caprange anchored to boot frame 0 and
   turbo load-stretch drifted the window run-to-run; that's fixed.) `--flat` forces
   the legacy boot-anchored distil; `--house-segtrace` the boot→HOUSE wrap.

   **Exported frames are renumbered to anchor-relative 0-based** (`frame_00000.png`
   = window start). The engine names frames by absolute `g_tick.frame_count` (which
   jitters with load time); export_trace subtracts the first captured frame so the
   FILENAME *is* the stable anchor-relative index. So a reference like `frame_00186`
   / `crop … frame=f=186` resolves to the same sim instant in every replay of the
   same trace — paste it back and it's found instantly. `meta.jsonl` keeps the
   absolute frame as `frame_abs`; `global.json` records `final_anchor` + `frame_base_abs`.
3. Push as a `trace` card (NixOS: feed.py needs `nix run nixpkgs#python3 --`):
   ```
   nix run nixpkgs#python3 -- /opt/src/llm-feed/feed.py trace \
       --dir runs/trace-export/<name> --title "..." --note "..."
   ```
   Opens its own viewer tab (`/trace.html?id=<id>`): ←/→ ±10, `,`/`.` ±1, Home/End,
   play at fps, `c` marks captures, **drag a box → `crop id=… box=… frame=f=<n>`**.
4. The user flips through and pastes back frame hints / a `crop …` reference.
   `feed.py get <id>` recovers the source paths; the asset PNGs are under
   `/opt/src/llm-feed/data/assets/<id>/frame_NNNN.png`.

### B) The user recorded a trace and wants analysis

1. **Record (user drives):** `tools/run-openrecet.sh --debug` (console build,
   real-time). **F2** start/stop, **F3** capture-point, **F4** call-trace window.
   On stop it writes `openrecet-trace-<pid>-<seq>.raw.jsonl` **in the game asset
   dir** (`/mnt/c/Program Files (x86)/Steam/steamapps/common/Recettear/`) and prints
   the path. The raw carries per-frame masks + `{esc}` + **every anchor firing**
   (`{"anchor":NAME,"frame":REL,"gframe":ABS,"rng":LCG}`).
2. **Distil (anchor-gated):**
   ```
   nix develop --command python3 tools/distill_trace.py <raw> --anchor-segments -o <out.jsonl>
   ```
   Every recorded anchor becomes a `{wait}` sync point; inputs/escs/captures between
   anchors are emitted relative to the preceding one, and RNG is re-pinned at each
   anchor to the recorded value. The result replays correctly under turbo with **no
   hand-rebasing** — an ESC `N frames after a dialogue anchor` fires N frames after
   that anchor actually lands.
3. Export + push exactly as workflow A (steps 2–3). For determinism, run twice and
   compare by **anchor-relative index** (below).

> Old recordings (pre-2026-06-03) have no `{anchor}` rows → `--anchor-segments`
> errors. Ask the user to re-record with the current `--debug` build.

---

## Anchoring rule (standing, from the user)

- **Default: anchor at `FREEROAM_START`** — the player gains free control (fires
  after the 2nd ESC→confirm skip, or after the prologue auto-completes). This is
  the sync point to start a free-roam walk from. `HOUSE_FREEROAM` is **too early**
  (load overlay dropped but still mid-iv1_2 conversation; player locked).
- **Cutscene content: anchor at `TEXT_ANIM_START` / `TEXT_ANIM_END`** (a dialogue
  line revealing / fully revealed).
- Anchors auto-sync inputs to jitter. **Never hand-rebase or capture by absolute
  frame.** The ultimate goal is to keep adding anchors until any point of a full
  playthrough can be reached deterministically (see the vision doc).

Anchor list lives in `src/anchor_trace.c` (`g_anchors[]`): `BOOT`, `NEW_GAME`,
`LOADING_START/END`, `HOUSE_FREEROAM`, `TEXT_ANIM_START/END`, `EXTRA_SPRITE_*`,
`DLG_LINE_SHOW/CLEAR`, `CONV_POSE_START/END/BLINK`, `FREEROAM_START`. The port
emits `anchor: {...}` on stderr; the recorder logs them into the raw; the retail
Frida agent mirrors them (TODO: `FREEROAM_START` not yet on the retail side).

---

## Segtrace ops (`--input-segtrace FILE`, JSONL)

| op | meaning |
|---|---|
| `{"frame":k,"buttons":"0xNN"}` | hold mask at base+k (base = current segment's anchor frame; 0 for the boot segment). Holds until the next change-point. |
| `{"wait":"ANCHOR"}` | segment break: next segment's frame 0 = the frame ANCHOR next fires (strictly after entry, so a repeated anchor resolves on its NEXT firing). Spam-until-anchor: the segment's entries run until the anchor fires. |
| `{"wait_until":{"va":"0x..","type":"f32","op":"<=","val":2.0}}` | break on a live-global predicate instead of an anchor (e.g. hold UP until `pz<=2.0`). Ops `<= >= < > == !=`. |
| `{"capture":N}` | screenshot at base+N (anchor-relative). |
| `{"caprange":[start,count]}` | contiguous capture window [base+start, base+start+count) — for frame-by-frame export (bypasses the 32-frame `{capture}` cap). |
| `{"rngseed":[frame,value]}` | force the LCG state to `value` at base+frame (before that frame's sim). |
| `{"esc":N}` | synthesise the engine ESC dispatch at base+N (arms the skip prompt in-event; quits at the title). |
| `{"calltrace":N}` or `[start,len]` | arm the call tracer for an anchor-relative window. |
| `{"gframe":[frame,value]}` | **EXPERIMENTAL** — pin `g_tick.frame_count` to `value` at base+frame (for frame-count-derived state like the time-of-day HUD clock). **Do NOT combine with `{caprange}`** — the caprange window is computed from the real anchor frame but `capture_in_range` tests the pinned counter, so capture breaks. |

Button bits (`src/input.c`): UP `0x04`, RIGHT `0x01`, DOWN `0x08`, LEFT `0x02`,
Z/A (confirm) `0x10`, ESC `0x100`.

Parser/struct: `src/input_segtrace.{c,h}` (host-tested in
`tests/test_input_segtrace.c`). Distill emits these from a raw recording.

---

## Determinism & comparing two runs

- Anchor-relative capture + RNG-pin-per-anchor ⇒ the sim is **bit-exact** across
  runs. Validated: two runs of an `--anchor-segments` trace were 0/504 frames
  different even though the final anchor fired at 641 vs 647 (load jitter).
- The anchor frame jitters in absolute terms, but **export_trace renumbers frames
  to anchor-relative 0-based**, so the **filenames already ARE the anchor-relative
  index** — `frame_00186.png` is the same sim instant in every run. Compare two runs
  directly by filename (or `meta.jsonl` with the `frame_abs` field stripped). The raw
  absolute frame is preserved as `meta.frame_abs` if you need it. (Verified: the
  documented `frame_0186` standing-dust repro reproduces bit-exactly — 0.000 mean
  abs diff — from a fresh raw replay.)
- PNG encode is deterministic, so identical framebuffer ⇒ identical bytes ⇒ `cmp`/
  `md5` per index is a valid bit-exactness test.
- Residual leak (when present): the time-of-day HUD clock keys off the absolute
  `g_tick.frame_count`, so it can differ run-to-run. `{gframe}` pins it
  (experimental, not with caprange). For dust/character work it's irrelevant — the
  sim and dust (pinned LCG) reproduce exactly.

`export_trace.py` writes `frames/` + `meta.jsonl` (per-frame px/pz/anim/oct/rng/…)
+ `global.json` (rng seed, the runnable `trace_jsonl`, anchor offset). The feed
`trace` card round-trips: `feed.py trace-export <id> -o out.jsonl`.

---

## Retail side

Drive retail through the Frida agent: `tools/frida_capture.py --input-segtrace …
--frida-remote cutestation.soy:27042 --hide-window --turbo --silent-audio`
(agent `tools/frida/openrecet-agent.js` mirrors the segtrace ops + anchor stream;
`--watch NAME=0xVA:type` for per-frame globals; `tools/kill_retail.py` after;
restart frida-server if captures degrade). Capture both targets at the same
resolution and diff via `tools/pixel_diff.py` / `tools/compose_comparison.py`.

---

## Legacy (still wired, superseded for new work — do not use for new traces)

- `distill_trace.py --house-segtrace` — wraps a recording onto the proven
  new-game→HOUSE intro and rebases to a fixed anchor+1565 idle offset. Used to
  generate the existing `tests/scenarios/*/trace.jsonl`. Superseded by
  `--anchor-segments` for any recording that carries `{anchor}` rows.
- `--input-trace-replay` (absolute-frame trace) — still used by
  `tools/render_trace_gate.py` and legacy scenarios. Use `--input-segtrace`.
- `--auto-z-spam` — still wired into the `dump_*_groundtruth.py` tools; replaced
  by `{"wait":"ANCHOR"}` for new drives.
