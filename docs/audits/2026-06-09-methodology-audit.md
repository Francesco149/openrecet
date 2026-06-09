# Deep audit — methodology, tooling, direction (2026-06-09)

> Commissioned by the user; authored by Fable 5 with five fan-out recon agents over the
> whole repo (tools inventory, src/build/FP, scenarios/CI/velocity, coverage scale, retail
> exe forensics). **Purpose: settle the strategic questions durably so future sessions
> (Opus) get maximum mileage without re-deriving or re-litigating them.** Treat §1 and §7
> as decision records: do not reopen without *new evidence*, and if you do, update this
> file. The ranked tooling roadmap is §3; the pinning upgrades §4; the Opus working
> agreement §6. Facts snapshot (dated) in §8.

## 0. TL;DR verdicts

| Question | Verdict |
|---|---|
| Is behavioral TAS-trace parity the right methodology vs byte-exact decomp? | **Yes — keep. Byte-exact is strictly additive cost here** (§1). The per-function Frida oracle layer already recovers the per-function proof byte-matching would buy. |
| Is the overall approach efficient? | **Yes.** ~46% of recent effort went to tooling; that was the right front-load and it is now mostly built. The remaining wins are **glue, not platforms** (§3). Expect the port-share of effort to rise from here. |
| Better ways to do RNG/phase pinning? | Mechanism is sound and proven. Upgrade it from **reactive to exhaustive** (phase-state census, §4.2) and from **prose-rules to default tool behavior** (lint + auto-pin, §3 T2). |
| Tracing/divergence tooling improvements? | T1–T12 ranked in §3. Highest leverage: one-command `triage` (T1), trace lint + capture-time auto-pin (T2), phase census (T3), state-checksum anchors + save-equality oracle (T4), executed-but-unported census (T5). |
| Direction/sequencing? | Frame-0-forward along the player path is right. Add a **milestone replay ladder** with an outcome KPI (first-divergent-frame), §5. |

The single most important meta-observation: this project's competitive advantage is the
**multi-pillar divergence-attribution doctrine** (logic vs phase vs RNG vs upstream
inputs, normalize-then-compare). It converts "fuzzy visual diff" into a decidable
procedure. Every tooling recommendation below exists to make that doctrine *cheaper to
execute*, not to replace it.

---

## 1. Decision record: behavioral parity, not byte-exact

### 1.1 What the retail binary is (forensics, 2026-06-09)

- PE linker version **6.0** → **Visual C++ 6** era toolchain (VS2003 would stamp 7.1).
  TimeDateStamp 2010-10-07 = the Carpe Fulgur Steam build's pack date, not compile date.
- **Static CRT** (no msvcr*.dll imports; "Microsoft Visual C++ Runtime Library" banner),
  no Rich header surviving in the unpacked image.
- **x87 FPU throughout** (fldz/fld1 inline; `__ftol` truncation pattern; the
  MSVC-classic LCG 0x343fd/0x269ec3 in `rand()`).
- `.text` ≈ **1.1 MB**, 2,548 non-thunk functions (functions.csv), ~1.04 MB summed
  function bytes.

So byte-matching is *technically feasible* — VC6 is the best-trodden MSVC for matching
decomp (decomp.me supports it; the LEGO Island `isle`/`reccmp` ecosystem proves the PE
workflow) — and we should be honest that it offers a real property: per-function
mechanical proof with no behavioral test needed.

### 1.2 Why we still don't want it

1. **The shipped artifact is mingw-built.** A byte-match proves `VC6(source) == retail`.
   The port ships `mingw(source)`, which re-introduces every codegen difference the
   byte-match was supposed to eliminate. You'd need the behavioral harness *anyway* to
   verify the actual deliverable — so byte-matching is additive cost (realistically ~2×
   per function: fight register allocation and codegen idioms, forbid restructuring),
   not an alternative cost.
