# Plan: HOUSE free-roam structural-parity roadmap

> **Status (2026-06-01):** ACTIVE survey. This is the master work-list for
> closing structural parity on the HOUSE free-roam state, written now that the
> player controller is un-MVP'd (controller Chips 1–4,
> `plans/house-controller-unmvp.md`). It is **evidence-based**: the priorities
> come from a live call-graph diff of *what retail actually executes* in
> free-roam, not from guessing. Lead priorities (user, 2026-06-01): **(1) Tear
> glow particles, (2) dialogue, (3) 2D HUD.**

## Methodology — how this list was derived

Two signals, reconciled:

1. **Live call-graph diff (the backbone).** A Frida call-trace of *retail*
   (`tools/frida_capture.py --call-trace`, the 1979-VA `engine_function_vas_frida_safe`
   set) over a **140-frame HOUSE free-roam window** (idle → walk-start), driven
   by `traces/house_walk_ct140.jsonl` (`{"calltrace":[1500,140]}` anchored at the
   2nd `HOUSE_FREEROAM`). Run: `runs/freeroam-ct-retail/` (348 k events).
   - **168 distinct engine functions execute** in free-roam.
   - Of those: **30 verified + 61 ported = 91 already done**; **12 stubbed + 65
     unported = 77 are the work list** (51 KB). Data: `runs/freeroam-worklist.json`,
     `runs/freeroam-execset.json`.
2. **Static reachability (the completeness check).** `tools/freeroam_reach.py`
   does a transitive closure from the free-roam spine roots over the decompile's
   FUN→FUN edges (`docs/decompiled/by-address/`) and classifies vs
   `port-ledger.json`. The static set (1109 unported-reachable) **over-approximates**
   (it includes every ingame state — dungeon, all menus), so the *live* set is the
   prioritizer; the static set supplies caller/depth context and catches
   free-roam-conditional paths the 140-frame window didn't trip.

**Caveat (coverage floor):** the Frida-safe set is 1979 of 2548 functions; ~570
crash-on-hook functions can't appear, so the 77 is a *lower bound*. And the live
set only counts *unported/stubbed* functions — it does **not** see incomplete arms
inside functions the ledger calls "ported" (Class B below). Both are covered here.

## Two gap classes

- **Class A — unported/stubbed functions that execute in free-roam** (the 77).
  Structural holes: the function has no faithful body yet.
- **Class B — incomplete arms inside "ported" functions that execute.** The
  ledger status is coarse; several "ported" functions are partial/TODO stubs whose
  free-roam-relevant arm is missing. **The user's top two priorities are Class B:**
  - **Tear glow (type-0x1f) render arm** of `FUN_004176ff` — ledger says "ported"
    but it is the `scene1_walk_chr_TODO` stub; no particle arm is written.
  - **Dialogue (Pass 8a)** + the **always-on HUD** arms of `FUN_0040a765` — the
    HUD aggregator shell is ported (`scene1_hud.c`) but passes 4–9 are absent.

## Headline — the free-roam frame, what's missing

```
SIM spine   FUN_004536cb (sim_a, stubbed body)         ← steady arm faithful; transitions missing
  └ FUN_00442cef (ingame default arm, VERIFIED)
      ├ FUN_0048670f player ctrl (VERIFIED skeleton)   ← walk bit-exact; impulse chain not structural (P4)
      │   └ FUN_0048b850 (stubbed) → 489e66/48cdcc/4897c6 impulse+timers+slot
      ├ companion ctrl (VERIFIED) + wing-glow EMIT      ← sparkle spawns + ages, but NOT DRAWN (P0)
      ├ FUN_0044bb1a camera-pan driver  — UNPORTED, VISIBLE (camera follows to room edge)
      ├ FUN_0042ea35 NPC sprite anim    — UNPORTED, VISIBLE when customers present
      ├ FUN_00430c6d customer integrator/spawner — UNPORTED (producer for 0042ea35)
      └ FUN_0046f621 ambient particle motes — STUBBED no-op, VISIBLE (floating dust absent)
RENDER spine FUN_004547ab (VERIFIED)
  ├ 3D mesh walker (room/furniture)     — drawn (PII.3b + C8 walkers); some leaf draws stubbed
  ├ FUN_004176ff table-A/B glow walker  — TODO STUB → no particle/glow billboards (P0 Tear glow)
  ├ FUN_0040a765 HUD aggregator (shell ported) → passes 4–9 absent:
  │     FUN_00409925 always-on HUD (gold/gauge/day/panel) — P2
  │     Pass 8a dialogue text                              — P1
  │     FUN_0040c4eb item tooltip, bubbles, menus          — later
  └ FUN_004523e6 FPS overlay — BENIGN (registry), do not port
```

