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

- **✅ FIXED 2026-07-16 — the `ranking_records` banks 1–99 catch was a REAL port bug;
  now byte-exact vs retail.** The region is a MISNOMER: dword `0x9e76` is the
  **encyclopedia (図鑑) discovery store** (`encyclopedia.c` ENC_DISC_BYTE `0x279d8`;
  `FUN_0049f012` = `encyclopedia_setup`, which the port author mislabeled "RANKING" —
  `scene_title.c:740`). Record = `{category_key@+0, catalog_count@+1, discovered_flags@byte8+}`.
  **Root cause (byte-confirmed, 3-way seed/port/retail):** the seed's banks 1–99 are
  **never-committed slots** — valid magic, but an **unstamped checksum (stored `0x0` ≠
  computed `0x345e7bcf`)** and a populated key+count a prior title-図鑑 open wrote.
  Retail's `FUN_004901c2` **gates its per-bank verify sweep on `DAT_095d3728`** (set on
  save-load, `FUN_004902fe`) ⇒ it does NOT re-validate loaded banks ⇒ **retail output ==
  seed byte-for-byte (0 diffs) for all 99 non-active banks.** The port's
  `save_bank_init_all` **ignored the gate** and always swept ⇒ the stale-checksum banks
  failed `save_bank_checksum_ok` ⇒ `save_bank_init_one` re-inited them, zeroing the
  encyclopedia key+count + re-stamping the checksum (66 dwords/bank = key+count + cksum).
  Invisible to every pixel/frame pillar — the save pillar's raison d'être, demonstrated
  on its first real run. **Fix (commit pending): model the gate** — `g_save_bank_skip_verify`
  in `save_bank.c` (retail `DAT_095d3728`), gate the sweep on it, set it in
  `save_io_try_load`'s load buckets (reset by `save_bank_arena_clear`), + rename the
  state-map region `ranking_records`→`encyclopedia_discovery`. **VERIFIED `--target
  openrecet` re-drive: banks 1–99 `port^seed = 0`, `port^retail = 0` (all byte-identical);
  save diff `6836 → 6` bytes** — the 6435 ranking + 394 banks-1–99-checksum diffs GONE.
  +2 host tests (`save_bank_skip_verify_preserves_stale_bank`,
  `save_io_load_preserves_stale_checksum_nonactive_bank`); host 3432/0. No
  `PORT-DEBT(save-ranking-nonactive-banks)` needed (was never code-tagged).
- **✅ FIXED 2026-07-17 — `house_cam_flag` (0xb37d) was a SECOND real port bug** (the
  finding's original 6-byte residual mislabeled it "1 unmapped byte, non-logic" — it is
  NOT non-logic; it is a stable port=1/retail=0 divergence in BOTH drives). Bank dword
  `0xb37d` (`DAT_0451058c`) is a transient "returned to the house from the world map"
  camera-yaw flag: set 1 by the map→house return (`all.c:40769`), CLEARED to 0 on a
  mode-0 CONTINUE resume by the engine (`FUN_0049a59e all.c:100639-100642`, gated on the
  saved scene-mode `0xb381==0`). The port's `scene_post_fade_init` continue branch
  hardcoded INGAME but OMITTED the clear ⇒ a `{savefile}` taken right after a map→house
  return (this one carries a stale 1) re-commits 1 while retail commits 0. **Fix (commit
  `d686739`): `scene.c` continue branch reads `RESUME_MODE` (0xb381), zeroes
  `HOUSE_CAM_FLAG` (0xb37d) for mode 0.** Port doesn't read 0xb37d ⇒ render-neutral, pure
  save-state fix. Non-house resume modes 2/3/4/6 stay `PORT-DEBT(scene-resume-modes)`.
  Mapped both fields in `save_bank.h` + state-map. VERIFIED `--target openrecet`: byte
  186628 port 1→0 == retail; save diff 6→5. The save pillar's SECOND catch, invisible to
  every frame pillar (after the encyclopedia one).
