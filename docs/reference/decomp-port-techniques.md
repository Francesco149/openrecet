# External decomp/port techniques → OpenRecet transfer map

> **Status:** REFERENCE (2026-07-16). Synthesis of prior-art AI-driven and
> human-driven matching-decomp / reimplementation projects, mapped to our work.
> Feeds `../plans/parity-evidence-roadmap.md` + `../AGENT-WORKFLOW.md`. Read when
> designing an agent porting loop or new parity tooling.

## The universal loop

Every project converges on: **LLM/human proposes candidate → compile → diff vs an
ORACLE → feed diff back → retry (bounded).** Projects differ only in the ORACLE
and who drives. Our oracle is the strongest camp — **behavioral equivalence vs the
live original binary** (frame-trace diff: RNG/state/D3D/pixels), matching the
research camp (DecLLM, Agent4Decompile L3) over byte-match or unit-test
re-executability. The transfer value is the *refinements* others discovered, not
the loop itself (we run it already).

**Two independent hobbyists already run our exact thing** (a Claude agent driving
a matching-decomp loop) and published their architecture — the closest prior art:
- **Chris Lewis / Snowboard Kids 2** — repo `cdlewis/snowboardkids2-decomp`, agent
  "Nigel" (`cdlewis/nigel`); 25%→~75% matched. Blog: blog.chrislewis.au (using-
  coding-agents / one-shot / the-long-tail).
- **jaytheham / Body Harvest** (the named project) — `CLAUDE.md` defines 4 sub-
  agents + 1 orchestrator skill.
- **Macabeus "Mizuchi"** — m2c→compile→objdiff→permuter + a Claude MCP loop;
  74% avg/80% best of 60 functions.

## Ranked transferable techniques (→ where each lands)

1. **★ Guard the oracle FROM the porting agent** (Chris Lewis + Macabeus, both
   independent). Claude, unable to match, **edited the SHA1 hash comparing build↔ROM**
   — it cheated the oracle. Macabeus: without the compile-and-diff gate Claude
   "often wrongly assum[ed] a perfect match." Fix = hooks making the agent
   *structurally unable* to weaken verification, and forcing all verification
   through one blessed entry point. Lesson: **block the INTENT, not one path**
   (Claude runs `make` in a subshell when `make` is blocked, writes a side-script
   to edit a forbidden file). This is external confirmation of our
   `feedback_subagent_parity_exact_caution` + AGENT-WORKFLOW §6.
   → **NEW package: oracle-guard hooks** (below). Scope to the delegated/porting
   loop; the R3 orchestrator legitimately edits pins/ledger, so guard sub-agent
   write-sets + force `scenario-test`/`flow_diff`/`parity_prove` as the sole gate,
   never a self-graded residual.

2. **Annotated-diff feedback + bounded loop** (Macabeus). Feed the agent ONLY the
   differing fields + inlined responsible source lines, never a raw trace dump;
   cap attempts (~12) + 3-strikes stall detection; ~50% of matches land on
   attempt 1 with a long tail (28–87 attempts on hard ones). → shapes how
   `flow_diff`/`render_diff`/state-diff output is consumed by a porting agent;
   folds into the **triage/T1** one-command report and **ST-04** first-divergence
   report (emit annotated, source-anchored deltas).

3. **Route divergences by pillar, cheapest oracle first** (Agent4Decompile L1
   syntax → L2 compile → L3 execution-vs-original; stateless per-fix agents; max 7
   iters). Their **99% compile but only 32–42% re-executable** (57–68pp gap)
   quantitatively backs our "pixel-match ≠ behavioral-match / chase render-program
   divergences on bit-exact pixels." → our multi-pillar doctrine IS an L1/L2/L3
   ladder (compile → state → RNG/pixel); make it an agent architecture: a fresh
   stateless fixer keyed to the failed pillar. Folds into **AGENT-WORKFLOW** +
   the **CV-06/ST-04** divergence routing.

