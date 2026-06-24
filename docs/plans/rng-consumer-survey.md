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
- **⚠ rng-drill VERIFICATION BLOCKED — the determinism FOUNDATION gap (RE §21.2).** The drill needs retail's rngcalls
  over the f406 window ⇒ the rng-callsite hook ⇒ which TAXES the initial cad868 Continue-load worker's rng draws,
  inflating it from ~3500f to **14161f** (the WHOLE pre-entry stretch is that ONE load, NEW_GAME@206→HF@14367) ⇒
  the esc-skip mis-times ⇒ retail runs the SCRIPTED tutorial (b51c==1), never reaching the b51c==0 f406 entry.
  Retail's load-wait is COMPLETION-based (not time-based) so the **wall-clock pin canNOT fix it** (mis-framing,
  corrected). **Fix = (1) condition-gated rng hook** (defer installRngCallerHook to the f406 entry — clean, the
  measurement proves no-tax=no-stretch); (2) pinning that one load needs N≈14161 (impractical). The bgnpcpin pin
  itself is correct-by-construction; the stream-alignment verdict awaits the unblocked drill.

## Pointers
FRONT active arc; `findings/customer-service-haggle-RE.md` §8.4/§8.8/§19/§20/§20.1/**§21/§21.1**;
`findings/scene1-rng-stream-parity.md`; `findings/freeroam-rng-consumption.md`;
`docs/flow-trace-cheatsheet.md`.
