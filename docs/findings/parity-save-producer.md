# Save pillar PRODUCER + canonical state model (ST-00/ST-01) — 2026-07-16

> Wave-1 of `plans/parity-evidence-roadmap.md` §7 (ST). The `save` pillar was the
> UNBUILT `NOT_CAPTURED` stub `parity_prove.py` carried; THIS is its producer +
> the canonical state model it localizes against. Roadmap **M1** (first-state
> localization). Commits `fe4101f` (ST-00) + `33f706b` (ST-01). Vocabulary:
> `../reference/parity-vocabulary.md`. Model: `../reference/canonical-state.md`.

## Why

The `save` proof pillar answers "does the save each side WRITES match retail's,
byte-for-byte?" — the persistent-state axis of parity, invisible to every
pixel/frame pillar. It was `NOT_CAPTURED`: nothing produced a comparison.

## Survey first — capture already exists; only the comparator was missing

A save-write-path survey (both sides) settled the shape decisively: **no new
engine or agent work is needed.** A `scenario-test <scen> --target both` drive on
a save-committing scenario ALREADY leaves two byte-comparable 18,838,832-byte
`save.dat`:

- **port** → `run/openrecet/saveout/save.dat` via `--save-write-dir`
  (`src/save_io.c write_arena_to`, unconditional in `scenario-test.py:495`);
- **retail** → `run/retail/saveout/save.dat` via the CreateFileW/A Frida redirect
  hook (`tools/frida/openrecet-agent.js:2280`, sandbox `frida_capture.py:1277`),

both seeded from the SAME `{savefile}` — so untouched storage banks match and any
diff is a real port↔retail gap. What was missing was purely the **comparator**.

## ST-00 — the canonical state model (`fe4101f`)

The engine keeps two ~18 MB arenas that share ONE layout: the **save** arena
(disk `save.dat`, persistent) and the **working** arena (live game state,
volatile-deterministic) — `findings/save-working-arena.md`. So one map localizes
either.

- **`docs/schemas/state-map-v1.json`** — the machine region map: the named layout
  (header sliders/slot cursor + per-bank gold/day/rank/item-table/display-grid/
  ranking/chara/closeness/deco/news/sold-lists/checksum/…) transcribed from
  `src/save_bank.h`, each entry citing its `#define`/`DAT_`. **RE correction baked:**
  header dword 6 = `hidden_char_unlocked`, dword 7 = `last_slot_used` (the
  `#define SAVE_HEADER_FIELD_LAST_SLOT 7` is authoritative; save_bank.h's stale
  early comment putting last_slot at dword 6 is superseded).
- **`docs/reference/canonical-state.md`** — the prose model: the four state classes
  (**persistent** = save arena, the `save` pillar; **volatile-deterministic** =
  working arena + the flow_diff scene state a future `state` pillar/ST-03 captures;
  **environmental** = load-timing/phase origins; **unknown** = unmapped bytes),
  and the normalization policy (float by bit pattern; no pointers in the arena;
  unknown bytes INCLUDED + reported `(unmapped)`, never masked or fabricated;
  checksum = a derived echo of content).
- **`tools/parity/state_map.py`** — the offset→`Locus` localizer: an absolute byte
  offset resolves to `bankN/region[elem]` (or fail-SAFE `bankN/dword0x…(unmapped)`).
  Byte-addressed fields win over the containing dword region.

## ST-01 — the save pillar producer (`33f706b`)

Mirrors the pixels producer's split so the truth-defining core is testable with
NO drive dependency:

- **`tools/parity/save_producer.py`** — `compare_saves(port, retail, state_map)`
  diffs the two arenas (numpy), localizes the first divergence via the state map,
  and buckets EVERY diff by `(scope, region)` collapsing array elements + banks
  (so "6435 bytes, ranking_records, banks 1–99" reads clearly). **FAIL CLOSED:** a
  missing file or a wrong-size buffer RAISES — never emits `identical` from partial
  evidence. Drivers: `produce(port_dat, retail_dat, out)` (stamps each save.dat
  SHA-256 as `source`) + `produce_from_run_dir(run)`.