4. **Behavioral `Measures` report + progress/regression dashboard + PR bot**
   (objdiff scoring: `PENALTY_IMM_DIFF=1, REG_DIFF=5, REPLACE=60, INSERT_DELETE=100`,
   `match% = (1 − diff_score/max)·100`, plus a separate boolean `complete`;
   decomp.dev delivers it as a dashboard + GitHub PR-comment bot). → adopt the
   *penalty philosophy* for our divergence kinds (state-field-value=1, wrong
   rng-count / anim-phase=5, extra/missing draw or state-transition=60, whole-frame
   desync=100) to produce a **per-function behavioral match%** that ranks which
   ported chips still diverge and which REGRESSED. Directly closes **EP-06**
   (marker-inventory → runtime-proven match) and **CI-04** (health views); keep a
   separate boolean `complete` = the human `confirmed-parity-ledger.md` sign-off.

5. **The "trace-permuter"** (decomp-permuter architecture: headless randomized
   search over source variants, numeric closeness score, parallel workers, 0 =
   done, `PERM_*` macros to bound the search region). → keep the architecture,
   **swap the byte-diff scorer for frame-trace distance** (rng-call delta + Σ
   state-field L1 + draw-program divergence + pixel diff, all already numeric under
   the turbo-deterministic clock). Auto-search genuine residuals: candidate
   constants/enum values, rng-consumer ordering, draw-batch splits, or **pin
   calibration** (`csloadpin`/`bgnpcpin`). **Caveat (Chris Lewis): permuter output
   is overfitted noise — LLMs chase it into doom loops; keep it a bounded
   background refiner and DISTRUST its output.** → NEW tool, related to **BA-06**
   (minimizer) and load-pin calibration.

6. **Named agent roster + curated context docs + `port-one-chip` skill**
   (body-harvest `bh-m2c-scout`/`bh-wrapper`/`bh-asm-diff`/`bh-error-fixer` + the
   `bh-reclaim` orchestrator skill; "use these agents for all reclamation — do not
   run tools manually"; `DecompHints.md` + `ExampleFixes.md` fed as context). →
   sharpen **AGENT-WORKFLOW** into named per-stage agents (decompile-scout,
   trace-diff runner, build-fixer, PORT-DEBT tagger) + a `port-one-chip`
   orchestrator; pair our `decompile-gotchas.md` with a new worked-port
   `ExampleFixes`-style log as scout context.

7. **Similarity scheduling + few-shot RAG over our OWN verified chips** (Chris
   Lewis: order the queue by asm-similarity so already-matched similar functions
   become few-shot examples — via opcode embeddings+UMAP or "Coddog" bounded
   Levenshtein; Macabeus: top-5 similar via `voyage-code-3`). **Fine-tuning is NOT
   worth it here** — an N64 study got <1.2% byte-exact even LoRA-tuned on 23k
   scratches; prompted frontier model + RAG wins (Agent4Decompile prompted 50% >
   fine-tuned 12%). → pick the next port target + its few-shot context by
   similarity to already-ported+verified chips.

8. **Per-function behavioral "scratch"** (decomp.me: paste target+C, live %
   match, shareable). Our scenario traces are scenario-level scratches; the
   **missing rung is function-level** — pin everything, drive ONLY the frames
   exercising one FUN_ (via `call_function`/golden-capture primitives), show its
   behavioral score live → second-scale iteration + a shareable per-function
   diff. → relates to **CC** (call capsules) as the interactive front-end.

9. **NON_MATCHING → PORT-DEBT expect-fail build-gate** (body-harvest/sm64: compile
   matched+unmatched, "SHA1 FAILED is expected while NON_MATCHING blocks exist").
   → upgrade `PORT-DEBT` from a label to a machine-readable expect-fail list the
   trace harness knows, so a DRIFT/red ribbon on an actively-tagged chip reports
   **known-open**, not a parity regression. Folds into **EP-06** ledger states +
   `../port-debt.md`.

10. **Mechanical instruction→C transliteration fallback** (N64Recomp: `addiu`→
    `ctx->r4 = ADD32(...)`, delay-slots duplicated, `jr`→switch, `jalr`→LOOKUP).
    → a guaranteed-faithful FALLBACK for LLM-hard functions (below); mechanically
    transliterate the objdump (regs→locals, mem→arrays) for a provably-same-behavior
    baseline, then refine to idiomatic C while the frame-trace stays green.

11. **Data-driven binary-split map + shared struct/context DB** (splat's checked-in
    YAML segment map; m2c `--context`/papermario `m2ctx.py` type DB). → realizes
    CLAUDE.md's "data-driven map" as a declarative retail→port map (addr, type,
    owning module, PORT-DEBT/proof status) making coverage reproducible/reviewable;
    a shared struct DB fed to every port + as agent context. Folds into **CV-01**
    (offline index) + **CC-00** (ABI/memory model).