## Prioritized roadmap

### P0 — Tear glow particles (user #1) — **✅ LANDED 2026-06-01 (`src/scene1_wing_glow.c`)**

> **Landed.** The renderer is `FUN_004176ff`'s records-A **main**-sweep arm
> L3818 (NOT the L1422 sweep / "catch-all" theorised below — see the correction
> in `docs/findings/scene1-wing-glow.md` + engine-quirks §80). Recipe recovered
> from retail GT (`tools/dump_wingglow_groundtruth.py`): additive ONE/ONE, grey
> age-fade diffuse × bright-blue `effect.bmp` 32×32 cell, ±256 quad,
> `RotZ·billboard·S·T`. Port draws bright additive blue at Tear (verified
> +67,+100,+140 RGB at the glow core). The original prose below is left for the
> investigation record; the renderer hunt is done. **Open follow-up:** aggregate
> glow is smaller than retail at the matched frame — suspect the frozen player
> (controller unported, P4) offsetting Tear + the unported Tear sprite palette,
> not the renderer; revisit after P4.

The companion wing-sparkle is emitted (`co_emit_wing_sparkle`, `FUN_00447f4f`,
type-0x1f, every 4th frame) and aged (`scene1_particles_tick.c`, grav −0.001 /
damp 0.97 / kill 0x20) but **nothing draws it**.

**PROBE RESULT (`runs/tear-glow-probe/`, retail `--dump-records-b`):** the sparkles
are unambiguously **records-A type-0x1f**. At any free-roam frame there are **~8
live type-0x1f records-A slots**, scale 0.1, at Tear's hover position (≈(1.2, 4.0,
9.0) — y≈4 = the flying fairy, vs Recette at y=0), ages stepping 1/5/9/…/29 (the
every-4-frame emit, kill at 32). **records-B is EMPTY (count_b==0) the entire run**
(free-roam AND the tutorial dialogue). So:
- The port's **emit + sim are correct** — same table, same ~8-live steady state.
- `FUN_004176ff`'s only 0x1f arm (L1180) walks **records-B**, which never populates
  → that arm never runs in free-roam. **Do NOT port L1180 for the Tear glow** (the
  earlier P0.1 plan was wrong).

**Renderer IDENTIFIED (2026-06-01, `runs/tear-glow-draws/` `--quad-hist` draw trace).**
On a free-roam frame, **`FUN_004176ff` emits 6 `DrawPrimitiveUP` billboards at exactly
the records-A 0x1f positions** ((1.38,3.91,8.82), (1.11,4.34,9.35), (1.18,3.57,9.59),
…). So `FUN_004176ff` **is** the renderer — via its **records-A sweep**
(L1422–~L1995, base `&DAT_069b2fb8`, count `DAT_0076b960`), not the dead records-B
L1180 arm. The user confirms the glow is a bright translucent blue. (Also seen in the
same trace: `FUN_0046f648` draws the 6 ambient motes = P3; `FUN_0045a56f` the
character bodies.)

