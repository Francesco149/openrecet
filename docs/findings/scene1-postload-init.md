# `FUN_00436f97` — scene-1 INGAME state-entry init (survey)

**Engine fn:** `FUN_00436f97` @ `0x436f97`, **4788 B / 710 lines** decompiled C.
**Survey date:** 2026-05-23.  **Chip:** Cf.0.

This is **not a per-tick** — it's the scene-1 INGAME state-entry initializer
that runs once on every transition into `DAT_0438b1c0 == 1`.  It resets
scene-1 BSS state, places the player on the stage's spawn point (with a
walkable-terrain spiral retry), runs ~12 sub-init helpers, dispatches to a
per-stage-class init function, and **at the very end** runs the 200-iter
ambient-particle spawn loop that the chip ladder calls out as our target.

The 200-iter spawn loop is **10 lines out of 710**.  Almost all of the rest
is BSS resets, stage-record fanout, and sub-helpers — most of which either
don't have consumers yet in our port or are touched by other already-ported
modules.  The survey breaks the body into 25 logical blocks so the MVP port
can stay tight to its scope.

## Why it's the next chip

Per `openrecet_scene1_render_ladder.md`, with C8h + C8i + Cs1 + Cs2 landed
the integrator and spawn API are both scope-complete and the per-tick INGAME
caller is wired.  What's still missing is **anyone calling `scene1_spawn`**.
`FUN_00436f97` is the engine's canonical caller — it pre-populates 200
type-0x4f particles around the player's new spawn pose so the scene comes
up with the ambient effect (smoke/dust/etc.) already mid-flight, then the
integrator evolves them.  Without this populator the data side stays empty
and Pass D / Pass F never receive records to draw.

## Callers (the function is *not* called per-tick)

Cross-referenced via `grep -rn "FUN_00436f97" docs/decompiled/by-address/`:

| Caller                | Site             | Context |
|-----------------------|------------------|---------|
| `FUN_0049e163`        | `LAB_0049e304` (L98)  | State-8 (dungeon combat) exit-to-INGAME transition. Runs after `FUN_004528b3()` poll succeeds and the new state has been committed. |
| `FUN_0048526d` (142 B wrapper) | L8  | Always paired with `FUN_004851e2(1)` + camera/UI commits. Called from 4 sites: `FUN_00462403:249`, `FUN_00442cef:335`, `FUN_0048670f:214/218`. The `442cef:335` site commits `DAT_0438b1c0 = 1` immediately after the wrapper — this is the canonical "enter INGAME" hook. |

> **CORRECTION 2026-05-29 — FUN_00436f97 *does* fire on new-game HOUSE
> entry.** The claim below (that HOUSE state-1 entry skips FUN_00436f97)
> was a static-analysis inference and is **wrong**. Empirical proof via
> the E.1 Frida call tracer (0x436f97 in the safe-VA list) driving
> --auto-z-spam into a new game: **FUN_00436f97 is called exactly once at
> engine frame 3200**, 11 frames before the first `scene1_render_meshes`
> (frame 3211) and the HOUSE furniture walker (0x457714). It runs the
> **block-21 else-branch** (the "alt-stage arm" phase-2 writer), which is
> what populates `DAT_0438bfb4` + the phase-2 furniture arrays the walker
> reads. The likely path is `FUN_00442cef` (scene1_ingame_default_arm_tick,
> the canonical "enter INGAME" hook) → `FUN_0048526d` wrapper → here. The
> capture is reproducible: `runs/cf-writer-probe/` (call trace) +
> `tools/dump_phase2_groundtruth.py` (the array values). The block-23
> ambient-spawn loop (Cf.1) staying HOUSE-dormant is a *separate* question
> from the block-21 writer, which is live.

The state-1 (INGAME) entry **from HOUSE** runs through `FUN_004547ab` case-1 →
`FUN_00474a9a` (the HOUSE-asset loader we already wired in C7e). That path
loads assets; the *stage-arm* writer (FUN_00436f97 block 21) runs in addition,
reached via the INGAME-entry arm tick (see correction above). HOUSE has no
ambient *particle* layer (block 23 stays dormant), but the block-21 furniture
arm DOES fire.

