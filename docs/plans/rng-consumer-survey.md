# Plan — the RNG-consumer survey (the minimum FOUNDATION for the deterministic trace)

**User directive 2026-06-23 PM** (after the {csloadpin} fix made b574=119 match yet the trace
studio still rendered the port's wrong reaction line "How much should I?..." vs retail
"Capitalism, ho!"):

> *"just matching 1 rng element isn't really useful — for rng stuff you just need a full rng
> survey and work at the rng-consumer level until we have every consumer ported; then the rng
> stuff will fall into place IF the phase pinning is also solid. That is the minimum foundation."*

So **STOP chasing single matched values.** A matched downstream output (the offer b574) does
NOT prove the rng STREAM is aligned at an upstream draw (the `cs_pick_line %2` variant/face).
The foundation is two pillars; the variant/face/offer become 1:1 only when BOTH hold:

## Pillar A — port EVERY rng consumer (draw COUNT + ORDER match frame-for-frame)
The scope = the working trace `house-firstcust-cutscene-day2`, window cc08==4 + the
skip→wrap-up→first-customer flow.

**Method (the survey loop):**
1. **CLEAN rng-callsite capture, NO phasepin** (the phasepin's bg-NPC re-seed FABRICATES
   consumers — RE §8.4 lesson: "never rng-drill through a `{phasepin}`"). Capture per-frame
   rng call-sites on BOTH port + retail over the window:
   - retail: `frida_capture` rng_callsites (the `0x47be92`/rng-callsite probe).
   - port: the equivalent call-trace rng probe.
   - tools: `tools/flow_diff.py --rng-drill` (who-consumed-the-LCG at an absolute frame),
     `tools/cs_walker_drill.py` (per-frame rngcalls + npc-state diff), per-frame `rngcalls`
     compare from a `scenario-test --target both --call-trace` capture.
2. **Diff per-frame rng draw COUNT** port vs retail. Each frame where the count differs ⇒ a
   consumer the port stubs / mis-counts / double-runs. (Earlier seen: retail draws ~2× the
   port over the cc08 window — RE §8.4 — because the port stubbed `FUN_0047019f` etc.)
3. **Attribute each divergence to a specific consumer** (the call-site VA → the function).
4. **Port that consumer 1:1** (faithful, no MVP) so its draw count+order matches.
5. Repeat until the per-frame rng draw count+order is **bit-identical frame-for-frame** across
   the whole window.

**Known / suspected consumers in this window (verify + port each):**
- 目玉商品 sparkle `player_ctrl_display_sparkle_emit` (g_sim%8==3, 3×rng_next_unit × occupied
  display column) — the `gsim` +50 phase ⇒ fires at a shifted %8 origin (pillar B).
- bg-window NPCs `scene1_bg_npc` (FUN_0046f621 warmup + per-frame drift) — user-flagged.
- in-shop cs-walker chibi NPC pump `scene1_customer_npc_pump` (FUN_0047019f) —
  `PORT-DEBT(cs-walker-rng-phase)`, the npcsp/npcfr divergence at off~30 (RE §19).
- the companion sparkle `FUN_0048a833` (db054%4==0 wing-glow) — db054 frozen in cc08 (RE §8.8).
- the cs live machine: `cs_pick_line` (FUN_00460a1a, the %2 variant draw = the VISIBLE gap),
  `cs_accept_eval` (FUN_00460672), `cs_pushback_line` (FUN_00460f16).
