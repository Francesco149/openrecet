# Phase-state census — HOUSE arrival (2026-06-09)

What `tools/phase_census.py` is and why: `docs/audits/2026-06-09-methodology-audit.md`
§3 T3 + §4. Short version: run the SAME side twice with deliberately different
pre-anchor timing (variant B's boot inputs shifted +Δ), `{memsnap}` both runs at the
same anchor-relative frame, byte-diff the writable sections. Every differing byte is —
by construction — **load-timing-dependent** state. **Discovery** mode (pins stripped,
snapshot at the window start) enumerates the full phase/RNG-bearing set; **pinned** mode
(canonical pins, snapshot at pin+settle) is the **pin-completeness gate** — a complete
pin yields an empty engine diff.

This is the standing record of what the census found per scene. Re-run after an arc /
per new scene; fold real hits into `{phasepin}` (or here, as accepted-benign).

## Method notes (so a re-run reproduces)

- Bench: `phase_census.py run --scenario house-loaded-display --side port --mode {discovery,pinned}`.
  Δ=37 (odd; shares no factor with the 4/8/10/16/40/64 anim cycles). Pinned snapshot at
  pin+64 (past the ~48f companion spring-lerp settle).
- **Control run (A vs A', same timing) is essential.** It classes per-run noise as
  `volatile` — without it the diff is dominated by heap-allocation churn that differs
  between *any* two runs. On HOUSE it cut 376/532 ranges.
- **`ptr-layout` class:** a differing range whose every word is null-or-pointer on both
  sides is load-ORDER layout (a relocated arena/hash-table pointer), not frame-affecting
  phase. Phase counters are small ints/floats.
- **VA attribution:** port dumps record section RVA; the differ resolves symbols at the
  fixed link base `0x400000` (the dump's `link_base` is the per-process ASLR base —
  load-invariant RVAs + the static link base are what `nm` reports).

## Result — port, HOUSE (house-loaded-display)

Discovery: ~530 raw ranges. Pinned + control + classing → the engine-signal set is **one
symbol**: `g_scene1_overlay_slots`. Everything else triaged:

| symbol | class | verdict |
|---|---|---|
| `g_tab` (lnkdatas/asset hash table) | known-benign | load-ORDER pointer layout; engine derefs by key, not slot. `A=0 / B=heapptr`. |
| `g_scene_title_anim` | known-benign | title menu state — inactive in HOUSE; boot residue. |
| `g_sim_buttons` | harness | the +Δ input shift itself (`0x00030003` vs `0x00010001`). |
| `g_singleton_mutex` | harness | per-process mutex HANDLE. |
| heap/arena pointers (`g_arena`, `g_item`, …) | volatile/ptr-layout | save deserialization + allocation layout; not phase. |
| **`g_scene1_overlay_slots`** | **engine — REAL** | see below. |

(The benign symbols are recorded in `phase_census.py` `PORT_BENIGN_PAT` so the gate reads
clean against them; a NEW symbol appearing in a future run is the signal.)

## The one real lead — `g_scene1_overlay_slots` (the 目玉 sparkle residue)

Under the canonical pin, the two timing variants STILL differ across the overlay-slot
array (per-slot stride 0xdc): floats `0x3f7d70a4`≈0.989 / `0x3f000000`=0.5, a count
`0x18`=24, and small per-slot lifetime/phase counters (`+0x74: 5↔0xd`, `+0x308: 0x15↔5`).
These are the **records-A 目玉商品 sparkle particles** over the displayed swords.

**Why the pin misses it:** `{phasepin}` re-seeds the LCG (19937) and zeroes
`g_sim_frame_count` so the sparkle EMITTER fires in phase — but it does **not** clear the
slot array, so particles emitted during the load/settle frames *before* the pin survive
with a load-timing-dependent age. The emitter being in phase from the pin forward doesn't
erase the pre-pin residue.

**Status: accepted-known for now, NOT folded into the pin.** It is sub-visible — the
pinned sparkle reads 1:1 vs retail by eye (FRONT / `shop-item-display-RE-status.md`) and
the residual particles age out within a few frames. Folding it in (zero
`g_scene1_overlay_slots` + the retail mirror at the pin) is a plausible correctness
improvement but is a **visually-verified change**, not a blind one — the audit's
triage→record→decide discipline. It is the census's first actionable lead; do it in a
sparkle-parity pass if/when the residue is shown to matter, or to make the sparkle fully
load-deterministic.

## Not yet run

- **Pinned RETAIL census** (same bench, `--side retail`) — needs the Frida host; the
  `{memsnap}` retail path is built + wired (agent + `frida_capture` regions) but unrun
  here. The KNOWN_RETAIL table in `phase_census.py` pre-annotates the pinned DAT_ set.
- **Other scenes** (world map, dungeon when it lands) — one discovery+triage pass each.