Important consequence for wiring: `scene1_preload_house_cb` is reachable but
not the engine-correct hook for the spawn loop.  See the MVP wiring section
below.

## 25-block breakdown

Line ranges refer to `docs/decompiled/by-address/436f97.c`.

| #  | Lines     | What it does                                                                              | Port status                  |
|----|-----------|-------------------------------------------------------------------------------------------|------------------------------|
| 1  | L37-38    | Per-stage data + palette pointers (`local_c = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8`; `DAT_068dd2f0 = &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c`) | partial (`g_stage_palette` exists; `DAT_044e3798` stage-record array unported) |
| 2  | L39       | `FUN_0044b219(0,0)` — small helper                                                         | unported (TODO survey) |
| 3  | L40-117   | ~75 BSS field zero-resets across `DAT_0438b...` and `DAT_044e...` scene state             | unported (all BSS-zero on boot anyway in our port — most consumers unported) |
| 4  | L118-125  | 20-entry 0xffffffff fill at `&DAT_0438b554` stride 4                                       | unported (no consumer in port) |
| 5  | L126-139  | Music-timer mod-N branch: `if (DAT_0438b4d4 < 0) iVar8 = DAT_0438b4cc % N; if (iVar8==0) DAT_0438b1e8 = 0`. N depends on `DAT_0438b4c8`. | unported |
| 6  | L141-149  | 5-channel reset at `&DAT_0438c008..0438c058` (0x14 dwords × 5 strides)                    | unported (no consumer) |
| 7  | L150-160  | ~10 more BSS resets (`DAT_0438be6c/70/74/78/7c/84/88/8c/90`, `_DAT_0438bfa8/ac`)         | unported |
| 8  | L161      | `FUN_004360b6()` — unknown small helper                                                    | unported (TODO survey) |
| 9  | L162-175  | Per-stage "starting HP table" init: 8 rows × 15 entries at offset `0x438b160` + `0x5bf*4`. Default 0x1c20 (DAT_0438b4c8==0) or 0x960. | unported (touches scene-1 HP/timer state; no consumer reads it from our port yet) |
| 10 | L176-227  | Stage-type music+time setup: `DAT_0438c85c = DAT_0438b4b0 * <per-class constant>` (switch on DAT_0438b4c8). Plus the alt-stage `DAT_056da1cc/d0` mode selector branch at L178-208 (reads `DAT_068dd3fc[stage*0x6cf]`). | unported (music driver / per-class constants tied to unported stage records) |
| 11 | L228-276  | **Player spawn placement + spiral retry**: `(&DAT_056daabc..)[i*3]=0` (velocity zero for player[i]); `(&DAT_056da1d8..)[i*3] = _DAT_0438b1ec - i*1.5` (player[i] x); `(&DAT_056da1dc/e0..)[i*3] = _DAT_0438b1f0/f4` (y,z). For i=0, spiral up to 7 retries via `FUN_00432e50` (terrain-collision test) — angle `((j+0.5)*2π)/4`, radius=2.  | unported |
| 12 | L268-275  | Sentinel writes for player[i]: `(&DAT_056dab00)[i*0xb]=4` (action), `(&DAT_056dae18)[i]=1.0f` (alpha?), `(&DAT_056dae20)[(i+1)*4]=1.0f`. Loop runs 3 iterations (`local_10 != 4.2039e-45` ≈ 3). | unported |
| 13 | L277-294  | Two parallel reset loops: `(&DAT_056da200..3e0)` stride 3 + `(&DAT_056da3fc..)` stride 0xb; then `DAT_056daae0=0`; then `(&DAT_056db11c..56dd7dc)` stride 0xf8 fills 0xffffffff. | unported |
| 14 | L295-300  | `(&DAT_056dacf8..56dae4c)` stride 0x11 zero pairs `[-0x55]` + `[0]`                       | unported |
| 15 | L301-378  | ~75 more BSS resets in `DAT_056da/56db...` range                                          | unported (all BSS-zero in our port) |
| 16 | L379-392  | NPC/people table init: `&DAT_0695efe4..0695b2fe4` stride 0xa8 × ~64 rows; per-row writes `[+8]=0xffffffff`, `[0]=0`, `[0x7f]=0`, and a 100-iteration inner loop copying `puVar5[0x80]` to `puVar9[0..99]`. | partial — `g_scene1_people` exists from C8h.4a (different stride 0x2e9 — these are likely different tables; needs reconciliation) |
| 17 | L393-403  | 11 sub-init calls: `FUN_0040d132`, `FUN_0040f64b` ✓, `FUN_0041f252`, `FUN_004991fd(0)`, `FUN_00432473`, `FUN_0041f758`, `FUN_004060ff`, `FUN_00401208`, `FUN_004850ec` | mostly unported (FUN_0040f64b ✓ = scene1_records_reset already in our port) |
| 18 | L404-407  | 4 more BSS clears (`DAT_0438b8a0/a4`, `_DAT_0438ccb0`, `_DAT_0438b8fc`)                  | unported |
| 19 | L408-409  | `FUN_0044bd03()` + `FUN_0046f1e9()`                                                       | unported |
| 20 | L410-587  | **Stage-type per-class init dispatch** (switch on `DAT_0438b4c8` 0..5): calls one of `FUN_0044c88f / d1e4 / d9d3 / e08f / 0443 / 0ee0`. Then per-class player-nudge + sub-spawn at L443-585 (uses `FUN_00432e50` collision + `FUN_0044ad81` spawn helper). Includes `FUN_00406551(0xb4, 1)` music start at L582-584. | unported |
| 21 | L588-687  | **Alt-stage arm** (taken when `DAT_068dd3fc[stage*0x6cf] < 0 || > 4`): writes `_DAT_073de39c = 0x40490fdb` (π) + `_DAT_056db060 = π` + a 14-iter loop reading per-stage offsets from `&DAT_005c5120/24`. Includes `FUN_004851e2()`. | unported |
| 22 | L689      | `_DAT_0438b4ac = _DAT_056db05c` — single-field copy                                       | unported (no consumer) |
| 23 | L690-700  | **The 200-iter spawn loop**: `if (*(int *)(DAT_068dd2f0 + 0x1b28) != 0) { for (i=200; i; --i) FUN_00447f4f(0, player_pos.x, player_pos.y+2, player_pos.z, 0x4f, 1.0f, 1); FUN_0040fb3a(); }` | **THIS CHIP** — Cf.1 |
| 24 | L701-705  | `FUN_0046f892()`, `FUN_00435612()`, conditional `FUN_004852fb()` gated on `*(DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c) == 0 && DAT_0438b928 == 1` (i.e. HOUSE *and* a UI mode flag) | unported |
| 25 | L706-707  | `FUN_0048439a()` + `FUN_00435fbb(0, 0xffffffff)`                                          | unported |

