# Canonical state model (ST-00)

> **Status:** v1 ADOPTED 2026-07-16 (roadmap `plans/parity-evidence-roadmap.md` §7).
> R3-owned; field selection / normalization / a new class needs R3 sign-off.
> Machine map: [`../schemas/state-map-v1.json`](../schemas/state-map-v1.json).
> First consumer: the `save` pillar (ST-01, `tools/parity/save.py`). Vocabulary:
> [`parity-vocabulary.md`](parity-vocabulary.md).

## Why

A parity pillar that compares "state" needs to know **what a byte MEANS, who owns
it, and whether it is even supposed to be equal** — otherwise a diff is an
un-actionable number and a benign byte reads as a regression. This doc defines the
model; `state-map-v1.json` is the field-level detail a tool localizes against.
M1's exit ("report a mutation at the correct anchor, region, object, and FIELD")
is met the moment a divergent byte resolves to a named region here.

## The two arenas (one layout)

The engine keeps **two** ~18 MB arenas, both `0x0b10` header + 100 banks × `0x2dfc8`
(`findings/save-working-arena.md`, `src/save_bank.h`):

| arena | base | role | port owner | class |
|---|---|---|---|---|
| **save** | `DAT_056e5770` | disk mirror = `save.dat` | `save_bank.c` | **persistent** |
| **working** | `DAT_044e2c88` | live game state (slot 0) | `save_work.c` | **volatile-deterministic** |

They share ONE byte layout, so a single map (`state-map-v1.json`) localizes an
offset in either. A `save.dat` is the save arena dumped verbatim
(`src/save_io.c write_arena_to`); on a commit the working slot 0 is copied into a
save bank, its checksum re-stamped, and the whole arena written.

## The four state classes

Every byte the model tracks is exactly one of:

1. **persistent** — on disk in `save.dat`; survives sessions. THE save arena. This
   is what the **`save` pillar** (ST-01) compares byte-for-byte. Fully enumerated
   in `state-map-v1.json` (from `save_bank.h`): header sliders/slot cursor + per-bank
   gold, day, rank, item table, shop display grid, ranking records, chara stats,
   closeness, deco, news, sold lists, checksum, …
2. **volatile-deterministic** — live game state, reproducible under pinned inputs
   but not itself on disk unless committed. The **working arena** (same layout;
   fields tagged `working_alias` in the map) PLUS the non-arena scene state the
   **`state` pillar** (ST-03/ST-06, expanded across all 5 scene domains —
   `findings/parity-state-producer.md`; map `schemas/state-volatile-v1.json`;
   Merkle roots over `tools/parity/state_codec.py` + `state_merkle.py`) captures:
   - **Title / Config:** `title_menu`, `fade_tick`
   - **Shop / Economy:** `customer_service`, `player`, `companion`, `shop_npc`, `interaction`, `camera`, `dust_fx`, `dialogue_house`
   - **Town / World Map:** `town_map` (`town_target`, `town_cursor`, `facility_mode`, `guild_cursor`)
   - **Dungeon / Combat:** `dungeon_sim` (`floor_index`, `mob_count`, `player_hp`, `player_sp`)
   - **Scripted Events / Timeline:** `dialogue_intro`, `sim_dispatcher` (`clock_phase`, `shoptime`, `shopaccum`, `cardday`)
   - **Core Engine:** `rng` state (`DAT_006023a0`), `phase` (`db054`, `gsim`)
3. **environmental** — load-timing / host-dependent ORIGIN, not a logic value: the
   phase offset the intro-video freeze bakes into `db054`/playtime, the
   completion-based load-bracket frame counts (`findings/phase-state-census.md`).
   A constant offset here is `phase`, normalized by a pin — NOT a value the pillar
   should demand equal without one.
4. **unknown** — byte-preserved but semantics not yet mapped (the ~700 header
   scratch dwords, unlabeled bank gaps). INCLUDED in a byte-exact comparison and
   reported as `(unmapped)` — never silently masked, never given a fabricated name
   (roadmap ST-00 stop condition).

## Normalization policy

- **Float:** f32/f64 compared by **bit pattern** (the project's x87 invariant) — no
  epsilon. A named float region carries `type` so a report can print the value;
  equality is over raw bits.
- **Pointer/handle:** the arena is **pure value data** — a disk mirror with no
  pointers or OS handles. Nothing to normalize away; every dword is comparable.
- **Padding/unknown:** included byte-exact, reported `(unmapped)`. Excluding a byte
  requires a named region with an explicit reason (none excluded in v1).
- **Checksum:** each bank's XOR checksum (`0xb7f1`) is a pure function of its
  content — a checksum diff is a **derived echo** of a content diff upstream, never
  an independent finding (`state-map-v1.json policy.checksum`).

## Scene applicability

Persistent fields are present in every scene (the arena is always mapped). Their
MEANING can be scene-gated (news list populates only day > 8; the working-arena
aliases `shop_day`/`clock_target` are live only in HOUSE/shop). A contract that
compares saves at a scene where a region is not yet initialized should expect the
fresh-init value, not a played value. The volatile scene state (class 2) is
scene-specific by construction; ST-03/ST-06 expand it scene by scene.

## How consumers use this

- **ST-01 `save` pillar** (`tools/parity/save.py` + `save_producer.py`): compares
  two `save.dat` a `--target both` drive writes; `state_map.py` resolves the first
  divergent byte → `bankN/region[elem]`, and buckets all diffs by region.
- **ST-02** (planned): a canonical encoder + Merkle roots over this tree (root →
  persistent/volatile → subsystem → object → field) so equal semantic state at
  different addresses hashes equally and one mutation reports one leaf path.
- **ST-04** (planned): the first-divergence state report reuses `state_map.locate`.

## Worked example — the first real result

`house-pause-save-commit` (`--target both`, commits occupied slot 0), the first
save-pillar verdict: **FAIL**, 6836/18,838,832 bytes differ across 4 regions:

| region | bytes | banks | class | reading |
|---|---|---|---|---|
| `occupied_playtime` | 2 | 0 | persistent | **first div** @ byte 2840 — total-playtime FRAME count (port 20906 vs retail 29643): environmental phase-origin, not a logic value |
| `(unmapped)` | 1 | 0 | unknown | dword `0xb37d`, past `deco_carpet` |
| `checksum` | 398 | 0–99 | persistent | derived echo of the content diffs |
| `ranking_records` | 6435 | 1–99 | persistent | **confirmed port bug** — the port ZEROES the ranking of non-active banks 1–99 while retail preserves it from the seed (`seed==retail`, `seed!=port`); a real fidelity gap invisible to every pixel/frame pillar (`PORT-DEBT(save-ranking-nonactive-banks)`) |

The model turns a raw 6836-byte diff into: one phase-origin field (pin it), one
derived-checksum echo (ignore), and one genuine port↔retail load-init divergence
to chase — exactly the first-state localization M1 asks for.
