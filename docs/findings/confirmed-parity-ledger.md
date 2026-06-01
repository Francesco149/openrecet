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
| Tear (companion) position | "slightly off" hover position | persistent; not blocking play | needed for faithful sprite-Z dust occlusion (#5) |
| Foot-dust position/phase | RNG-stream desync vs retail | user: visible bugs first | after the visible free-roam render gaps close |
| Wing-flap phase alignment | flap phase not aligned at capture anchor | "chase phase later" (§81) | companion-controller faithfulness pass |

## CONFIRMED 1:1 (human-verified)

| subsystem | scope | evidence | date |
|---|---|---|---|
| **Recette position + walk phase** (free-roam) | exact frame-for-frame on the walk benches; she reads pure-black in the port-vs-retail diff | user, on the `house-walk-down-dense` feed comparison | 2026-06-01 |
| Player walk + collision (HOUSE) | bit-exact px/pz vs retail (mesh resolver) | engine-quirks §60–70; wall_collide_diff | 2026-05-31 |
| Character ground shadow (player+Tear, Csh.1) | "pretty spot on" | user, feed `20260601T122354_6f81` (cap_06 feet zoom) | 2026-06-01 |
| Foot-dust EMIT cadence | every 16 frames (median gap 16) | Frida ground-truth probe | 2026-06-01 |
| **Furniture shadows** | the visible furniture/floor shadows are the **real baked 3D meshes** (real mesh loader), NOT a placeholder — table-base zoom-diff is all-black | `house-walk-tables` cap_10 zoom-diff, feed 2026-06-01 | 2026-06-01 |

## CONFIRMED NOT 1:1 (human-flagged — do NOT hand-wave)

| subsystem | what's wrong | note |
|---|---|---|
| **Tear (companion) position** | "slightly off" — a **persistent** known issue | NOT jitter to dismiss; her wrong Z is what made b1acf7c occlude her glow. Real fix pending ([[project_next_char_controller]]). |
| **Foot-dust occlusion** | dust draws IN FRONT of the walker; retail draws it behind | needs faithful sprite Z-write (see draw-order GT) — NOT yet solved; b1acf7c's attempt was reverted |
| **Foot-dust position/phase** | diverges from retail | likely free-roam RNG-stream completeness ([[scene1-rng-stream-parity]]) — but treat as a real structural gap to CLOSE, not "just RNG" |

## REGRESSIONS caught + fixed this session

| regression | cause | fix | confirmed |
|---|---|---|---|
| Tear's blue wing-glow vanished | b1acf7c sprite full-quad Z-write occluded it (Tear's off-position Z in front of the glow) | revert (957af8c) | user: "the glow is there I promise" |
| Rectangular dust/shadow hole around Recette | same b1acf7c Z rectangle clipped the dust | same revert | user: "yes that fixes it" |

## Still UNVERIFIED (don't assert either way)

- Wing-glow size/intensity exact match (port glow looked smaller at one matched
  frame — never human-confirmed; could be Tear position).
- Dynamic contact-shadow-BLOB pass (`FUN_00470385`/`FUN_0046f648`) — **stubbed**
  (no src ref); retail draws ~6 soft multiply blobs under objects every frame
  (draw-order GT). These are an *augmentation on top of* the already-matching
  baked mesh shadows; visible impact is small/localized (house-render-gaps §4).
  ZWRITE=0 → irrelevant to dust occlusion. Separate minor cosmetic chip.
- Ambient motes (`FUN_0046f2a3`, 6 live in HOUSE) — stubbed; never rendered.

See [[scene1-walk-dust]] (draw-order ground truth), [[scene1-rng-stream-parity]].