## Key data symbols touched

| Engine addr            | Meaning                              | Port-side name (if any) |
|------------------------|--------------------------------------|-------------------------|
| `DAT_068dd2f0` / +0x1b28 | Stage-palette pointer / spawn-loop gate | `g_stage_palette` (needs new field at +0x1b28) |
| `DAT_0438b4dc`         | Current stage index (palette selector) | `g_stage_palette` index — bare int unported |
| `DAT_0438b1e0`         | Current scene record selector         | unported |
| `_DAT_0438b1ec/f0/f4`  | Per-stage **default player spawn origin** (set by FUN_0044f13d to `(-40, 0, -60)`) | **new for Cf.1** |
| `DAT_056da1d8/dc/e0`   | Player[0] xyz pos                     | `g_scene1_player_pos[3]` ✓ (in `scene1_particles_tick.h`) |
| `DAT_056da1f0/f4/f8`   | Spawn-origin override                 | `g_scene1_spawn_origin[3]` ✓ |
| `DAT_056daabc/c0/c4`   | Player[0] xyz vel                     | unported (BSS-zero stand-in OK for MVP) |
| `DAT_056dab00`         | Player[0] action sentinel (=4 here)   | unported |
| `DAT_056dae18`         | Player[0] alpha (=1.0 here)           | unported |
| `_DAT_073de39c`        | Camera yaw                            | `g_scene1_camera_yaw` ✓ |
| `_DAT_056db05c`        | Camera-yaw-alt                        | `g_scene1_camera_yaw_alt` ✓ |
| `_DAT_073de3a0`        | Camera distance? (=0x42340000 = 45.0f) | unported |
| `DAT_068dd3fc`         | Per-stage selector at stride 0x6cf, drives mode/branch | unported |