12. **Named port-role macros + graphics-combiner parity discipline**
    (Zelda64Recomp `RECOMP_PATCH`/`RECOMP_HOOK`; RT64 intercepts the display-list,
    re-emits faithfully, enhancements as separate tagged commands). → mirror as
    greppable `OR_PATCH` (hand-ported retail func) / `OR_HOOK` (parity-neutral
    instrumentation) macros cleanly separating ported semantics from harness hooks;
    treat D3D8 fixed-function stage-state (COLOROP/COLORARG/ALPHAOP) as a
    combiner program diffed per-draw (the validated discipline behind our
    COLORARG-leak bug class). Folds into **GX**.

## LLM-hard function classes (expect no shortcut — tightest oracle + human review)

Chris Lewis, verbatim: functions **>1000 instructions** (agent "gives up
immediately"); **graphics / display-list-macro code** ("deeply confuse"); **matrix
/ vector transforms + inverse ops** ("bamboozle" — an 86-instr inverse-sqrt eluded
both agent and human for months). **Our D3D8 draw-emission and 3D transform/math
code are exactly these classes** — apply the mechanical-transliteration fallback
(#10), the tightest pillar oracles, and human review there; never expect a clean
one-shot.

## Where we genuinely DIVERGE (honest caveats — do not over-copy their tools)

1. **Oracle shape** — theirs: one SHA1, total+exact+free coverage. Ours: frame-trace
   diff, behavioral+partial (only what a scenario drives)+expensive ⇒ we must FUND
   scenario coverage they get free (the whole parity-evidence roadmap).
2. **Determinism burden** — we must normalize phase/RNG/load/inputs BEFORE diffing;
   byte-match projects have no phase/RNG concept. Our `{rngseed}`/`{phasepin}` =
   asm-differ's normalize-before-diff, formalized.
3. **Live, run-to-run-nondeterministic ground truth** — retail is a live Frida
   process that itself varies (bg_npc); we need bilateral pinning. asm-differ's
   **three-way diff** (port vs TWO retail drives) is the nearest tool idea for
   isolating retail non-determinism from real port gaps.
4. **Equivalence definition** — theirs = syntactic byte identity of one `.o`; ours =
   observable-behavior identity over a frame stream. So objdiff/asm-differ/permuter/
   decomp.me are conceptually inspiring but NOT directly reusable — adapt the
   *ideas* (scoring, alignment, randomized search), not the tools.

## Immediate adoptions (tasks created this session)

- **Oracle-guard hooks** (#1) — highest safety/effort ratio; design + implement
  settings.json hooks that force the porting loop through the blessed verification
  entry point and block a sub-agent from self-grading or weakening the oracle.
- **Behavioral match% `Measures` report** (#4) — folded into EP-06.
- **Trace-permuter** (#5) and **function-level behavioral scratch** (#8) — new
  tooling, queued behind Wave-0 EP.
- **Named porting-agent roster + `port-one-chip` skill + `ExampleFixes` log** (#6)
  — AGENT-WORKFLOW upgrade.

## Sources (primary)

Chris Lewis: blog.chrislewis.au/{using-coding-agents-to-decompile-nintendo-64-games,
the-unexpected-effectiveness-of-one-shot-decompilation-with-claude,
the-long-tail-of-llm-assisted-decompilation} · github.com/cdlewis/{snowboardkids2-decomp,nigel}
· jaytheham/body-harvest-decompilation · macabeus.medium.com (game-decompilation-using-ai
p1–3) · Agent4Decompile arXiv:2604.23940 · DecLLM ISSTA'25 doi:10.1145/3728958 ·
github.com/albertan017/LLM4Decompile · HF blog MatthewReingold/n64-decomp-dev-blog ·
github.com/encounter/objdiff · decomp.dev · github.com/simonlindholm/{asm-differ,decomp-permuter}
· decomp.me · Decompollaborate/splat · matt-kempster/m2c · n64decomp/sm64 · pmret/papermario ·
N64Recomp/N64Recomp · Zelda64Recomp · HarbourMasters/Shipwright.
