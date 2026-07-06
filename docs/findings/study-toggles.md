# Study toggles — six kill-switches for the HOUSE lighting tricks

**Tooling, not RE** (light_debug's sibling): runtime switches to film the actual
game (openrecet drop-in + retail data) with each HOUSE lighting trick
individually OFF, for the @GemmaExplains shop-lighting video. `src/study_toggles.{c,h}`.

## Controls

| Key | `--study-off` name | Trick | Hook site |
|-----|--------------------|-------|-----------|
| SHIFT+1 | `mod2x`    | ×2 room brightness (drawcode 2 → COLOROP=MODULATE2X on the room/map+furniture draws) | `scene1_render.c` — the three palette-combiner call sites (base L185 + pre-pass + alpha-pre, all `+0x1a40`-driven) demote via `study_room_combiner_mode` → MODULATE |
| SHIFT+2 | `keylight` | the single directional key light | `scene1_maplight.c ml_study_light` — Diffuse zeroed on the bound copy |
| SHIFT+3 | `ambient`  | the ambient fill | same — Ambient zeroed |
| SHIFT+4 | `fog`      | the near-invisible scene fog (fog:20:500, fogcolor 230:240:255) | `scene1_render.c scene1_apply_fog_state` — enable gate |
| SHIFT+5 | `hikari`   | the five god-ray planes of shop_1st.x | `scene1_walker_pass_init.c` — pass-3 `draw_loop_b_mesh` skips (same draws light_debug wraps) |
| SHIFT+6 | `blob`     | character blob shadows (player/companion + bg-NPC + customer) | `scene1_chr_shadow.c` — Block A + the two NPC blob calls gated; Block G glow (not a shadow) kept |

- All default **ON = retail**; every hook is a read of a default-1 int ⇒ traces/
  TAS/parity untouched (host 3394/0 post-change). Combos free — that's the point
  (`--study-off mod2x,keylight,ambient,fog,hikari,blob` = the everything-off look).
- Hotkeys in main.c WM_KEYDOWN next to F5/F6, repeat-guarded (lParam bit 30);
  digits 1-6 are NOT in the bindable DIK table (`input.c input_dik_table` has no
  0x02..0x07) ⇒ no gameplay collision; SHIFT bindable but unbound in vendor
  defaults. Each press printf's the new state to stdout — **no on-screen
  indicator** (filming).
- CLI `--study-off name,name,…` pre-flips at boot (headless verify + hands-free
  filming). Harness passthrough: `scenario-test.py … --exe-arg=--study-off
  --exe-arg=mod2x`.

## Grounding corrections (vs the naive per-trick recipe)

- **ambient ≠ D3DRS_AMBIENT**: the render state is ALREADY black (0xff000000)
  through the scene-1 pass (scene1_render.c L188-L198); the visible fill is
  **light 0's Ambient** (the maplight mode-3 preset, 0.6³ daytime). So ambient-off
  zeroes the light's Ambient component.
- **keylight ≠ LightEnable(0,FALSE)**: key + fill live in the SAME light 0 —
  a full disable would kill both tricks at once. Component-zero keeps them
  independent (both-off ≡ light contributes nothing anyway).
- **mod2x scope**: only the room-pass palette-combiner (`drawcode`/+0x1a40)
  applications. The hikari pass's own combiner (`hikaridrawcode`/+0x1a54), the
  wide-followup billboard MODULATE2X, chr-prepass, and every UI/title MODULATE2X
  stay retail.

## Verification (2026-07-06, house-loaded-display-pinned, ordinal frame 15)

| Variant | mean brightness | px(>8) vs baseline |
|---------|-----------------|--------------------|
| baseline (all ON) | 103.0 | 0 |
| mod2x OFF   | 58.6 (≈half — the ×2 is real) | 581 653 |
| keylight OFF| 65.0 | 498 714 |
| ambient OFF | 49.0 | 584 920 |
| fog OFF     | 102.1 — sub-8 everywhere; 435 709 px shift 1-7, depth-graded (far rows |Δ|=2.2 vs near 0.5) | 0 |
| hikari OFF  | 101.1 (beams gone) | 37 766 |
| blob OFF    | 103.2 (shadows gone) | 5 534 |
| ALL six OFF | **5.6** — the room mesh renders black; only unlit sprites/HUD survive. The room's entire look IS the six tricks. | 587 964 |

Montage: `runs/study-toggles/montage_all8.png` (pushed to the feed). Each
variant driven through the standard harness (`--exe-arg` passthrough), 48/48
captures, exit 0; `study-toggle: <name> OFF` confirmed in each stdout.log.
