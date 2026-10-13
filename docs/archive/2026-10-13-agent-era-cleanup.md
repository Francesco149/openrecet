# 2026-10-13 agent-era cleanup — index of removed / reverted work

Owner-approved cleanup of Gemini-era (2026-08-22..23) and GPT-era (2026-07-16..17) work
judged low-value or fabricated in `../audits/2026-10-13-methodology-reset.md` §1.2–1.3.
Git history is the archive: recover a file with `git show <cleanup>^:<path>`, or a whole
change with `git revert <cleanup>`. Format: origin → what → cleanup commit.

## Gemini era

- `375205f2` → day-2 "autonomous playthrough" (`tests/test_day2_playthrough.c`, `tools/test_day2_autonomous.py`; tests asserted back the fields they wrote) → reverted `b20e27ae`
- `61b0e7f1` → mislabeled 14k-line bundle: behavior atlas (`tools/atlas/`, `behavior_atlas.py`), call capsules + synthetic fixtures (`tools/parity/capsule*.py`, `docs/schemas/fixtures/capsules/`), `ExportGhidraIndex.java`, `retail_fields.json` 0x48526d/0x476320 entries, `diff_entry` ABI shims, `standing-conventions.md`, `main.c` d3d8-from-cwd, proxy staging into `vendor/original` → reverted `7a069cdb` (no part kept; its CLAUDE.md lines left for the lead)
- `7e23e209` → CC-04 unknown-write capture (capsule capture, agent hooks, fixture); house-firstcust-cutscene-day2-full calltrace/caprange restored to the [0,16500) set by `6ee68a3d` → reverted `5d783d98`
- `9163f81a` → BT-00 boundary events (`tools/parity/boundary.py`, `docs/reference/boundary-events.md`, schema, test) → reverted `7d64fa33`
- `3c7a0ef3` → "all 5 customer kind machines" (invented `cs_kind_select_buy`, missing RNG branches/gates; retired `PORT-DEBT(cs-other-kinds)`) → reverted (quarantined, to re-port under rhost) `238b8f36`
- `0ed53a76` → coverage atlas (`tools/coverage_atlas.py` + test, Stalker plumbing, scenario coverage_expectations) → reverted `4524d610`
- `d07dbbfe` (re_index half), `62ec2d83`, `0fda8b17`, `f2b39b22` → `tools/re_index.py` + `tools/test_re_index.py` (DB built in fallback mode, 0 call edges) → removed `4524d610`; `d07dbbfe`'s FUN_00461303 port kept
- `9a41212e` → FUN_00460b93 equip upgrade kept; request-vs-equip slot bug, missing DAT_005c6c34 hint path and b5a4 restore fixed → `f19aa4d7`
- `39cc8dd7` → alleged owner 1:1 confirmation (tutorial skip / first customer / first haggle) kept but marked UNVERIFIED; whole confirmed-parity-ledger marked UNVERIFIED → `1101f691`
- `3c4bf6eb`, `17ef89de`, `3e7b9c7a` → viewer: state-panel filter + "diffs only" restored, string-binding edits and replay `SetRenderTarget` guard dropped; SEH logger, gap guard, device-lost reset kept → `98cf974b`

## GPT era (all removed in `9625666f`)

- `57345027` → parity vocabulary, proof/contract schemas + fixtures (`docs/reference/parity-{vocabulary,proof-format}.md`, `docs/schemas/`, `tools/test_parity_schema.py`)
- `213dd017` → provenance fingerprinting (`tools/parity/{fingerprint,environment,canonical}.py`, `tools/test_parity_fingerprint.py`, `docs/reference/parity-host-environment.json`)
- `8d27df2c` → observation adapters (`tools/parity/{observations,pixels,render_program}.py`, `tools/test_parity_observations.py`)
- `b4c4e2c8`, `06365856` → proof compiler `tools/parity_prove.py`, `tools/parity/prove.py`, `tools/test_parity_prove.py`, `docs/findings/parity-M0-adversarial-review.md`
- `2f9bad71`, `8053c1c1` → `docs/parity-proof-index.json` runtime binding + `docs/findings/parity-EP06-ledger-lifecycle.md` (ledger kept, runtime axis now 0)
- `74517165` → `docs/findings/parity-EP08-cache-provenance.md` (EP-08 cache code in `v3cache.py` kept); `62ece6e5` view.json container sha256 dropped
- `8514b9d9` → pixels producer `tools/parity_pixels.py`, `tools/parity/pixel_producer.py`, test, `docs/findings/parity-pixels-producer.md` (`replay.exe --render-dump` kept)
- `fe4101fe`, `33f706b1`, `2f2ca164` → save pillar adapter/CLI (`tools/parity/{save,save_producer,state_map}.py`, `tools/parity_save.py`, `tools/test_parity_save.py`, `docs/reference/canonical-state.md`); comparator extracted to `tools/save_compare.py`, map moved to `tools/save_arena_map.json`
- `efaf2e74`, `3c98a73a`, `7712a298` → state pillar (`tools/parity/state*.py`, `tools/parity_state.py`, `tools/test_parity_state.py`, `docs/findings/parity-state-producer.md`)
- `47abaf3d` → `tools/state_diff.py`, `tools/parity/state_diff.py`, `tools/test_state_diff.py` (the older `tools/state_diff/` oracle kept)
- `878759b1` → ST-05 mutation consumer (`tools/parity/state_mutation.py`, `docs/reference/state-mutations.md`, `tools/test_state_mutation.py`)
- `e941d755` → EP-07 human-review bridge (`tools/parity_review.py`, `docs/findings/parity-EP07-human-review.md`)
- `5120a250`, `cac0840d`, `95495a21`, `4533b7e3` → D3D8 census checker (`tools/d3d_census.py`, `tools/parity/d3d_census.py`, test, `docs/findings/gx00-d3d-method-census.md`, view/slice/cache census carry); proxy census counters kept
- `2cd74019`, `fb7332a0` → `docs/findings/gx03-resource-versions.md` (GX-04 VB/IB wrapping + gx04 fixture kept)
- `be1ac04f` → GX-05 fixture (`proxy/gx05_fixture.c`, `test_gx05_fixture.py`); reader fuzz `replay/corrupt_fuzz.c` + `test_corrupt_reader.py` kept
- `b1ae8dec`, `82591adf`, `f6d47b2a`, `ef980762` → GX-06 corpus (`tools/gx_corpus.py`, `tools/parity/gx_corpus.py`, test, `docs/parity-graphics-corpus.json`, `gx06_*` fixtures, `docs/findings/gx06-graphics-corpus.md`); reader hardening in `orv3*.py` kept
- `6f978f18` → `docs/plans/parity-evidence-roadmap.md`, `tools/ci/check_docs.py` + test + pre-commit/nightly gate (incl. the CI ban on model names in docs); `tools/run_python_tests.py` kept
- `06365856`, `2f2ca164` → `proof:` / `coverage_expectations:` / `schema_version` blocks in `house-firstcust-arrprobe` and `house-pause-save-commit` scenario.yaml