2. **It forbids the port's architecture.** Semantic names, modular files, host-testable
   pure functions, ASan/UBSan on 3,200 host tests — all of that exists because the source
   is *not* shackled to the binary layout. That architecture is what lets Sonnet-class
   agents port chips safely.
3. **The verification value is already substituted at the semantic level.** The
   `diff_test.py` oracle calls the *real retail function* via Frida with injected
   args/globals and bit-compares against the port compiled to a host .so. That is
   per-function proof of the contract we actually care about (same inputs ⇒ same
   outputs), minus the compiler archaeology. The flow-trace (`CALL_TRACE_*` +
   `retail_fields.json` + `flow_diff`) extends the same proof to stateful per-frame code.
4. **Empirically the behavioral stack catches byte-level bug classes.** It has caught a
   wrong `.rdata` constant at 1 LSB (−128.0 pulse scale, `a4da502`), cos-vs-sin
   (`§103`), a COLOROP enum mislabel (`§104`), a denormal constant (`§97`), and exact
   bit-pattern float constants (`0x3b712c27`). It *also* catches what byte-matching
   structurally cannot: cross-function draw order, batching shape, inherited device
   state leaks, anchor/event structure.
5. **Compiler identification still pays — for reading, not building.** Knowing it's VC6
   explains decompile idioms (`__ftol`, FPU-stack args Ghidra drops, CRT intrinsics).
   Optional spot tool: paste a confusing function into decomp.me (VC6) to sanity-check a
   reading. Zero infrastructure commitment; never a gate.

### 1.3 The FP determinism contract (why behavioral 1:1 is even possible)

Both sides execute on **x87**: VC6 statically-linked CRT on retail; mingw i686 (no
`-mfpmath=sse`, no `-ffast-math`, `-O2`) on the port. Transcendentals reduce to the same
hardware `fsin`/`fcos`; truncation via `(int)` cast matches `__ftol`. This is a **load-
bearing invariant** — protect it:

- **Never** move the port to x86_64, `-mfpmath=sse`, or any `-ffast-math`-family flag
  before full parity. Portability stays Phase 6.
- **Residual risk class:** x87 80-bit *intermediate spill points* differ between VC6 and
  gcc codegen, so a long chained float expression can round the last bit differently.
  Defense so far: oracles + 1-LSB pixel checks keep passing, and constants are
  objdump-recovered as bit patterns. **Policy when it bites:** restructure the local
  expression to match retail's store points (the asm shows where it spills) — do NOT
  reach for global `-ffloat-store`, which would perturb already-validated functions.
- **New guard (cheap, recommended):** a CI/pre-commit check that disassembles
  `build/openrecet.exe` and fails on SSE arithmetic opcodes (`addss/mulss/divss/
  cvttss2si/...`) so a toolchain bump can't silently flip the FP model. (§3 T11)

### 1.4 Verdict

**Behavioral-first with per-function semantic oracles is the correct and cheaper path,
and it is the only one that verifies the artifact we ship.** TAS traces are not the weak
substitute for byte-exactness — combined with the attribution doctrine and the oracle
layer they are a *stronger* proof for a *reimplementation*. Closed. (Risks of the
approach and their mitigations: §2.)

---

## 2. Risk register of trace-based verification (and mitigations)

The honest weaknesses of behavioral verification, each with its standing mitigation.
None require a methodology change; all are tooling/policy items cross-referenced to §3.

- **R1 — Path-coverage tail.** Traces prove only exercised paths; 34 scenarios today
  cover title/intro/house/world-map only. *Mitigations:* scenario-per-arc policy
  (already de facto); **random-walk differential traces** (T6) to sweep input space
  cheaply; **diff-target-per-chip policy** (raise from today's 4 oracle targets — every
  pure-ish leaf chip should land with one); milestone ladder (§5).
- **R2 — Silent state drift.** A wrong-but-invisible field (gold, flag, inventory count)
  can survive thousands of frames before rendering. *Mitigations:* **state-checksum
  anchors** + **save-file byte-equality oracle** (T4) — the working save arena is THE
  game-state container and both sides can hash it; growing flow-trace field coverage
  (existing policy).
