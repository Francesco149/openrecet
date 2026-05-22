# Title → NEW GAME fade-out

The black fade that plays after the player picks NEW GAME on the
title menu. This doc supersedes the "scene fade-out particle
animation" entry at the bottom of `PROGRESS.md` 2026-05-22 §"Scene-
state global + title fade-out counter" — that entry was based on a
wrong reading of the decompiled code; the real mechanism is much
simpler than what was filed as "deferred — big."

## Visible behavior

Captured from `vendor/unpacked/recettear.unpacked.exe` via Frida
under the `title-z-press` scenario. Frame numbers reference the
scenario's sim ticks:

- **Frame 44** (A pressed at frame 30, +14 select countdown):
  `fade_counter` (`DAT_0964351c`) latched to 1, title freezes.
- **Frames 44..72**: title renders normally, counter ticks 1..29,
  nothing visible changes. The "fade" hasn't started yet.
- **Frame 73**: counter reaches 30 → engine calls `FUN_004526f5(0,
  0x11)` (phase-1 fade init, duration 17), starting `DAT_0438bf78`
  from 1. First darkened frame.
- **Frames 73..89**: alpha quad ramps from ~17 to 255 over 17 sim
  ticks; the title progressively darkens to pure black.
- **Frame 90+**: alpha clamped at 255 → screen is solid black.
  "Now Loading..." text overlay (`FUN_00453147`) appears in the
  bottom-right; engine sits here until the worker thread completes
  scene init.

Measured retail mean-RGB-delta vs frame 50 reference (mid-freeze,
pre-fade):

| frame | predicted alpha | measured dmean |
|-------|-----------------|----------------|
| 73    | 34              | 28             |
| 80    | 153             | 124            |
| 85    | 239             | 193            |
| 90    | 255 (clamped)   | 205 (≈ full black) |

## Mechanism

A single 640×480 alpha-blended quad textured with a 6×6 patch of
`bmp/system.bmp`. The patch at `(9, 1)..(15, 7)` is pure black (we
extracted the asset and verified — see "Ghidra mis-decomp" below for
the source-rect coordinates). With SRCALPHA/INVSRCALPHA blending and
white vertex color (`color = 0xffffff | (alpha << 24)`), the result
is `output = title × (1 − alpha) + black × alpha` — i.e., the title
gets darkened toward black at the alpha given.

No off-screen render target. No back-buffer capture. No particles.
No worker thread spawned for the fade itself (the loading worker
thread *does* run, but only to gate the post-fade scene init — it's
not what produces the fade visual).

### Counter machinery

Five engine globals:

| global         | role |
|----------------|------|
| `DAT_0438bf78` | fade-quad counter; FUN_00453e8f reads as alpha |
| `DAT_0438bf7c` | phase flag: `1` = phase-in, `-1` = phase-out, `0` = idle |
| `DAT_0438bf80` | mode: `0` → src `(9,1)-(15,7)` = black, `1` → src `(1,1)-(7,7)` = white, `2` → special "no duration" mode (used elsewhere) |
| `DAT_005c5934` | duration target (e.g. `0x11 = 17` for NEW_GAME) |
| `DAT_06a48d6c..` + `DAT_06a4921c..` | per-particle x/y/z scratch tables — **dead writes**, see "Vestigial state" |

Functions:

- `FUN_004526f5(mode, duration)` — phase-1 init: sets
  `DAT_0438bf78=1`, `DAT_0438bf7c=1`, `DAT_0438bf80=mode`,
  `DAT_005c5934=duration`. Also writes the dead per-particle tables
  in a 30-tick pre-roll loop (vestigial).
- `FUN_0045281c(mode, duration)` — phase-1 (out) init: same but
  `DAT_0438bf7c=-1`, counter starts at 0 instead of 1, no particle
  pre-roll. Used for the fade-IN at scene-load completion.
- `FUN_004526ab()` — counter tick. Called once per sim tick from
  `FUN_004536cb`. Phase 1: increment, clamp at `duration+1`, stays
  there. Phase -1: increment, when past `duration` reset both to 0.
