# GX-06 — graphics-capture regression corpus (LANDED)

> **Status:** LANDED 2026-07-17 (roadmap `../plans/parity-evidence-roadmap.md` §9 GX-06;
> depends GX-02✓[vacuous]/GX-04✓). Manifest `../parity-graphics-corpus.json`; core
> `../../tools/parity/gx_corpus.py`; CLI `../../tools/gx_corpus.py`; gate test
> `../../tools/test_gx_corpus.py` (6 checks). The capstone of the GX arc: GX-00→GX-05 proved
> the D3D8 capture is COMPLETE (every render-affecting call recorded); GX-06 proves the
> record→REPLAY path for every recorded opcode is itself correct AND regression-guarded.

## Why

Capture-completeness (GX-00 census, GX-01 precondition) proves nothing was silently
FORWARDED. It does NOT prove the recorded opcodes REPLAY faithfully. A `pixels`/`render_program`
PASS reconstructs the frame from the captured stream, so a bug in the recorder OR the replayer
for any opcode would corrupt the reconstruction. GX-06 closes that with a corpus that proves
each opcode's record→replay path two ways, fail-closed, so it can never silently regress.

## Coverage unit + the two axes

**Unit = the container OPCODE** (`orv3.OPNAME` — the concrete replay primitive), each tied to
the census recorded method(s) it captures (`opcode_methods`, drift-guarded). 25 recorded
opcodes (every `OPNAME` except `EOF`) + the 4 SURFREF kinds. Two axes per opcode:

- **FIXTURE** — a synthetic standalone D3D8 exe that exercises the opcode in a controlled frame
  and replays BIT-EXACT (`replay <cap> <v3ref_000.raw> 0` → 0 differing bytes). Proves the
  plumbing in ISOLATION.
- **REAL PROOF** — a real cached scenario whose container CONTAINS the opcode and passes
  `v3verify` bit-exact (`REPLAY_EXACT`, every kept frame's re-render == the proxy's reference
  hash). Proves it IN SITU.

**Fail-closed gate (`gx_corpus.build_report`):** EVERY recorded opcode needs a fixture (else its
replay path is unexercised); every OBSERVED opcode (present in ≥1 bit-exact real proof) needs a
real proof too; a supported-but-UNOBSERVED opcode is fixture-only + recorded honestly. Plus
opcode↔census drift both ways (every census-recorded method captured by ≥1 opcode/SURFREF; every
mapped method actually recorded). The opcode universe is imported from `orv3.OPNAME`, so a new
GX-02 opcode auto-enters and gates uncovered until the corpus adds a fixture/proof.

## The sweep — 3 recorded opcodes are UNOBSERVED

Swept all **134 cached containers** (32 scenario names, both sides, ~14.6 GB): **DrawPrimitive
(18), DrawIndexedPrimitiveUP (21), CopyRects (29) appear in 0 of 134**. The engine's captured
stream uses only `DrawIndexedPrimitive` + `DrawPrimitiveUP`, and renders the pause backdrop via
`SetRenderTarget` re-render (NOT a `CopyRects` screen-capture). So these 3 are **fixture-only**
in the corpus — proving the replay path works — with no real proof required (nothing emits them;
recorded honestly in the manifest `unobserved_note`). If a future scene emits one, `--verify`
surfaces it as newly-observed and it then requires a real proof.

## The corpus (all bit-exact)

**Fixtures** (`proxy/gx06_{sink,rt}_fixture.c`, built on demand; `test_gx06_*_fixture.py`):
- `gx06_sink` — one lit+textured+transformed VB/IB quad + pretransformed UP tris → **all 22
  NON-RT recorded opcodes** in one frame. Makes Transform/Material/Light load-bearing. 0-diff.
- `gx06_rt` — render-to-texture → composite → `CopyRects` a corner onto the backbuffer →
  **RES_RT_TEX, SetRenderTarget, CopyRects + all 4 SURFREF kinds** (NULL/BACKBUFFER/DEPTH/TEX).
  0-diff. (CopyRects's only capture anywhere.)
- `gx04`/`gx05` — VB same-frame mutation SPLIT/DEDUP + forced-collision byte-compare (a DEEPER
  property than presence; bonus RES_VB coverage).

**Real proofs** (`--verify` re-measured, `verify_counts`):
| scenario | regime | frames | opcodes |
|---|---|---|---|
| title-encyclopedia | 2D title | **120/120** REPLAY_EXACT | 11 |
| house-firstcust-arrprobe | HOUSE 3D (the M0 scene) | **1500/1500** REPLAY_EXACT | 20 |
| house-pause (f1bf56e7) | render target | **240/240** REPLAY_EXACT | 22 + 4 SURFREF |

Together: **25 opcodes — 22 observed (all bit-exact-proven) + 3 supported-unobserved
(fixture-only) — and all 4 SURFREF kinds. Gate: COMPLETE.**

## The gate — fast + verify

- **FAST (default, host suite):** reasons over the committed manifest attestations + the census,
  touching NO caches and NO replay.exe ⇒ runs on any checkout. Coverage math + drift → COMPLETE
  / GAPS. `test_gx_corpus.py` proves the shipped corpus COMPLETE and that each failure mode
  (no_fixture, observed_unproven, opcode/method drift ×4, schema guard) fails closed.
- **`--verify` (drive-capable):** re-parses each real-proof container (opcodes/SURFREFs) + re-runs
  `v3verify` (bit-exact) + runs the 4 fixture tests, RECONCILING against the manifest; `--write`
  re-STAMPS it so an attestation can't silently rot after a cache re-drive re-keys a scenario dir.
  VALIDATED e2e: all 3 proofs + 4 fixtures match reality.

## GX-05 residual CLOSED (diagnostic reader corruption-safety)

Root assessment: the diagnostic re-walkers `orv3_xform`/`orv3_rt` only ever walk a container that
`Container.load` (GX-05-hardened) already VALIDATED, and `orv3_state` is NOT a container reader —
so the corrupt-INPUT path was already closed by construction. Added defense-in-depth
(`orv3.checked_reader` — shared bounds-checked u32/i32/span → clean ValueError) into their raw
re-walks (+ the `_f16` matrix and CopyRects rects/points spans), so a re-walk DESYNC fails with a
clear message, never a raw `struct.error`. Verified no regression on the real pause RT frames.

## Acceptance MET

Roadmap GX-06: *"every observed render-affecting method has at least one fixture and one real
scenario proof."* MET — 22 observed opcodes each have ≥1 fixture AND ≥1 bit-exact real proof; the
3 unobserved have fixtures; all 4 SURFREF kinds covered; the whole set is a fail-closed,
drift-guarded, self-refreshing gate. The GX arc (capture completeness → replay correctness) is
COMPLETE.

## Tooling

`tools/gx_corpus.py` (CLI: default fast gate / `--verify [--write]` / `--json`) ·
`tools/parity/gx_corpus.py` (pure core) · `docs/parity-graphics-corpus.json` (manifest) ·
`tools/test_gx_corpus.py` (6-check gate test) · fixtures `tools/trace_studio_v3/proxy/gx06_*.c`
+ `test_gx06_*_fixture.py` + `gx06_fixture_common.py` · `orv3.Container.opcode_counts()` /
`surfref_counts()` / `checked_reader()`.