- **R3 — Overfit to the canonical seed/phase.** All pinned runs use 19937 + zeroed
  counters; a logic error could cancel at exactly that origin. *Mitigation:* periodic
  **multi-seed re-runs** (2–3 alternate seeds via the existing `{rngseed}` op) of
  milestone traces. Cheap; quarterly-ish or after big RNG-adjacent chips.
- **R4 — Harness-masked behavior.** TAS substitutes the input poll (`chain_benign`),
  turbo fixes the timestep, capture hooks shift timing. *Mitigations:* keep the benign
  registry discipline; one **realtime human-played smoke** per milestone; never compare
  an un-pinned capture against a pinned conclusion.
- **R5 — Upstream-input misattribution.** Already doctrine (fix frame 0 forward); keep.
- **R6 — Regression as ported surface grows.** Pre-commit host tests + ledger regen are
  enforced via `core.hooksPath`. CI cannot replay traces (no assets, no Windows host).
  *Mitigation:* a local **parity-sweep runner** over the confirmed-1:1 scenario set,
  run on the Windows host on a schedule or at arc boundaries (T11).
- **R7 — Decompile trap classes.** Ghidra drops FPU-stack args, "argless" trig, enum
  value-vs-name, 0-based Frida args, sim-counter pre/post-increment seam. These live
  scattered in memories/notes. *Mitigation:* promote to a single
  `docs/reference/decompile-gotchas.md` checklist cited from the chip workflow (T12).

---

## 3. Tooling roadmap — ranked (leverage ÷ effort)

Context for the ranking: the divergence loop today is capture → studio/pixel diff →
`flow_diff --verdict` → `--field-timeline` / `render_diff --explain` /
`d3d_state_at_draw` → fix → recapture. Every leg exists and is good; the cost is that
*Claude composes 3–5 commands and hand-reads text between legs*, and several
correctness rules live as CLAUDE.md prose instead of tool behavior. Evidence of where
time actually went (PROGRESS, 2 weeks): capture reliability, alignment footguns,
pin-placement mistakes, stale-golden confusion — exactly the class the items below kill.

**T1 — `triage`: one command from session → ranked divergence report.** (M effort, XL
payoff — the single biggest Opus-mileage item.) `trace_studio triage <session>` (or
`scenario-test --triage`) chains what is today manual: per-frame diff stats → first
real divergent ordinal (skipping known-benign regions) → `flow_diff --verdict` +
`--field-timeline` → `render_diff --explain` at the divergent frame → rng-callsite drill
if DESYNC → emit ONE JSON (`triage.json`: first_divergence{ordinal,abs_port,abs_retail},
pillar verdict per field, first divergent (call,field), suspect draw + state delta,
rng table, links to crops) + a 20-line human summary. Everything it calls exists;
this is pure glue. It also subsumes the cheatsheet's "three questions" into one entry
point a fresh session can run without remembering anything.

