# Merchant-Level HUD vs iv1_2 dialogue-character draw order (RESOLVED 2026-06-03)

**RESOLVED.** Root cause: the port drew the dialogue (`scene1_dialogue_draw`)
BEFORE the HUD (`scene1_hud_render`); retail draws it AFTER. The "standing
character" that occludes the HUD is the **iv1_2 conversation character**, drawn by
the dialogue system (`FUN_0046c090`, render_quad 2D quads) — not a free-roam
billboard. Engine render root `FUN_004547ab` dialogue-active path: `FUN_0045bbf9
(scene) → FUN_0040a765 (HUD) → FUN_00417504 (overlay) → FUN_0045404b (fx_tail) →
FUN_0046c090 (dialogue, LAST)`. Confirmed THREE ways: (1) that code order; (2) a
fresh retail iv1_2 d3d-trace `runs/iv2-hud-zorder-d3d` frame 5100 — the
merchant-HUD ADDSIGNED render_quad draws, then the dialogue render_quad batch
draws after it; (3) that frame's pixels show Tear's leg over "Merchant Leve|l".
Fix: moved `scene1_dialogue_draw` after `scene1_hud_render` + `scene1_render_overlay`
in main.c. Historical investigation kept below for context.

---

# Merchant-Level HUD vs standing-character draw order (was OPEN)

**2026-06-03, user-flagged from the README hero.** In retail the lower part of
Tear's standing sprite (her knee) **occludes** the right end of the bottom-left
"Merchant Level" HUD (the final "l" of "Level" + the XP-bar outline). In our port
the HUD draws **on top of** Tear instead. Full-res proof: the feed hero asset
`20260602T235731_a500` — PORT half shows the complete gold "Level" + blue bar
over the leg; RETAIL half shows "Leve|" with the leg covering the rest. (The
*current* `house-idle` both-run, frame 9146/8969, has Tear on the right and does
NOT exercise the overlap — its bottom-left HUD diffs black — so reproduce with a
frame where a character stands over the HUD.)

## Why it is NOT a z-test issue

The HUD quads are `render_quad` XYZRHW with **z = 0** (nearest), so with ZFUNC
LESSEQUAL they always pass the depth test — a character can never occlude the HUD
by depth. The occlusion must therefore be **draw order**: retail draws the
standing character *after* the merchant HUD; our port draws it *before*.

## The contradiction to resolve before fixing

The engine render root `FUN_004547ab` documents the order
`scene1_render_meshes (3D + shop-walker billboards) → FUN_0040a765 (merchant HUD)
→ overlay → fx_tail`. So the **shop-walker** character billboards draw BEFORE the
HUD (HUD on top) — and the retail d3d-trace `runs/walkdust-d3d` frame 5495
confirms it: the 7 `FUN_0045a56f` sprite draws (0x45aa31) come first, the
ADDSIGNED merchant-HUD quad (0x405396, ZEN0) draws last. For a *walking*
character the HUD is correctly on top in both retail and port.

Yet the hero (an **idle/standing** character) shows the character OVER the HUD in
retail. The only consistent explanation: retail draws the **idle standing
character** in a pass that runs AFTER `FUN_0040a765` — i.e. NOT the early
shop-walker billboard (`FUN_004552d0`/`sw_pass_light`) that the walking sprites
use, but a later pass (the overlay system `FUN_00417504`, fx_tail, or a
standing/conversation-pose path). Our port draws the standing player+companion
via the early shop-walker (`sw_pass_light`), so our HUD lands on top.

This ties into the still-deferred standing-character draw-path work (the
session-start note's "multiple character draw paths" — `FUN_004552d0` shop-walker
vs `FUN_00456f56` chr-walker vs the conversation-pose `FUN_0048407f`; Tear is also
not yet 1:1, confirmed-parity-ledger.md).

## Next step (authoritative)

Capture a retail **d3d-trace at an idle house frame where a character stands over
the bottom-left HUD** (the walkdust trace is a walking frame and never has the
overlap), then read which function's draw lands AFTER the 0x405396 merchant-HUD
quad. That identifies the real late character pass; port the standing character
through it (after the HUD) so the knee occludes the HUD as in retail. Do NOT
reorder blindly — the player/companion ZWRITE + draw-order is the same area that
caused the b1acf7c furniture-shadow/glow regression.