- (sweep for any others the rng-callsite diff surfaces — DON'T assume this list is complete.)

## Pillar B — solid PHASE PINNING (seed/phase ORIGIN match)
The seed + frame-phase origin must match so the (already-ported) consumers draw the same
VALUES, not just the same count. Work-list:
- **`{phasepin}`-breaks-wrap-up TOOL gap** (task #2; lead: the Frida bg-NPC re-seed ALSO forces
  `DAT_0438b4e0=0` spawn-gate open at FUN_0046f621, which the PORT never mirrors ⇒ retail keeps
  spawning bg_npc through the wrap-up CONV_POSE, extra rng, the 1176-blink stall; the port's
  gate stays frozen ⇒ survives). Fix so bg_npc + g_sim CAN be pinned. Then the full phasepin
  zeroes the `gsim` +50 offset too.
- **WALL-CLOCK pin** (hook GetTickCount/QPC/timeGetTime → a virtual clock synced at anchors,
  like `{rngseed}`) for any remaining time-based non-determinism.
- **`{csloadpin}`** ✅ (the load-bracket pin; the v3 re-arm-race tool fix landed `9c455f3`).
- **`{rngseed}`** ✅ (per-segment canonical seed).

## Success criterion
A `scenario-test --target both --call-trace` (and the v3 `--state` capture) shows the per-frame
rng draw count+order **bit-identical** across the window AND `flow_diff --verdict` is ALIGNED;
THEN the rendered reaction line + face + offer are 1:1 in the trace studio (verify on ≥2
captures + BOTH harnesses, per the FRONT). Only then call it done.

## Anti-pattern (what burned us 2026-06-23)
Matching ONE value (b574=119) and declaring the variant fixed. The rendered variant stayed
wrong. Single-value matches are coincidence until the whole stream is aligned.

## Progress (2026-06-24)
- **✅ 目玉 sparkle (g_sim%8) — DONE.** Pillar-B `{gsimpin}` op (RE §21, commit b2ba55f): the port's
  `g_sim_frame_count` origin differs (intro skip) AND was non-deterministic run-to-run; pinned to retail's
  recorded gsim at the f406 entry (`{gsimpin:[0,811]}`). Sparkle now fires 1:1 at the same offsets; gsim
  bit-identical to retail from the entry to the reaction. The METHOD established here: compare **cumulative
  rngcalls from the aligned entry** (0x47be92 = VA 4701842), attribute a divergent frame via retail's
  `FUN_005041f6` LCG entries' `ret_va` (RVA+0x400000 → caller). See RE §21.1 for the recipe.
- **✅ cs-walker spawn — ALIGNED (not a gap).** npcsp matches frame-for-frame (off 30/60/90…); the earlier
  "spawn-cadence phase" worry (`PORT-DEBT(cs-walker-rng-phase)`) was a drill-column misread. Its residual
  divergence is DOWNSTREAM of bg_npc (the spawn reads a bg_npc-misaligned LCG).
- **✅ bg_npc position phase — `{bgnpcpin}` LANDED 2026-06-24 (RE §21.2, commits 2207c1a + d9abe4e).** Option (a):
  a CONDITION-gated agent dump captures retail's natural `DAT_073a7f80` SoA at the f406 entry (the segment-gated
  {memsnap} can't — the wrap-up desync stalls it); the `{bgnpcpin:[F,[150 dwords]]}` op pins `g_scene1_bg_npc`
  field-by-field (port struct NOT byte-compatible). Host-tested; fires at off 0; baked. NPC0 is the inert
  cs-leave-reset slot (rng-irrelevant). PORT-ONLY (retail = un-pinned source).
- **✅ rng-drill UNBLOCKED 2026-06-25 — condition-gated rng hook (RE §21.3).** The boot-installed rng-callsite hook
  taxed the initial cad868 Continue-load ⇒ retail mis-timed the esc-skip ⇒ ran the SCRIPTED tutorial, never reaching
  the b51c==0 f406 entry. **Fixed** by deferring `installRngCallerHook` from boot to the f406 entry (`cc08==4 &&
  b51c==0`, in the agent's segtraceTick — the same gate as the bgnpc SoA dump); auto-enabled in `frida_capture` when
  the segtrace carries a `{bgnpcpin}`. VERIFIED: the deferred retail drive (`…215600Z`) ARMS the hook @frame 14658 +
  reaches the entry (the boot-hook run `…203209Z` never did). The initial load still stretches ~14000f under the
  call-trace trampolines (§21.2's "no-tax=no-stretch" was imprecise), but reaching the entry is what the drill needs.
- **FIRST DRILL VERDICT (RE §21.3):** `cs_walker_drill` (port `203038Z` ↔ retail `215600Z`, `--span 200`, both
  bgnpcpin+gsimpin): **14/200 frames diverge in per-frame rngΔ; gsim%8 aligned off≥1; cs-walker spawn cadence (npcsp)
  aligned.** Remaining rngΔ gaps (off 8,30-34,57,60,82,107,132,191,198; biggest = the off 30-34 spawn cluster, retail
  +11; net retail +3 over off 1-199) = the consumer-level COUNT gaps to survey. off=0 is a measurement boundary.
  **NEXT:** re-drive `--rng-callsites` over the entry window → `FUN_005041f6` ret_va attribution (§21.1) per diverging
  offset → port the consumer 1:1 → re-drill. Start with the off 30-34 spawn cluster.

## Progress (2026-06-25, RE §21.4) — the off-30-34 cluster traced; TWO roots + the foundational blocker
The survey of the off-30-34 cluster (no re-drive needed: the `215600Z` retail call_trace already carries the
`FUN_005041f6` ret_va rows; tool `/tmp/rng_sxs.py`) found:
- **✅ bg_npc off-by-one PIN TIMING — ROOT-CAUSED + FIXED + port-VALIDATED.** off-7 diverged from TWO sub-issues:
  (a) retail's bg_npc entry state is **non-deterministic run-to-run** (variable warmup ticks during the variable
  load) ⇒ the PORT-ONLY `{bgnpcpin}` pins to a STALE capture that can't match a fresh retail drive; (b) the pin
  landed **one tick late** (segtrace `base+0` fires at *anchor+1*, so the LOADING_END-segment pin → off1, but the
  dump = D₀ off0 pre-tick). **Fix:** bgnpcpin → **CONV_POSE_END segment** (off0-effective) + **BILATERAL** (agent
  writes the canonical to retail; `frida_capture` forwards `bgnpc_pin_soa`; `--no-bgnpc-pin-retail`=capture mode).
  **STEP-1b:** port (off0-effective, 215600Z dump) ↔ natural retail 215600Z = off-7 1:1, stream bit-identical off 0-28.
- **cs-walker GRID gap (off 29-32) = a NEW pillar-A consumer**, NOT a bg_npc cascade: identical rng VALUES at off 29
  yet cs_npc_tick (FUN_0046fbee) retargets a different # of times ⇒ the furniture-layout grid `DAT_074b28e8`
  (`shop_display_grid_rebuild`/FUN_0048960d, from the save's shop-tier template + furniture) differs port↔retail.
  Retarget LOGIC verified vs the decompile ⇒ grid CONTENT differs. **NEXT:** dump retail's grid + the port's, diff.
- **★★★ THE FOUNDATIONAL BLOCKER (pillar B, task #1): the cross-target WRAP-UP DESYNC manifested as a HARD STALL.**
  The bilateral `--target both` drive STALLED retail in the iv1_7 wrap-up CONV_POSE (**1180 blinks, never reached the
  f406 entry**) — load-dependent + intermittent (215600Z reached the entry; this run didn't). Until this is fixed,
  bg_npc/g_sim pinning can't be reliably validated AND the trace isn't deterministic. **This is now the #1 task** —
  the segtrace {wait} stalls when a wrap-up anchor (DLG_LINE_CLEAR?) doesn't fire under retail's load jitter. Lead: §21.2.

## Pointers
FRONT active arc; `findings/customer-service-haggle-RE.md` §8.4/§8.8/§19/§20/§20.1/**§21/§21.1**;
`findings/scene1-rng-stream-parity.md`; `findings/freeroam-rng-consumption.md`;
`docs/flow-trace-cheatsheet.md`.