**Why the branch-search missed it:** the records-A sweep dispatches type as a
**float-reinterpreted** constant (`fVar22 = pfVar15[-2]`, compared to `1.23314e-43`
= 0x58, etc.), and its explicit arms are {0x58,0x93,0x5a,0x56,0x42,0x41,0x61,0x72,
0x62,0x1,0x2} — **0x1f is not among them**. The 0x1f particles fall into the sweep's
**catch-all `else`** (≈L1708), which is the wing-glow arm.

**The draw recipe (for the port chip):**
- **Geometry:** ±256 object-space quad, `D3DPT_TRIANGLESTRIP`, prim_count 2, FVF
  `0x142` (XYZ|DIFFUSE|TEX1, stride 24) — identical shape to `scene1_pass_f.c`.
- **Texture:** the glow atlas (runtime handle observed `0x15a3d1b0`), bound by
  `FUN_004176ff` itself.
- **World matrix:** per-particle `translate(pos) · scale(scale·0.0005) · billboard
  (DAT_0438cdf8)` via `thunk_FUN_004a3462`=mat_translation, `004a33d2`=mat_scaling,
  `004a2a03`=mat_mul (already identified helpers; `DAT_0438cdf8` billboard matrix is
  ported). `scale` = records-A field +0x38 (the 0.1 from the emit).
- **Diffuse (the blue):** intensity `uVar5` = age-fade (`age·0x10` clamped 0xff, then
  ramped down past a late-age threshold). The catch-all builds D3DCOLOR with
  **B=uVar5, R=G=uVar5/2** → ≈`0xFF7F7FFF` light blue. (Other arms tint differently;
  0x1f→else→blue.)
- **Blend:** additive/translucent (confirm `SRCBLEND/DESTBLEND` from the sweep
  preamble via d3d-trace; `--quad-hist` didn't record renderstate).

**Chip P0.1 (final) — records-A wing-glow billboard renderer.** Port the records-A
sweep's catch-all (0x1f) arm, modeled on `scene1_pass_f.c` (the analogous records-A
0x92 renderer): walk records-A for the non-explicit types, build the per-particle
billboard matrix, set the blue age-fade diffuse, bind the glow atlas, draw the ±256
tristrip. Validate with a **zoomed port-vs-retail diff on Tear** (push to feed *with*
the diff panel — user expects this). **Risks:** the `thunk_FUN_004a*` matrix args are
FPU-stack-mangled in Ghidra — recover the exact compose order via objdump at the
sweep's call sites before trusting the decomp; pin the blend mode + the exact `uVar5`
age formula. Reclassify `FUN_004176ff` ledger status (it is the `scene1_walk_chr_TODO`
stub, not fully "ported").

### P1 — Dialogue (user #2)

**Chip P1.1 — dialogue text (Pass 8a of `FUN_0040a765`, ~L261–351).** Lands as an
edit to `scene1_hud.c` (the shell is already there): speaker portrait/name quad on
`DAT_073d8748` + word-wrapped body text (measure `FUN_0047d0ea`, draw via the
ported `font_draw_text`), reading the per-character text array at
`slot_base + 0x2a6c4`, gated on `DAT_0438b92c != 0`. **Dep:** the per-save-slot
record struct (below) + porting the wrap measurer `FUN_0047d0ea`. Makes all
story/shop dialogue visible.

### P2 — Always-on 2D HUD

**Chip P2.1 — `FUN_00409925` (3434 B) HOUSE always-on HUD core** + its always-on
leaves `FUN_00481ec3` (368 B, day/value digit plate) and `FUN_00407ab4` (504 B,
event badge). Draws the **gold counter, gauge bar, day plate, bottom-right info
panel** — the single biggest free-roam pixel win, present every idle frame. **Dep:**
the **per-save-slot record struct** (base `&DAT_044e3798 + slot*0x2dfc8`), typed for
gold/gauge (`+0x450fb90/94/98`); the `DAT_073d8748` atlas is preloaded; reuse
`font_draw_text` + the `FUN_005038ff` sprintf shim (already ported). **Decision
needed:** the save record is BSS-zero today, so the HUD would render "0 gold" until
the gameplay sim populates it — land it drawing zeros (structurally correct) or
block on save-record population? Recommend land-drawing-zeros; the struct is a
shared dependency for P1/P2 and several later chips.

