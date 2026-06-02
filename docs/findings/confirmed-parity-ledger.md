# Confirmed parity ledger — what is (and is NOT) 1:1 with retail

> **Purpose (user directive, 2026-06-01):** keep an explicit, durable record of
> which behaviours have been **confirmed with human eyes** to match retail (or
> not), so we never re-litigate a settled fact, and never hand-wave a real
> divergence away as "jitter/RNG/phase" until it is **100% human-confirmed**.
>
> **Rules:**
> - A row is **CONFIRMED 1:1** only when the *user* has eyeballed a zoomed
>   port-vs-retail diff (on a deterministic TAS trace) and said so.
> - A row is **CONFIRMED NOT 1:1** when the user has flagged it as wrong/off.
> - Anything else is **UNVERIFIED** — do not assert it matches, and do not
>   dismiss a divergence in it as benign, until a human confirms.
> - Update this file the moment the user confirms/denies something. Cite the
>   feed push id / scenario / date.
> - **DEFERRED ≠ benign ≠ resolved.** When the user says a small divergence
>   "isn't worth chasing right now," it goes in the **Deferred** table below and
>   stays there until fixed — it is a known real gap, not something to forget or
>   later hand-wave away.

## DEFERRED (real divergences the user chose not to chase yet — keep tracking)

| subsystem | divergence | deferred when / why | revisit trigger |
|---|---|---|---|
| Recette **idle** animation phase | idle pose "slightly off" on `house-movement` **cap_00 (first frame = idle free-roam)**; she's **1:1 while moving** (cap_01/02). Likely idle-phase accuracy, not position. | user, 2026-06-01 ("probably accuracy issues on idle phase that we'll chase later") | idle-animation faithfulness pass; isolate idle-phase counter vs retail |
| Tear (companion) appearance | "slightly off" — position and/or anim phase (unisolated); wing-flap phase adds comparison noise | persistent; investigate once everything else is spot on | needed for faithful sprite-Z dust occlusion (#5) |
| Foot-dust position/phase | RNG-stream desync vs retail | user: visible bugs first | after the visible free-roam render gaps close |
| Background-window NPC anim/identity phase (`scene1_bg_npc`, was "ambient motes") | sprites now RENDER + animate (user-verified 2026-06-02); exact per-frame char/anim-phase vs retail not yet pinned | sprites landed 2026-06-02; phase a faithfulness follow-up | bg-npc anim-phase pass; see [[scene1-bg-npc]] |
| Suspected real ambient particle (the "tiny dots") | a few tiny dots don't match in diffs; very faint | was wrongly attributed to "ambient motes" (that's the NPC system, now resolved); a genuine faint ambient effect likely exists but is unfound | when chasing free-roam visual parity; find the real effect, don't re-conflate with bg-npc |
| Wing-flap phase alignment | flap phase not aligned at capture anchor | "chase phase later" (§81) | companion-controller faithfulness pass |

## CONFIRMED 1:1 (human-verified)