## The 200-iter spawn loop verbatim

Line 690-700, the entire MVP scope:

```c
_DAT_0438b4ac = _DAT_056db05c;                          // L689 — single copy
if (*(int *)(DAT_068dd2f0 + 0x1b28) != 0) {             // L690 — stage gate
    iVar8 = 200;                                        // L691
    local_8 = DAT_056da1d8;                             // player_pos.x
    local_c = DAT_056da1e0;                             // player_pos.z
    local_10 = DAT_056da1dc + 2.0;                      // player_pos.y + 2.0
    do {
        FUN_00447f4f(0, local_8, local_10, local_c,
                     0x4f, 0x3f800000, 1);              // L696 — scene1_spawn type 0x4f, scale=1.0, count_index=1
        FUN_0040fb3a();                                 // L697 — scene1_particles_tick
        iVar8 = iVar8 + -1;
    } while (iVar8 != 0);
}
```

**What the loop does:** spawns 200 type-0x4f particles centered on
`(player.x, player.y+2.0, player.z)` with `scale=1.0` and `param_7=1` (the
spawn-count argument; for type 0x4f, that means 1 particle per call). Between
each spawn it advances the integrator one tick. The net effect: after the
loop, `g_scene1_records_a` has up to 200 type-0x4f records at ages 0 through
-199 (the integrator decrements age each tick — particles spawned earlier
in the loop have had more integration applied).

Type 0x4f is the constant-angle-velocity / inner-angle pos-jitter handler
ported in C8i.5c: `vel.x = mag_v (sin(π/2) = 1) × magnitude`, `vel.z = 0`
(cos(π/2) ≈ 0), `vel.y = -0.05 - u*0.05`, pos jitter via sin/cos × 8 with a
`u` consumed between the two trig calls, `pos -= vel*100` anchor-back, and
`PARAM2 = 100`.

## MVP scope (Cf.1)

Port **only block 23** (L690-700) plus the minimum block-11 pose copy that
the loop reads from.  Specifically:

1. **New stage-palette field at `+0x1b28`** — call it `ambient_spawn_flag`
   (`int32_t`).  Add the field, the `_Static_assert(offsetof(...))`, and
   the necessary `_pad_<from>` adjustment to keep the struct 0x1b3c bytes
   total.
