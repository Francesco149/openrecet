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
| Tear (companion) appearance | "slightly off" — position and/or anim phase (unisolated); wing-flap phase adds comparison noise | persistent; investigate once everything else is spot on | needed for faithful sprite-Z dust occlusion (#5) |
| Foot-dust position/phase | RNG-stream desync vs retail | user: visible bugs first | after the visible free-roam render gaps close |
| Ambient mote positions (`FUN_0046f648`) | a few tiny dots don't match in the diff; very faint, hard to spot even in retail | user: "not worth chasing the visual parity for now" (2026-06-01); sim is structurally faithful (1×/frame = retail post-warmup) | when chasing free-roam visual parity, after structural parity up to free-roam |
| Wing-flap phase alignment | flap phase not aligned at capture anchor | "chase phase later" (§81) | companion-controller faithfulness pass |

## CONFIRMED 1:1 (human-verified)

| subsystem | scope | evidence | date |
|---|---|---|---|
| **Recette position + walk phase** (free-roam) | exact frame-for-frame on the walk benches; she reads pure-black in the port-vs-retail diff | user, on the `house-walk-down-dense` feed comparison | 2026-06-01 |
| Player walk + collision (HOUSE) | bit-exact px/pz vs retail (mesh resolver) | engine-quirks §60–70; wall_collide_diff | 2026-05-31 |
| **Character ground shadow** (player+Tear, Csh.1) | **user-confirmed 1:1** | user (explicit), + feed `20260601T122354_6f81` cap_06 feet zoom | 2026-06-01 |
| Foot-dust EMIT cadence | every 16 frames (median gap 16) | Frida ground-truth probe | 2026-06-01 |
| **Furniture shadows** | **user-confirmed 1:1** — the real baked 3D meshes (real mesh loader), NOT a placeholder; table-base zoom-diff all-black | user (explicit) + `house-walk-tables` cap_10 zoom-diff, feed 2026-06-01 | 2026-06-01 |

## CONFIRMED NOT 1:1 (human-flagged — do NOT hand-wave)

| subsystem | what's wrong | note |
|---|---|---|
| **Tear (companion) appearance** | "slightly off" — a **persistent** known issue. **NOT isolated**: could be position OR animation phase; the wing-flap not being exactly 1:1 per frame also adds comparison noise. | NOT jitter to dismiss. Do NOT assert it's "position." Investigate closely **later, once everything else is spot on** (user, 2026-06-01). It's the suspected reason b1acf7c's sprite Z occluded her glow, but the exact cause isn't pinned. ([[project_next_char_controller]]) |
| **Foot-dust occlusion** | dust draws IN FRONT of the walker; retail draws it behind | needs faithful sprite Z-write (see draw-order GT) — NOT yet solved; b1acf7c's attempt was reverted |
| **Foot-dust position/phase** | diverges from retail | likely free-roam RNG-stream completeness ([[scene1-rng-stream-parity]]) — but treat as a real structural gap to CLOSE, not "just RNG" |
| **Ambient motes** (`FUN_0046f648`, ported 2026-06-01) | a few faint dots don't match in the zoom-diff | user-flagged 2026-06-01. The **sim is structurally 1:1** (warmup-once then 1 call/frame, exactly retail — call-trace verified on `house-wall-collide`); the visual mismatch is the same family as the Tear-phase + dust position divergences (positions, not structure). Render only the DARK contact pass — the bright sparkle is a separate unported sprite pass. Deferred (faint, not worth chasing now). |

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
  ambient-mote render (`FUN_0046f648`) that `FUN_00470385` also calls IS now
  ported; the object-table blobs remain. Separate minor cosmetic chip.

See [[scene1-walk-dust]] (draw-order ground truth), [[scene1-rng-stream-parity]].