**T2 — Trace lint + capture-time auto-pin (policy → mechanism).** (S–M effort, L
payoff.) `trace_studio lint <trace>` validating the rules currently carried as CLAUDE.md
prose: pins present and placed in the segment that OPENS the `{caprange}` (after the
FIRST free-roam wait, not the last), exactly one `{rngseed}` at the anchor and it is
canonical 19937 (no stacked seeds), `{calltrace}` spans the `{caprange}`, pins within
trace bounds, `{savefile}` resolvable, settle-margin warning (<48f before caprange for
spring-lerp scenes). Wire as a preflight into `capture`/`recapture`/`apply` (warn or
fix, `--no-pin` to opt out), and make **capture insert the canonical pin block by
default when absent** — the standing policy ("every trace pinned + call-traced up
front") becomes something the tool does, CLAUDE.md's half-page recipe shrinks to one
line, and `apply --auto-pin`'s verdict-gated/last-wait limitations stop mattering.
Include the naming/zero-frame invariants here too: assert both sides' session frames are
ordinal-named with `base_abs` in the manifest, and fail loud if retail renumbering
produced 0 frames (today that can silently yield an empty diff).

**T3 — Phase-state census: make pinning exhaustive.** (M effort, L payoff, durable.)
Today every load-dependent free-runner is discovered by losing a divergence hunt to it
(db054, b154, rmb shake, bg-NPC warmup, sparkle %8, the still-open blink counter
`DAT_073a3e0c`, world-map entry timer `_DAT_09643628`). Replace whack-a-mole with a
census: run retail TWICE with deliberately different load-stretch (insert a dummy
`{wait}` delta), same inputs + same `{rngseed}`, snapshot **all of .data+.bss**
(~0.9 MB — trivial now that capture-local disk writes exist) at the SAME db054-aligned
anchor, diff → every differing address is phase-bearing or RNG-bearing state. Triage
the list once (most will be the known pins; the remainder is the complete future
pin/benign set), record in `findings/phase-state-census.md`, extend `{phasepin}`
accordingly. Repeat per major scene (HOUSE, world map, dungeon when it lands). Same
trick on the port (two runs with different artificial pre-delays) catches port-side
free-runners. This converts the #1 recurring time sink into a one-time sweep per scene.
**Pinned mode = pin-completeness regression test (the decisive feature, 2026-06-09):**
apply the canonical pin block at the anchor, snapshot one frame later on both
differently-stretched runs — a complete pin yields an EMPTY diff; any surviving byte
is a missing pin (or true non-determinism), named by address BEFORE it costs a
debugging session. Re-run after each arc / per new scene as the standing gate.
Discovery mode (unpinned) finds the set; pinned mode proves it stays closed. A
decompile sweep was considered for discovery and rejected: static candidate lists
are huge (reachability/indirection-lossy, scene-unreached counters, no visible-
consumer filter) while the census answers the actual runtime property directly;
decompile reading is the TRIAGE step for census hits (writers/readers of the few
differing addresses), not the discovery mechanism. Known limitation: heap/arena
state outside .data/.bss (rare so far; the save arena is covered by T4's hashes) and
state that only starts free-running after the anchor — mitigate by snapshotting at
several anchors along the milestone traces.

**T4 — State-checksum anchors + save-equality oracle.** (S–M effort, L payoff vs R2.)
(a) At every `{wait}` anchor frame, both sides emit `sha256(working save arena)` (+ a
few named sub-region hashes) into the call-trace; `flow_diff` compares and names the
first divergent anchor + region. Port side is trivial; retail side is a Frida read of
the same VA range. (b) New scenario assertion: after a scripted sequence ending in a
save, the **save file bytes are equal** port↔retail (under pins). That is a brutal
end-to-end integrator over all sim state that rendering never shows. Wire (b) into the
shop-roundtrip arc which is active right now — it's the natural first consumer.

**T5 — Executed-but-unported census per trace ("what's missing").** (M effort, M–L
payoff.) The freeroam survey's 77-fn work list proved the shape; make it a standing
report: enumerate retail functions executed in a capture window (TTD `batch_calls.js`
over the existing safe-VA list — or one-shot self-removing Frida hooks for cheap
breadth), set-difference against `port-ledger.json`, rank by `calls × size`, annotate
ledger status. Output lands in `triage.json` (T1) and directly feeds "what to port
next". Re-run per new arc/scene; it is the coverage-driven complement to the visual
diff (it sees logic that never draws).

**T6 — Random-walk differential scenarios.** (S effort once T4 exists.) A generator
emits seeded pseudo-random input traces (bounded to a room; N seeds × M frames) run
`--target both` with pins, asserting per-anchor state hashes — no goldens, no human
eyeballing. Massively broadens R1 path coverage for free; nightly-able on the Windows
host. Start with HOUSE free-roam (movement/collision/menus), extend per scene.

**T7 — `--json` everywhere + machine-readable verdicts.** (S effort; prerequisite of
T1.) `flow_diff`, `render_diff`, `d3d_state_diff` gain `--json`; the session manifest
already carries the verdict — keep that the single source. Exit codes stay as they are
(flow_diff's 0/1/2 contract is good). Rationale: Opus burns context parsing prose
tables; JSON + a stable schema compounds across every future session.

**T8 — Capture lake manifest (stop re-driving retail).** (M effort, M payoff.) Append
one line per capture to `runs/manifest.jsonl`: scenario/session, trace content hash,
caprange, pins, which traces exist (call/d3d/verts/frames), target, path, date. A
`find-capture` query answers "do we already have ground truth for X?" before any
re-capture; retail captures become reusable across sessions keyed by (trace-hash,
window). Retail drives are the slowest, flakiest leg — every avoided one is minutes
saved and one less singleton-mutex hazard.

**T9 — Vivisection mode (reserve, for the hardest chips).** (L effort per use.) For
gnarly stateful subsystems (event VM, combat tick), compile the chip's TU against a
per-chip shim header mapping its globals to REAL retail VAs (`#define g_foo
(*(int*)0x450f470)`), build as DLL, inject into retail via Frida,
`Interceptor.replace` the retail function with the port implementation, replay a pinned
trace → a bit-identical run proves the chip **in vivo against true retail upstream
state**, eliminating all harness/alignment doubt. cdecl/stdcall + x87 both sides makes
this tractable; the globals layer being per-file (not centralized) means the shim is
per-chip manual work — which is why this is a scalpel, not the default. Use when a
chip's oracle is too hard to drive and its trace verification keeps being confounded.

**T10 — Audio event diff default-on.** (S effort.) Both sides already emit
`audio.jsonl` (BGM swap / SE id / fade events). Diff them by default in
`--target both` runs and surface in triage — frames don't capture audio regressions,
and this is nearly free. (Defer PCM diff per harness-roadmap Phase C until a real bug
demands it.)

**T11 — CI/guards.** (S effort.) Keep nightly lean (build + no-proprietary-bytes).
Add: the **FP-model guard** from §1.3; the stub-count baseline check if not already
gated; optionally `render_trace_gate.py` if it can run asset-free. The real regression
net stays local (R6 parity sweep) because assets/Windows can't be in CI.

**T12 — Docs consolidation (small, do opportunistically).** Promote the decompile-trap
checklist (R7) into `docs/reference/`; add the **chip definition-of-done** to
AGENT-WORKFLOW (§6); after T2 lands, shrink CLAUDE.md's pin recipe to one line. Add a
**scenario×subsystem coverage matrix** to STATUS (derived from scenario yaml + a
`covers:` field) so R1 gaps are visible at a glance.

**Explicit non-priorities** (so effort isn't spent): TTD expansion (forensic reserve
status is correct; Frida capture-local solved the reliability need), more SPA viewer
features beyond the editor's current scope (it just hit "battle-tested viewer" — let
usage drive it), PCM audio diff, headless/wine render path.

---

## 4. Phase + RNG pinning — assessment and upgrades

### 4.1 The current design is right — state why, once

`{phasepin}` + `{rngseed 19937}` does NOT fake parity; it **normalizes the phase/seed
ORIGIN and then lets each side run its own code from that shared origin** — so a
pinned bit-identical result is a *parity proof of the logic given the same data* (the
bg-NPC warmup re-run is the cleanest example: both sides execute their own spawn code
from seed 19937 and must agree). The doctrine distinction (data-1:1 vs observed-1:1,
pillars off-but-accepted) is exactly the right epistemics. The `--verdict`
ALIGNED / CONST-OFFSET / DRIFT classifier over db054-aligned flow traces is the right
oracle. Keep all of it.

### 4.2 Upgrades (in priority order)

1. **Exhaustive pin discovery** — the phase-state census (T3). The pin list stops
   growing by ambush.
2. **Default-on pinning** — capture-time auto-pin + lint (T2). The policy stops
   depending on memory.
3. **RNG callsite table default-on in the verdict** — a DESYNC should *name its
   consumer* (caller VA × count delta) in the same output, not require a separate
   `--rng-callsites` capture. (Fold into T1/T7.)
4. **Multi-seed sweeps** of milestone traces (R3) — guards the canonical-seed blind
   spot.
5. **Known-pending pins to fold in now** (from FRONT/findings): the dialogue
   advance-arrow blink counter (`DAT_073a3e0c` / port `s_blink` — port should draw from
   per-script-reset state AND be phasepin-covered); audit the world-map entry timer
   after T3's census.

### 4.3 Considered and rejected (recorded so it stays rejected)

- **Full retail-state snapshot injection into the port** as a pinning replacement:
  requires a complete retail↔port volatile-state mapping (the port deliberately does
  not mirror memory layout); save-virtualization already covers persistent state;
  census+pins covers volatile counters at a fraction of the cost.
- **Reproducing retail's load-stretch** to make absolute frames align: chasing
  nondeterminism at its source (disk/loader timing) instead of normalizing it is
  strictly worse; anchors + turbo + pins already give a shared clock.
- **Tolerance-based (SSIM) diffing** to absorb phase noise: rejected 2026-05-21 and the
  reasons have only strengthened — 1-LSB and 1-2px bugs were real findings; pinning
  removes the noise the tolerance would have hidden.

---

## 5. Direction & sequencing

### 5.1 What the numbers say (2026-06-09)

462/2,548 functions touched (18.1%), ≈17% of code bytes, 69 runtime-verified, 18 open
PORT-DEBT shortcuts, 977 commits in 22 days, last-200 commit mix ≈ 46% tools / 32% src /
22% docs. 34 scenarios, all in title→intro→house→world-map space. 118 engine quirks
documented. The tooling stack (unified harness, studio v2 + SPA, flow-trace, d3d-trace
+verts, verdict, capture-local) is **complete enough that the marginal session should
now be mostly porting**, with tooling additions drawn from §3 as they pay for
themselves.

### 5.2 Sequencing verdict

Frame-0-forward along the player's actual path (title → prologue → house → shop loop →
world map → dungeon) is correct and should not change: it satisfies the upstream-inputs
pillar by construction and every arc lands user-visible parity. The **next big rocks in
encounter order**: finish the shop interaction/roundtrip arc (incl. customers/market —
the largest *shop-side* unported logic), generalize the dialogue cluster into full
event-VM coverage (IVE ops are the keystone for ALL story content), town scenes off the
world map, then the **first dungeon run** (combat/records completion, enemy AI, drops —
the largest unported subsystem overall), then synthesis/guild, then the day-cycle
save roundtrip closing the loop.

### 5.3 Milestone replay ladder (recommended, lightweight)

Define standing pinned+call-traced milestone traces, each a permanent scenario:
**M1 "first shop day"** (load → place → sell to scripted customers → close → save),
**M2 "first dungeon run"** (gate → 2 floors → boss chest → return), **M3 "chapter-1
week"** (the TAS-framework vision's first big waypoint). Per milestone track ONE
outcome KPI: **first-divergent-frame ordinal** (and % aligned frames) under standard
pins — reported by triage (T1) and recorded in STATUS next to the ledger numbers.
Function-count measures input effort; first-divergent-frame measures the actual goal
(frame-exact replay depth) and is immune to "many functions ported but the frame still
diverges at f300".

### 5.4 Standing build/platform decisions

i686 + x87 stay until post-parity (§1.3). No branches until nightly users matter
(existing policy). Portability (SDL/GL backend) stays Phase 6 — any abstraction layer
before full parity would forfeit the same-D3D8-runtime property the harness depends on.

---

## 6. Opus working agreement (how to get mileage out of all this)

1. **Entry points:** CLAUDE.md (auto-loads) → `docs/FRONT.md` (current front) → this
   audit (strategy) → `docs/flow-trace-cheatsheet.md` (the divergence decision tree,
   until T1 subsumes its three questions into `triage`).
2. **Never re-derive:** §1/§4.3/§7 decisions; `findings/confirmed-parity-ledger.md`
   entries; the benign-divergence registry. A tool "divergence" on a confirmed-1:1 item
   is a lead, not a regression (CLAUDE.md doctrine).
3. **Chip definition-of-done** (make every chip end the same way): decompile-faithful
   body (asm-checked where Ghidra is ambiguous) → host test(s) → `CALL_TRACE` fields
   declared on both sides → pinned-trace verification on the live session (recapture
   the user's session, inspect `diff/frames/` or the served URL — never a hand-paired
   /tmp diff) → ledger/PORT-DEBT updated → engine-quirks entry if anything non-obvious
   surfaced → commit.
4. **Sub-agent discipline** (AGENT-WORKFLOW): chips single-threaded in the main loop;
   fan-out reserved for audits/surveys; briefs self-contained with paths + length caps.
5. **Build the §3 items as they unblock real work, in rank order** — T1/T2 first; T3
   the next time a phase ambush costs more than an hour; T4 with the shop-roundtrip
   arc; T5 at the dungeon arc's start.
6. **Keep memory thin** — durable knowledge goes in docs/ (this file is the worked
   example); auto-memory holds pointers.
7. Consider a `/fewer-permission-prompts` pass to allowlist the standard read-only
   harness invocations — small, compounds across every session.

## 7. Considered-and-rejected register (beyond §1/§4.3)

- **Whole-exe static recompilation** (N64Recomp-style: lift x86 → C, start at 100%
  parity, refactor down): rejected. x86 lifting fidelity is the hard part (x87 80-bit
  stack semantics, jump tables, SEH, CRT intrinsics) — a lifter bug poisons the ground
  truth the whole project depends on; 18% is already hand-ported and verified; and the
  refactor-down endpoint equals the current endpoint with an extra unverifiable middle.
- **Wine-based Linux harness**: rejected 2026-05-19 (PLAN §6) — WSLInterop + real
  Windows D3D8 is the runtime retail shipped against; unchanged.
- **Hand-wiring run-openrecet + frida_capture** for synced captures: superseded by
  `scenario-test`; the primitives remain internal.
- **Restarting frida-server to fix flakiness**: permanently rejected — the 128 MiB
  ceiling is a per-message cap, "process-terminated" always has a real cause (the
  2026-06-08 capture-crash RCA being the proof of method).
- **`-finstrument-functions` whole-program call tracing** (harness-roadmap E.2 sketch):
  superseded by the explicit `CALL_TRACE_ENTER(va)` annotation scheme — lossless,
  self-documenting, stub-aware; keep as the only call-trace mechanism.

## 8. Facts snapshot (2026-06-09, for future diffing)

- **Retail:** VC6 (linker 6.0), static CRT, x87, `.text` 1,126,734 B, 2,620 functions
  (2,548 non-thunk, ~1.04 MB), classic-LCG rand. Unpacked image has no Rich header.
- **Port:** 127 .c / ~69k LOC src; 124 test files / ~68k LOC; mingw i686 `-O2 -g`
  `-std=c11`, no FP flags (x87 default), `-static-libgcc`; two PEs (GUI + console).
- **Coverage:** 462 touched / 69 verified / 15 stubbed / 2,086 unported; 18 PORT-DEBT
  entries (registry `docs/port-debt.md`).
- **Corpus:** 34 scenarios (all pre-dungeon), 23 with goldens (10 dual-target), 4 with
  d3d goldens, ~3 pinned variants, 3 call-traced. All carry `{savefile}`.
- **Hooks:** `core.hooksPath=tools/git-hooks` (pre-commit = ledger regen + host tests
  on C changes; commit-msg = co-author trailer). CI nightly = build + RIFF gate only.
- **Oracle targets enabled:** rng_next15, audio_fade, boss_id_allowed,
  floor_is_checkpoint (raise per R1/T-policy).
- **Velocity:** 977 commits since 2026-05-19; ~158/week recent; last-200 mix 46/32/22
  tools/src/docs.