2. **New stage globals `_DAT_0438b1ec/f0/f4`** — `g_scene1_stage_player_default_pos[3]`,
   defaults `(-40.0f, 0.0f, -60.0f)` per the engine `FUN_0044f13d` init
   (which we have NOT ported yet but have seen the literal in decompile).
   Document this as a **stand-in** (pending-human-check #9 candidate) since
   the real per-stage write-site is unported.
3. **`scene1_postload_pose_player()`** — does only the i=0 case of
   block-11: copies `g_scene1_stage_player_default_pos` into
   `g_scene1_player_pos`.  No multi-player loop, no spiral retry, no
   velocity zero (BSS already zero), no sentinel writes (no consumer).
4. **`scene1_postload_ambient_spawn()`** — does block-23.  Gate reads
   `g_stage_palette->ambient_spawn_flag`; loop body uses the existing
   `scene1_spawn` API (already exported) + `scene1_particles_tick`.
5. **Force-flag for tests + smoke** — a public `scene1_postload_force_ambient(int)`
   that sets `g_stage_palette->ambient_spawn_flag` directly; lets host
   tests + a future `--show-ambient-spawn` CLI flag exercise the loop
   without needing to discover the engine's per-stage write site.

## What this MVP intentionally defers

- **Spiral terrain-collision retry** (block 11 sub-body, L237-264).  Needs
  `FUN_00432e50` ported first — that's likely a tilemap/heightmap query
  for the scene-1 walkable region.  For HOUSE/MVP the player just lands
  at `(_DAT_0438b1ec, _DAT_0438b1f0, _DAT_0438b1f4)` directly.
- **Multi-player pose**: 3-iteration loop at L268-275 (player[0..2]).
  Single-player only for MVP.
- **All BSS scene-state resets** (blocks 3-7, 13-15, 18).  Our port keeps
  these globals BSS-zero anyway and no scene-transition loops back to
  state-1 yet, so the re-zero contract is provided "for free" by C runtime.
  When stage transitions land, each affected sub-module will need its own
  reset call — much cleaner than porting them as one giant blob.
- **All sub-init helpers** (block 17).  `FUN_0040f64b` ✓ (scene1_records_reset)
  is the only one that has a consumer today, and it's already called from
  `scene1_preload_house`.  No need to call it again here.
- **Per-stage-class init dispatch** (block 20).  Each of the 6 callees
  (`FUN_0044c88f / d1e4 / d9d3 / e08f / 0443 / 0ee0`) is its own multi-KiB
  init — they collectively define stage-class-specific entity placement
  and are months of work.
- **Alt-stage arm** (block 21).  Selector-conditioned; HOUSE doesn't hit
  it (selector value 0 falls inside `[0..4]`).
- **Music start + tail calls** (blocks 10, 24, 25).  All gated on stage
  globals that are BSS-zero in our port.

## Wiring recommendation

The engine call chain that drives FUN_00436f97 is **scene-1 INGAME state
entry from a sub-scene transition**.  We don't yet port the sub-scene state
machine (dungeon, dialog, worldmap return), so there's no engine-faithful
caller to hook in.  The options for Cf.1:

1. **Hook from `scene1_preload_house_cb`** after `scene1_records_reset(1)`.
   Engine-incorrect (HOUSE doesn't trigger this in the original; the gate
   field `+0x1b28` is BSS-zero for HOUSE anyway, so the spawn loop is
   guaranteed-dormant unless force-flagged via test API).  Pragmatic.
2. **Standalone host-test only** + a `--show-ambient-spawn` CLI flag for
   manual smoke.  No engine-flow wiring this chip.  Cleanest scope.
3. **New `scene1_postload_state1_enter()` function**.  Designed to be the
   landing pad for the eventual port of `FUN_0048526d` (the canonical
   "enter state-1" wrapper).  Today: called only from tests.

**Recommend option 1 + the test helper.**  Wiring from
`scene1_preload_house_cb` after `scene1_records_reset` makes the function
reachable from the live boot flow.  Since the gate stays BSS-zero on HOUSE
entry, this is observationally identical to "not wired" in the live build,
but means `--show-pass-f-test`-style toggles can exercise the populator
without rebuilding the wiring story.  Document the wiring as "stand-in
until FUN_0048526d / FUN_0049e163 ports".

## Pending-human-check items added

The Cf.1 port introduces one new candidate for the standard pending-human-check
queue (`openrecet_pending_human_checks.md`):

- **#9 — Stage default player pos defaults**.  `_DAT_0438b1ec/f0/f4`
  literals `0xc2200000 / 0 / 0xc2700000` ≈ `(-40.0f, 0.0f, -60.0f)` are
  read from `FUN_0044f13d:35-38` in the decompile.  Validate via Frida
  capture of the stage record after a fresh INGAME entry to confirm these
  are the actual engine values at the time FUN_00436f97 runs (not later
  per-stage overrides).

## Next chip after Cf.1

With the populator live, the next functional milestone is **wiring
`scene1_render_meshes` into `render_dispatch`** (see
`openrecet_scene1_render_ladder.md` "Iteration goal after C8h + C8i").  At
that point Pass D's mesh-draw path receives real records — and we will
finally see ambient particles in HOUSE if we manually toggle the gate (or
the gate writer ports).