- **✅ FIXED 2026-07-17 — `occupied_playtime` normalized by the bilateral `{playtimepin}`
  (★NEXT b LANDED; see §"★NEXT(b) LANDED" below).** It is the total-playtime frame
  accumulator; the port's value swings ~4000 frames run-to-run (the two async-load
  brackets it counts are a wall-clock CreateThread race), so it needed a determinism +
  origin pin, not just a phase constant. Bilateral pin at SAVE_PICKER_READY ⇒ byte-exact,
  drive-stable. Its 3-byte checksum echo resolves with the content.

## Full proof bundle LANDED (2026-07-16) — identity PASS · save FAIL, the near-PASS

★ NEXT (a) done. `house-pause-save-commit` carries a `proof:` block (schema_v2, join
`SAVE_PICKER_READY#1 [1,19]`, `required_pillars:[identity,save]`) → a content-addressed
bundle:

    parity_save.py  house-pause-save-commit --window 0:200        # deposit save-metrics.json
    parity_prove.py house-pause-save-commit --window 0:200 \
        --env-json docs/reference/parity-host-environment.json --json

**Verdict: FAIL (exit 1) · identity PASS · save FAIL** @ `bank0/occupied_playtime`
(this drive: port `0x41ee`/16878 vs retail `0x73f3`/29683). **Stable identity =
`contract_sha256 77e8e3f4…`** (the committed proof block) **+ this verdict** — regenerate
the bundle with `parity_prove … --window 0:200 --env-json docs/reference/parity-host-environment.json`.
**The proof_id is NOT a durable constant and MUST NOT be hard-cited:** it binds
`subject.port.git_commit` + PE + the drive's save hashes (EP-02 provenance), so it advances
every commit AND every drive (the port `occupied_playtime` is drive-variable — ★NEXT b; and
before commit `30243d3` a `.pyc`-polluted comparator hash also drifted it — now fixed, see
§Tooling). PORTABLE (abs paths ONLY in the non-hashed `envelope.local_paths`; hashed content
refs artifacts by sha256). render_program resolved
PASS (non-required bonus: aligned draw programs across the picker window, even though the
same-side pixel replay is REPLAY_DIVERGENT for the pause overlay). The 6-byte save
residual = the phase-origin near-PASS (recorded as the contract's R3 save exception):
occupied_playtime (phase) + its checksum echo + 1 unmapped byte, all bank-0, all
non-logic — the save-pillar analogue of arrprobe's honest render/pixels FAIL.

**★ GOTCHA — arm the v3 window at `--anchor SAVE_PICKER_READY`, NOT the default
`HOUSE_FREEROAM`.** The scenario re-anchors the commit inputs on SAVE_PICKER_READY (its
`{caprange}` sits right after `{wait SAVE_PICKER_READY}`). Driven with the HOUSE_FREEROAM
default, `--window 0:200` DESYNCS under load-stretch — the fast port reaches
SAVE_PICKER_READY while slow retail is still at PAUSE_READY ⇒ 0 shared anchors ⇒ 0 pairs
(identity PARTIAL). Arming at SAVE_PICKER_READY captures the picker on BOTH sides ⇒ **19
gap-free pairs** (`port#0==retail#1` at +1443 absolute, load-stretch-immune). The Jun-14
`win-0-200` was the wrong arm anchor (0 pairs); a `--force` re-drive at SAVE_PICKER_READY
fixed it. The scenario.yaml contract comment records the correct invocation.

**Lead — commit-region identity divergence.** Only the resting-picker frames [1,19] join;
offset 20+ (overwrite dialog + 60f commit anim) fall into gaps — the PORT stays on
SAVE_PICKER_READY while RETAIL re-anchors to **PAUSE_OPEN** during the disk write (180
PAUSE_OPEN retail-only frames). The save pillar is scenario-scoped (one committed
save.dat) so it proves the persistent OUTCOME regardless; but the anchor divergence during
commit is a real lead (retail re-opens/refreshes the pause layer on commit? a port anchor
def is missing?). Separate arc.

**Committed canonical env-json** (`docs/reference/parity-host-environment.json`): the 8
operator-attested EP-02/HOLE-3 fields (values match arrprobe's M0 bundle) so proof_ids
reproduce across bundles from the same host. No proprietary bytes.

**Stale-test fix (parity suite was RED since `6c9c85d`).** The catch-fix renamed the
state-map region `ranking_records`→`encyclopedia_discovery` (dword 40566) but left
`test_parity_save.py` asserting the old name ⇒ `test_summary_collapse` IndexError. The
pre-commit hook runs C host tests, NOT these Python suites, so it slipped through. Renamed
the 2 test refs + a stale `save_producer.py` comment. Parity suite green: save 41 · prove
51 · pixels 24 · observations 60 · fingerprint 52; `test_parity_schema` now auto-validates
2 opted-in contracts (arrprobe + this one — an automatic contract-schema regression guard).

NB **no `docs/parity-proof-index.json` entry** — that RUNTIME-axis index advances a VA
only on a PASS; this is a FAIL bundle (identity PASS ≠ a VA-coverage claim). The
save-commit VAs (`FUN_004905a8`/…) reach `scenario-pillar-proven` when the playtime
`{phasepin}` (next) flips save to PASS.

## Tooling — proof_id reproducibility (the git_commit binding + a bytecode-hash fix)

Compiling this bundle surfaced how the proof_id moves, and one real bug:

- **By design the proof_id binds `subject.port.git_commit`** (EP-02 provenance,
  `fingerprint.port_subject`). So it advances on EVERY commit — a specific id can never be
  self-cited inside the commit that produces it (naming it lands in the next commit, whose
  HEAD differs). **Cite `contract_sha256` (stable, in the committed scenario.yaml) + the
  verdict; regenerate the proof_id locally.** It stays deterministic given a checked-out
  commit + build + window.
- **It also binds the drive's save hashes** ⇒ a re-drive → a new id whenever the save
  differs; here the port `occupied_playtime` is drive-variable (★NEXT b), so this bundle's
  id is drive-specific too.
- **BUG FIXED (commit `30243d3`): `comparator_sha256` hashed `__pycache__/*.pyc`.**
  `dir_manifest_sha256(tools/parity)` walked every file, so 12 of 24 manifest entries were
  compiled bytecode — interpreter-version- and source-mtime-dependent, often absent ⇒ the
  comparator (hence the proof_id) drifted on any import/recompile even at a fixed commit,
  breaking "reproducible from a given commit". `dir_manifest_entries` now prunes
  `__pycache__` + skips `.pyc/.pyo` (source-only, verified stable across re-import); +2
  fingerprint checks. Affects every bundle (arrprobe's too).

## Follow-ups (roadmap)

- ✅ **DONE 2026-07-16** — the full `parity_prove` bundle (`contract_sha256 77e8e3f4…`;
  see §"Full proof bundle LANDED"). The save FAIL now lands inside a content-addressed
  bundle, parallel to the pixels-producer arrprobe bundle.
- **✅ ★NEXT(b) LANDED 2026-07-17 — bilateral `{playtimepin}` → save PASS.** See the
  dedicated section below.
- **ST-02** (canonical encoder + Merkle roots over the state tree), **ST-03**
  (expand retail+port state capture → the `state` pillar for the volatile class),
  **ST-04** (the first-divergence state report reusing `state_map.locate`).

## ★NEXT(b) LANDED 2026-07-17 — save PASS, the first fully-passing multi-pillar bundle

`house-pause-save-commit` proves **identity PASS · save PASS · 0 divergences**
(`parity_prove … --window 0:200 --anchor SAVE_PICKER_READY`, verdict PASS,
`contract_sha256 9c2d2755…`; render_program PASS as a non-required bonus). Two
divergences were resolved en route (both invisible to every frame pillar — the save
pillar's raison d'être): **house_cam_flag (0xb37d)** — a real port bug, §Leads above,
commit `d686739`; and **occupied_playtime** — a drive-variable phase origin, below.

### The playtime variance — EXPLAINED (the ★NEXT-b prerequisite)

Playtime (`SAVE_BANK_FIELD_PLAYTIME`, working `DAT_044e37a0[slot]`) ticks +1 every
live-scene frame at the HEAD of `FUN_004536cb` (`sim.c:308`, `if g_scene_state!=0`),
**BEFORE the worker-load gate** — so it counts LOAD frames. The port reaches the save
COMMIT at a *load-race-variable* engine frame, so playtime is variable:

| bracket (LOADING_START→END) | port A | port B | retail A | retail B |
|---|---|---|---|---|
| house load  | 3865 | 1478 | 14259 | 14307 |
| pause load  | 2870 | 1229 | 1213  | 1205  |

House-load Δ (3865−1478=2387) + pause-load Δ (2870−1229=1641) = **4028 = the observed
playtime Δ4028** (port 20906↔16878). The two brackets are completion-based async worker
loads (`worker_load_spawn`, `scene.c:176`, spawned AFTER the INGAME flip at `scene.c:114`)
whose duration is a wall-clock **CreateThread race** under turbo — the same non-determinism
`{csloadpin}`/`{primaryloadpin}`/`{tutloadpin}` bind elsewhere. Retail's loads are the
**deterministic intro-video cadence** (Δ48/Δ8 → stable ~29643; the ~14259-frame house
"load" IS the intro/prologue the port skips). Both sides use IDENTICAL tick logic:
`playtime = engine_frame_at_commit + 13598` on BOTH (port A 20906−7308; retail A 29643−16045;
savefile base 13855). The port is lower + variable purely because it commits at an earlier,
race-variable engine frame — a phase origin + a determinism gap, NOT a logic gap.

### The fix — bilateral `{playtimepin}` (commit `64d2e3f`)

A new pin op `{playtimepin:[F,V]}` (mirrors `{gsimpin}`): force the active working slot's
playtime accumulator to a canonical origin V at **SAVE_PICKER_READY** — the first anchor
PAST both variable loads (pinning at LOADING_END would re-diverge over the pause load).
Both the port (`input_poll`) and the Frida agent (`input_poll.onLeave`) fire it **pre-sim**,
ahead of the playtime tick, so both land on **V+K** after the SAME K=59 input-driven ticks
to the commit snapshot — **no off-by-one**. Unlike `{gsimpin}` (V = retail's natural, a
retail no-op), it is FORWARDED to the agent (`frida_capture.py`) so BOTH sides are pinned:
retail's natural swings run-to-run (29643/29683/**29830** observed), so a port-only pin
can't match it. V=29628 (retail-representative) ⇒ committed 29687.

VERIFIED `--target both` ×2: save.dat **byte-identical (ndiff 0)**, committed playtime
**29687 on both sides, drive-stable**. `parity_prove` verdict **PASS**. Dropped the
contract's save exception (`scenario.yaml`). +2 host tests (parse/fire + reject-scalar);
host 3434/0. Tooling: `src/input_segtrace.{c,h}` (parse/push/callback/rearm/fire),
`src/main.c` (`segtrace_playtimepin_cb`), `tools/frida/openrecet-agent.js` (bilateral apply
at `DAT_044e37a0[slot]`), `tools/frida_capture.py` (forward to agent).

**Doctrine note:** playtime-at-commit is a phase/environmental origin (load-timing), NOT
the pure-function logic contract — the tick is confirmed 1:1. Pinning it bilaterally is the
`{rngseed}`/`{phasepin}` pattern (force both sides to a canonical origin), legitimate
normalization; the proof reads "given a normalized playtime origin, the two committed
save.dat are byte-identical."

**★ NEXT (b′) — the runtime-axis index entry (deferred, deliberately).** This is now the
first PASS bundle eligible for a `docs/parity-proof-index.json` entry (the save-commit VA
`FUN_004905a8` → `scenario-pillar-proven`, the first non-empty runtime rung — it would flip
STATUS off "0% runtime-proven"). Held back one step because the index schema requires a
`proof_id`, which BINDS `git_commit` and so **cannot be self-cited inside the commit that
adds it** (a HEAD regen yields a different id — the exact volatility §Tooling warns about).
The first runtime entry should settle the convention first: key the durable binding on the
stable `contract_sha256` (9c2d2755…) + `va` + `scope`, treating `proof_id` as a
recording-time snapshot (or a schema tweak). A precedent-setting ledger change — do it
deliberately, not as a rushed capstone.