- **`tools/parity/save.py`** — `adapt_save` → `PASS` (byte-identical) / `FAIL`
  (localized `first_divergence`, `path = bankN/region`) / `NOT_CAPTURED` (absent) /
  `INCONCLUSIVE` (corrupt/stale-source). The save is scenario-scoped, so
  `first_divergence.logical_frame` is the contract join anchor (nominal); the real
  locus is the region `path`. It does NOT bind to `view.json`'s D3D container
  hashes (different evidence source); its provenance is the two save.dat SHA-256s.
- **`tools/parity_save.py`** — producer CLI: reads the newest `--target both` run,
  writes `save-metrics.json` into the v3 window dir so `parity_prove --window`
  consumes it. Wired into `parity_prove.py` (`save` dropped from `UNBUILT_PILLARS`).
- **`tools/test_parity_save.py`** — 41 checks: the **M1 negative test** (a deliberate
  one-byte mutation localizes to the exact region+offset), faithful→PASS, summary
  collapse, fail-closed, adapter edges. Parity suite `41+24+51+60+52` green.

## First real verdict — a truthful FAIL, localized

`parity_save.py house-pause-save-commit` (commits occupied slot 0): **save FAIL**,
6836 / 18,838,832 bytes differ across 4 regions:

| region | bytes | banks | class | reading |
|---|---|---|---|---|
| `occupied_playtime` | 2 | 0 | persistent | **first div** @ byte 2840 — total-playtime FRAME count (port 20906 / `0x51aa` vs retail 29643 / `0x73cb`): environmental **phase origin** (the port skips the intro-video freeze), not a logic value — a `{phasepin}`-class pin, not a code gap |
| `(unmapped)` | 1 | 0 | unknown | dword `0xb37d`, one past `deco_carpet` |
| `checksum` | 398 | 0–99 | persistent | the per-bank XOR — a **derived echo** of the content diffs (map policy predicted this) |
| `ranking_records` | 6435 | 1–99 | persistent | **REAL lead** ↓ |

The model turns a raw 6836-byte diff into one phase-origin field (pin), one
checksum echo (ignore), and one genuine port↔retail divergence to chase.

## Leads (for follow-up arcs, NOT blocking the producer)

- **`ranking_records` banks 1–99 — a CONFIRMED port bug the pillar caught AND
  attributed.** An independent seed-vs-output check (the input `{savefile}` both
  sides loaded vs each side's written arena) settles it: for banks 1–99 the RETAIL
  output PRESERVES the seed's ranking records (`seed == retail`), while the PORT
  ZEROES them (`seed != port`; port dwords read 0 where seed and retail read
  16/1/2/…). Bank 0 (the committed slot) is preserved on BOTH, and everything ELSE
  in banks 1–99 matches — so it is NOT a full bank re-init; the port specifically
  drops the per-slot RANKING summary the ranking screen (`FUN_0049f012`, base
  `DAT_0450b170`) reads. A real fidelity gap (the port would render wrong rankings
  for the OTHER save slots), invisible to every pixel/frame pillar — the save
  pillar's raison d'être, demonstrated on its first real run. Root cause (a
  follow-up PORTING arc, not this tooling arc): a stray ranking clear or a per-bank
  magic+checksum-gate / `save_bank_init_one` / `save_bank_checksum_ok` divergence in
  the port's boot-time `save_bank_init_all` (the port judges the seed's non-active
  banks differently from retail's `FUN_004901c2`). → **`PORT-DEBT(save-ranking-
  nonactive-banks)`** (not yet code-tagged).
- **`occupied_playtime`** is the frame-count phase origin — a candidate `{phasepin}`
  extension (pin the playtime accumulator origin) so a save proof isolates logic
  from phase, matching the pixel/state loop.

## Follow-ups (roadmap)

- A full `parity_prove` bundle: add a `proof:` block (schema_version 2,
  `required_pillars: [identity, save]`) to `house-pause-save-commit` + capture a v3
  window, so the save FAIL lands inside a content-addressed bundle (parallels the
  pixels-producer arrprobe bundle). The wiring is done + unit-tested; this is the
  end-to-end packaging.
- **ST-02** (canonical encoder + Merkle roots over the state tree), **ST-03**
  (expand retail+port state capture → the `state` pillar for the volatile class),
  **ST-04** (the first-divergence state report reusing `state_map.locate`).