> **Save-record struct is the pivotal shared dependency** for P1 dialogue, P2 HUD,
> and most Pass-8 panels. Worth a dedicated typing chip (map the offsets the agents
> inventoried: `+0x450fb90/94/98` gold/gauge, `+0x2a6c0/c4` dialogue, `+0x2c3ec`
> day, `+0x275c8` news, `+0x2a600` orders, `+0x2c400` level).

### P3 — Visible free-roam quick wins (independent, small)

- **Ambient particle motes — `FUN_0046f621`/`FUN_0046f2a3` + render
  `FUN_0046f648`/`FUN_00470385`.** `FUN_0046f621` is **mislabelled** in the port as
  "RNG churn" and stubbed to a no-op; it's actually the warmup driver for the
  shop's floating dust/sparkle motes (sin/cos drift, gravity ×0.05). Porting it
  makes the motes appear — the only clearly-visible gap in the player-ctrl subtree.
  Verify via feed montage vs retail. (Watch the float-as-int Ghidra constants:
  `2.52234e-43` = int 180, etc.)
- **Camera-pan driver — `FUN_0044bb1a` (455 B).** Visible scroll/snap as the player
  nears a room edge; standalone, wires at `FUN_00442cef` L160. Deps `FUN_00485c74`
  pan-kick + `FUN_00482a51` camera-lerp.

### P4 — Player-controller structural faithfulness (mostly invisible)

Closes the controller un-MVP. **Key discovery:** the walk impulse the port models
as a hand-rolled `sin/cos(facing)·0.1` actually lives in
`FUN_0048b850 → FUN_0048cdcc → FUN_00489e66` (`*(player+0x904) += sin/cos(angle)·accel`,
facing decoded by `FUN_00489db8`, accel chosen by a gate-tree that collapses to 0.1
in free-roam). The §61/§75 notes that place it in `FUN_0048670f` are imprecise. The
approximation is bit-exact on the benches but **not structurally faithful and is NOT
currently tagged `PORT-DEBT(simplified)`** — it should be, then retired by:

- **P4.1** `FUN_00489db8` facing decode + `FUN_00489e66` impulse writer (the real
  step-1 chain); proof of faithfulness = the gate-tree collapses to 0.1, walk stays
  byte-identical.
- **P4.2** `FUN_0048cdcc` input→state machine (the impulse's caller); restores the
  real `b850 → 48cdcc → 489e66` call structure (combat/talk arms land as `stubbed`).
- **P4.3** `FUN_004897c6` per-frame timers + actor render-slot fill (+ `FUN_004855e2`).
- **P4.4** `FUN_0048cbf6` item-pickup proximity + the b850 after-image/collect arms
  (dormant until item entities exist).

Risks: dropped Ghidra float args (these take the player base in a register, not the
spurious `float param_1`); pin `*DAT_068dd2f0` HOUSE-gate polarity empirically;
verify the particle-respawn RNG stream (`FUN_00471089`) is separate from the
gameplay roll `FUN_0043647f` before assuming P3-motes and P4.4 are independent.

### P5 — NPC presence (visible once customers exist)

- **`FUN_00430c6d` (3022 B) customer integrator/spawner → `FUN_0042df55` one-shot
  scene-entry effect spawner → `FUN_0041f4a6` table-A allocator** (the producer).
- **`FUN_0042ea35` (2135 B) people-table sprite integrator** (the consumer —
  animates in-shop customer sprites). **Ordering:** retail calls the producer
  (`FUN_00430c6d`, L170) before the consumer (`FUN_0042ea35`, L172) — land together
  or keep that order or NPCs animate from garbage.

### P6 — Transition machinery (blocks *leaving* free-roam, not free-roam itself)

- **`FUN_004536cb` (sim_a) blocks 11–17** — the scene-transition/fade latch set
  (`sim-step-a-dispatch.md`). The steady free-roam tick is already faithful; this is
  what's needed to open a menu / change floors / fade out.
- **`FUN_004427f1` stage-type tail router (90 B) + HOUSE callee `FUN_0044bd0d`** —
  port the router + HOUSE branch, leave the 6 dungeon/worldmap callees stubbed.
  Confirm HOUSE's stage-category lands in the `FUN_0044bd0d` range first (Frida read
  of `DAT_068dd3fc`).