- `FUN_004528b3()` — done query. Phase 1 + mode==2 → done at
  counter==`0x1f`. Phase 1 + other mode → done at counter==duration.
  Returns 0 otherwise.
- `FUN_00453e8f()` — per-frame quad render. Called from
  `FUN_004547ab` (render dispatch). The actual alpha formula is
  documented below.

The title→NEW_GAME flow uses these from `FUN_0049a59e`:

```c
if (0 < DAT_0964351c) {
    DAT_0964351c = DAT_0964351c + 1;          // tick fade_counter
    if (DAT_0964351c == 0x1e) {                // first hit on frame 73
        FUN_004526f5(0, 0x11);                  // phase-1 black fade, dur 17
        DAT_0438b1e0 = 0;
        FUN_00435c98();                          // game-state reset for scene 1
    }
    if (DAT_0964351c < 0x1e || !FUN_004528b3())
        goto LAB_0049b415;                      // still rendering title
    // FUN_004528b3 returned 1 — fade complete, do the scene transition
    _DAT_0438b1e4 = 0;
    DAT_0438b1c0 = 8;                            // scene state → LOADING
    FUN_0049de18();
    DAT_0438b1c0 = 1;                            // → INGAME
    /* …massive scene-1 init…*/
}
```

### Ghidra mis-decomp of FUN_00453e8f

This is the part that misled the prior investigation. Ghidra
decompiles the alpha computation as:

```c
local_8 = (float)DAT_0438bf78;
iVar1 = __ftol();                  // ❌ wrong — this is what Ghidra produced
```

— suggesting `alpha = (int)counter`, max 17 ≈ 6.7% opacity. We
spent time looking for "the *real* fade pipeline" (off-screen render
target / DAT_06a4999c / FUN_00454191) because the visible retail
captures showed full opacity by frame 90 and the decompiler's
formula couldn't explain it.

The actual x86 at `0x453ed5..0x453f5b` (read with
`i686-w64-mingw32-objdump -d`) does:

```
; phase 1 branch:
mov   eax, [0x5c5934]      ; eax = duration
add   eax, -2              ; eax = duration - 2  (= 15 for NEW_GAME)
mov   [-0x4(ebp)], eax
fildl [-0x4(ebp)]          ; FPU: push (float)(duration-2)
fstps [-0x4(ebp)]
flds  [0x519390]           ; FPU: push 256.0  ← the missing multiplier
fdivs [-0x4(ebp)]          ; FPU: top = 256 / (duration-2)
fildl [0x438bf78]          ; FPU: push (float)counter
fstps [-0x4(ebp)]
fmuls [-0x4(ebp)]          ; FPU: top *= counter
call __ftol                ; iVar1 = (int)(256 * counter / (duration-2))
```

So the real formula is:

```c
alpha_phase1 = (int)(256.0 / (duration - 2) * counter);
alpha_phase_1_out = 255 - (int)(256.0 / (duration - 2) * (counter - 2));
clamp(alpha, 0, 255);
```

(`0x519390` = `0x43800000` = 256.0; `0x519630` = `0x437f0000` = 255.0.)

For NEW_GAME's `(0, 0x11)`: `256/15 ≈ 17.07` per counter step.
Counter ticks 1..18 → alpha hits 255 at counter ~15 → full black by
frame 88 (counter 15). Matches the measured retail data.

The mis-decomp loses one `flds [0x519390]` + `fdivs` pair on the way
to `__ftol`. The FPU stack discipline is subtle here (multiple
`fstps` that pop without obvious effect, plus the constant load
spliced between two unrelated `flds` of the same local) — easy to
trip a decompiler.

**Lesson for future RE on this codebase:** when a per-frame alpha
ramp looks "too subtle to be the visible effect," re-disasm the
function with objdump and look for unexplained `flds` of `.rdata`
constants. The engine uses lots of these on-the-fly constants for
animation math.

### Vestigial state

`FUN_004526f5`'s init also pre-rolls two 100-element float-vec
tables at `DAT_06a48d6c` and `DAT_06a4921c`:

```c
for each of 100 particles {
    pos.xyz = 0;
    tex.xy = (grid_x - 288, 216 - grid_y);  // 10x10 screen grid
    tex.z = 0;
    for (30 iterations) {
        pos -= rand_rot_table[particle];        // DAT_06a47130
        tex -= rand_pos_table[particle];        // DAT_06a475fc
    }
}
```

The `rand_rot_table` and `rand_pos_table` are populated at boot by
the existing `prewindow_init` port (FUN_00452569). After pre-roll
each particle's pos = `-30 × rand_rot`, tex = `(grid, 0) - 30 ×
rand_pos`.

**Nothing in the binary reads these tables.** Verified via
`i686-w64-mingw32-objdump -d | grep 0x6a48d6c` — the only references
are FUN_004526f5's writes and FUN_0045281c's zeroing-writes, plus
one unrelated sentinel comparison in FUN_004518a3 (debug text
walker — uses the address as a loop limit, never reads the content).

The math hints at a former 100-particle fade animation that was
either cut from the game during development, or replaced with the
simple alpha quad we see today. The init code stuck around because
removing it would also require removing the prewindow random-vec
generation that consumes RNG state in a specific order, and the
engine's boot RNG is observably deterministic against the .data
seed — touching either would have ripple effects on every test save
file from the 2007 dev cycle.

If a future port wants to skip the vestigial pre-roll, the only
faithfulness concern is the 30-iteration float subtraction loop —
it touches no other globals, so omitting it is a no-op visually
*and* in any future test that observes `DAT_06a48d6c` (none today).

## Files referenced

| path | role |
|------|------|
| `docs/decompiled/by-address/4526f5.c` | FUN_004526f5 phase-1 init (with vestigial particle pre-roll) |
| `docs/decompiled/by-address/45281c.c` | FUN_0045281c phase-1 (out) init |
| `docs/decompiled/by-address/4526ab.c` | FUN_004526ab counter tick |
| `docs/decompiled/by-address/4528b3.c` | FUN_004528b3 done query |
| `docs/decompiled/by-address/453e8f.c` | FUN_00453e8f alpha-quad render — **mis-decomp; consult objdump for the multiplier** |
| `docs/decompiled/by-address/49a59e.c` (L53-77) | FUN_0049a59e NEW_GAME branch — calls FUN_004526f5 at fade_counter==30 |
| `docs/decompiled/by-address/453147.c` | FUN_00453147 "Now Loading..." text overlay (worker-thread-flag-gated) |
| `vendor/unpacked/system.bmp` after `tools/extract/data-bin.py` | (9,1)-(15,7) = pure black; (1,1)-(7,7) = pure white |

## Port plan

When picked up, the actual scope is small:

1. Counter + phase machinery (5 globals + 4 fns) — pure C, no D3D.
2. FUN_00453e8f alpha-quad render — uses existing `render_quad_add`
   path; just needs a `&DAT_073cb900`-equivalent metadata struct
   for system.bmp's `{w=128, h=128}` and the correct alpha formula.
3. Wire FUN_004526f5 trigger at `scene_title_sim`'s fade_counter==30
   site (currently a no-op increment).
4. Wire FUN_004526ab in sim_step_a (after the other per-tick hooks).
5. Wire FUN_00453e8f in the render dispatch after scene render.
6. Optional follow-up: FUN_00453147 "Now Loading..." overlay +
   replace main.c snap-back with hold-on-black until destination
   scene lands.

Worker thread (`FUN_00452cde` / `LAB_0045293d`) **NOT NEEDED** for
this scope — that's a scene-init dispatcher for *other* transitions
(NPC enter/exit, ESC-to-title, etc.) and doesn't fire in the title→
new-game flow.

The off-screen render target system (`DAT_06a4999c` + FUN_00454191)
also **NOT NEEDED** — that's a separate fade pipeline for in-game
scene-to-scene transitions, triggered exclusively by FUN_00453384
which has no caller in the title→new-game path.