| subsystem | scope | evidence | date |
|---|---|---|---|
| **Recette position + walk phase** (free-roam, **MOVING only**) | exact frame-for-frame on the walk benches; she reads pure-black in the port-vs-retail diff. **Scope = while moving** — her *idle* phase is separately flagged off (see NOT-1:1 + Deferred). | user, on the `house-walk-down-dense` feed comparison | 2026-06-01 |
| Player walk + collision (HOUSE) | bit-exact px/pz vs retail (mesh resolver) | engine-quirks §60–70; wall_collide_diff | 2026-05-31 |
| **Character ground shadow** (player+Tear, Csh.1) | **user-confirmed 1:1** | user (explicit), + feed `20260601T122354_6f81` cap_06 feet zoom | 2026-06-01 |
| Foot-dust EMIT cadence | every 16 frames (median gap 16) | Frida ground-truth probe | 2026-06-01 |
| **Furniture shadows** | **user-confirmed 1:1** — the real baked 3D meshes (real mesh loader), NOT a placeholder; table-base zoom-diff all-black | user (explicit) + `house-walk-tables` cap_10 zoom-diff, feed 2026-06-01 | 2026-06-01 |
| **Opening dialogue fade-from-black** (chr:5 kuro.tga, alpha 255→0 over fadeframe:240) | **user-confirmed 1:1** — render + the per-frame fade curve are bit-identical **when phase-aligned** (anchored to the fade's own `EXTRA_SPRITE_FADEOUT` edge; the *absolute* HOUSE_FREEROAM start-offset drifts — synthetic-load phase, engine-quirks §85). | user (explicit, feed) + `intro-fade` 0.07 mean\|abs\|/ch (residual = FPS overlay) | 2026-06-02 |
| **Opening dialogue extra/effect sprites** (sigh tameiki / zzz pop-ups) | **user-confirmed bit-identical** — render AND **within-script phase** are 1:1 with **no artificial sync**: captured at a *fixed* offset from `TEXT_ANIM_END` (no per-effect anchor) the diff is 0.06 mean\|abs\|/ch (residual = FPS overlay). Only the absolute HOUSE_FREEROAM offset drifts (§85, common to all prologue timing). | user ("bit identical") + `intro-sigh` / TEXT_ANIM_END-relative re-capture | 2026-06-02 |
| **Opening dialogue per-line** (bedroom bg, standees incl. Tear's −390→−100 slide-in, box, nameplate, char-reveal/book-icon, text) | user-confirmed 1:1 vs the per-line goldens (slide-in + char-based reveal landed 2026-06-02). Remaining real deltas tracked separately: box-edge halo + FPS overlay. | user (feed, cap_00/cap_01 + slide) + `intro-dialogue-lines` (line 1 diff = box-edge only) | 2026-06-02 |
| **Bottom-left "Merchant Level" HUD** (badge disc + level number, gold "Merchant Level" label, XP bar; `FUN_00409925` body → `scene1_merchant_hud.c`) | **user-confirmed 1:1** ("the hud is definitely 1:1"). `pixel_diff` of the bottom-left HUD region at native 640 = 0.07% (13/19008 px), white-diff black. Gating also confirmed: shown in free-roam + iv1_2 dialogue, hidden during iv1_1 (engine-quirks §87). Dormant/deferred: the "LEVEL UP!" pop (`FUN_00407ab4`, needs the level-up event subsystem to drive `DAT_0438b920`). | user (explicit) + `house-walk-table` vs `runs/changecam-trace2` retail, `pixel_diff` crop 0,414,288,480 | 2026-06-03 |

## CONFIRMED NOT 1:1 (human-flagged — do NOT hand-wave)

| subsystem | what's wrong | note |
|---|---|---|
| **Dialogue box-edge halo** | a ~1px halo around the `ive_window.tga` bubble border vs retail. Everything inside/outside the box is pixel-exact; only the frame edge differs (`intro-dialogue-lines` line-1 diff = box edge only). | user-flagged 2026-06-02 ("halo of pixels around the bubble, likely scaling filter"). Suspect POINT-vs-LINEAR / box-mip on the box quad (cf engine-quirks §54). Real delta — do NOT hand-wave. Task #7; opening-prologue.md box-edge note. |
| **iv1_2 opening: Recette look-up + blink** | during the iv1_2 (2nd dialogue) opening (post-fade, during char slide-in) retail plays Recette's HOUSE freeroam sprite a look-up-at-Tear pose + a blink (≥3 cycles); the port is static. | user-flagged 2026-06-02. Freeroam-sprite anim during the dialogue (NOT the standee). Reference: `intro-iv2-gap` scenario (zoom (520,578) 104×150, cap_03 open→cap_04 closed). opening-prologue.md §"Remaining real deltas" #4. |
| **iv1_2 opening: Tear angry pose + radial-lines** | same window — retail plays Tear an angry pose with a billboard radial-lines (manga anger-marks) effect; port absent. | user-flagged 2026-06-02. Same `intro-iv2-gap` zoom frames both Recette + Tear. opening-prologue.md #4(b). |
| **Dialogue text fade-to-transparent on dismiss** | when a line is dismissed retail fades the glyph text out as the box closes; the port pops it off. | user-flagged 2026-06-02. `intro-iv2-gap` captures the dismiss window. Box-close alpha on the text in `FUN_0046c9a2`. opening-prologue.md #5. |
| **Recette idle animation phase** | idle pose **slightly off** at `house-movement` **cap_00 (first frame, idle free-roam)** — while her **walking is confirmed 1:1** (cap_01/02). | user, 2026-06-01. Likely an idle-phase *accuracy* issue (the idle anim counter / start phase), NOT position — she's pure-black in the diff once moving. Chase later; do NOT hand-wave as jitter. ([[project_next_char_controller]]) |
| **Tear (companion) appearance** | "slightly off" — a **persistent** known issue. **NOT isolated**: could be position OR animation phase; the wing-flap not being exactly 1:1 per frame also adds comparison noise. | NOT jitter to dismiss. Do NOT assert it's "position." Investigate closely **later, once everything else is spot on** (user, 2026-06-01). It's the suspected reason b1acf7c's sprite Z occluded her glow, but the exact cause isn't pinned. ([[project_next_char_controller]]) |
| **Foot-dust occlusion** | dust draws IN FRONT of the walker; retail draws it behind | needs faithful sprite Z-write (see draw-order GT) — NOT yet solved; b1acf7c's attempt was reverted |
| **Foot-dust position/phase** | diverges from retail | likely free-roam RNG-stream completeness ([[scene1-rng-stream-parity]]) — but treat as a real structural gap to CLOSE, not "just RNG" |
| ~~**Ambient motes** (`FUN_0046f648`)~~ — **RECLASSIFIED 2026-06-02** | this was never ambient motes: it is the **background-window NPC system** (`scene1_bg_npc`, the townsfolk drifting past the back window). Sim + dark contact-shadow were ported 2026-06-01; the **bright character sprite** (`FUN_0046f737`) landed 2026-06-02 and is **user-verified rendering** (in-game + feed). See [[scene1-bg-npc]]. Residual: exact anim-phase/identity vs retail (deferred), and the genuinely-faint "tiny dots" that may be a *real* separate ambient effect not yet found (do NOT re-conflate). |

## REGRESSIONS caught + fixed this session

| regression | cause | fix | confirmed |
|---|---|---|---|
| Tear's blue wing-glow vanished | b1acf7c sprite full-quad Z-write occluded it (Tear's off-position Z in front of the glow) | revert (957af8c) | user: "the glow is there I promise" |
| Rectangular dust/shadow hole around Recette | same b1acf7c Z rectangle clipped the dust | same revert | user: "yes that fixes it" |

## Still UNVERIFIED (don't assert either way)

- **Opening-prologue dialogue cadence — MACHINE-frame-exact, awaiting user
  eyeball.** A `scenario-test intro-dialogue-lines` port↔retail diff (2026-06-01)
  found **44/45 inter-line TEXT_ANIM_END gaps identical to the frame**; the lone
  difference is the iv1_1→iv1_2 transition (retail +103 frames = the deferred
  inter-script load screen). Strong, but it is an automated frame-diff, not a
  user-eyeballed render — and the per-line TEXT/box PIXELS are still deferred
  (draws not ported), so do NOT yet claim visual 1:1. See
  `docs/findings/opening-prologue.md` §"port↔retail cadence is frame-exact".
- Wing-glow size/intensity exact match (port glow looked smaller at one matched
  frame — never human-confirmed; could be Tear position).
- Object/furniture contact-shadow-BLOB pass (`FUN_00470385` object table,
  `DAT_073a6e84`) — still **stubbed** (no src ref); retail draws ~6 soft multiply
  blobs under objects every frame (draw-order GT). An *augmentation on top of* the
  already-matching baked mesh shadows; visible impact small/localized
  (house-render-gaps §4). ZWRITE=0 → irrelevant to dust occlusion. The sibling
  background-NPC contact-shadow render (`FUN_0046f648`, `scene1_bg_npc`) that
  `FUN_00470385` also calls IS ported; the object-table blobs remain. Separate
  minor cosmetic chip.

See [[scene1-walk-dust]] (draw-order ground truth), [[scene1-rng-stream-parity]].