### Deferred / out of scope for free-roam

- Vendors menu `FUN_0046b00a`, customer haggle panel `FUN_0046602e`, Pass-8b panels
  (News/Order/Charm/Level), item tooltip `FUN_0040c4eb`, speech bubbles — all
  conditional on a menu/trade/customer being active. Port when trading is in scope.
- Status screen `FUN_004141c0` (Pass 3). Dungeon walker `FUN_00407cac`.
- Debug-param tick `FUN_00405552` — faithful stub; body inert unless the debug
  overlay (`DAT_06a49938`) is on. SE played-flag latch `FUN_004994f3` — audio
  bookkeeping; land with a host test for call-count parity.

## Appendix A — CRT / libm / allocator leaves (DEMOTED)

The high-frequency callees in the `0x4f8xxx` / `0x503xxx`–`0x512xxx` band (call
counts to 52 k / 140 frames) are **MSVC CRT / libm / allocator / compiler-intrinsic
leaves** — `sprintf`/`_output`, `memcpy`, `strlen`, `sin`/`cos`, 64-bit div/mod,
float↔string (`$I10_OUTPUT`/`_fltin`), heap free-list, `va_arg`, SSE sinf/cosf.
29/29 classified high-confidence (several carry VS lib names). **Satisfied by the
host toolchain; excluded from gameplay parity.** The only caveat: if a sub-pixel
trig divergence ever appears, suspect host libm rounding vs retail's x87/SSE path —
a TAS-determinism note for the benign-divergence registry, not a port task.

## Appendix B — corrections this survey surfaced (fix opportunistically)

- `FUN_004176ff` ledger status "ported" → it is a **TODO stub**
  (`scene1_walk_chr_TODO`); reclassify so STATUS coverage isn't overstated.
- The walk impulse is **not** in `FUN_0048670f` (§61/§75) — it's the
  `b850→48cdcc→489e66` chain. The hand-rolled approximation should be tagged
  `PORT-DEBT(simplified)` then retired by P4.
- `FUN_0046f621` port comment says "RNG churn / no visible effect" — it is the
  **ambient-particle-mote warmup**; the no-op suppresses visible motes.
- `scene1-walker.md` mislabels `FUN_00409925` as "price/cell HUD" — it is the
  **always-on gold/gauge/day/info HUD**.
- The "Adventurer Camera" / "Button 4: Change Camera" bottom labels are **not** in
  `FUN_00409925`/`FUN_0046602e`; likely a sibling label renderer (candidate
  `FUN_0040c962`, caller `FUN_004547ab`). One targeted trace before assuming P2
  covers them.

## Appendix C — reproduce / refresh this survey

```
# 1. live free-roam call-trace (retail; host = cutestation.soy:27042)
nix develop --command python3 tools/frida_capture.py --remote cutestation.soy:27042 \
    --run-dir runs/freeroam-ct-retail --input-segtrace traces/house_walk_ct140.jsonl \
    --call-trace --max-frames 16000 --duration-ms 600000 --turbo --silent-audio --hide-window --no-montage
# 2. static reachability + classify
nix develop --command python3 tools/freeroam_reach.py --out runs/freeroam-reach.json
# 3. cross-ref executed VAs vs ledger → runs/freeroam-worklist.json (see git history of this doc)
```

> Follow-up (tracked): productize step 1 as a TAS "call-graph-trace on/off" segtrace
> op + scenario flag so this plugs into any input trace without hand-editing
> (`plans/` TODO, user-requested 2026-06-01).
