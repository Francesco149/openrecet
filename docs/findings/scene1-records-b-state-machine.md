# Scene-1 table B per-record state machine — FUN_0043865e

**Status (2026-05-25):** survey only.  No port yet.  Documents the
8059 B Mt. Everest #2 at `0x43865e` invoked 73 times from inside the
table-B integrator FUN_0043ae20 (see `scene1-records-b-tick.md`).
This is the **combat system core**: it scans NPCs near the slot's
position, polls attack-button input, rolls damage, applies knockback,
and emits hit particles + SE.

## TL;DR

| Item | Value |
|------|-------|
| Engine address | `FUN_0043865e @ 0x43865e` |
| Size | 8059 B (≈909 decomp lines, `docs/decompiled/all.c` L35128..L36036) |
| Decomp signature | `undefined4 FUN_0043865e(int *param_1)` — single arg = `&slot[0]` |
| Return contract | `0 = no interaction`, `1 = hit fired (damage applied)`, `2 = full cleanup + slot kill` |
| Callers | 73 sites inside FUN_0043ae20 (table-B integrator) — wired today via PHC #20 stub hook (`state_machine_call_ret`) |
| Labels | 12 (LAB_00438805 / e6d / 90d3 / 949c / 94e8 / 97b0 / 9909 / 99cd / 9a48 / 9f28 / a1fd / a491) |
| Returns | 22 sites (0×6, 1×13, 2×3) |
| Major external calls | scene1_spawn ×16, input_held ×10, rng_next15 ×9, SE_play ×7, FUN_00482a51 ×7, sqrtf ×5, atan2f ×4, FUN_0041f46d ×4, FUN_00485712 ×2, FUN_00485c74 ×1, FUN_004412b6 ×2, FUN_00484e45 ×1, FUN_0049933c ×1, FUN_0048a348 ×1, FUN_0043824b ×2, FUN_0042e791 ×1, FUN_00485413 ×1, scene1_pfo_table_a_alloc ×1, scene1_overlay_spawn ×1, FUN_0044b16c ×1, FUN_004319d6 ×1, FUN_00451874 ×1 (text "ANG: %f" overlay — dev/debug) |
| Globals read/written | DAT_0438be98/9c/a0, DAT_0438b1c8 (entry gates) + DAT_06a46f98 (per-tick flag, PHC #21 ✓) + DAT_06a46f94 (damage out) + DAT_056daabc/c0/c4 (knockback vec) + DAT_056daeb0/dab04 (knockback type) + DAT_056db0bc (player HP float) + DAT_056db0fc / db108 / db00c / db004 / db008 (timers) + DAT_0438bed8 (post-hit flag) + DAT_005c23f0/2434/243c/2440 (per-TYPE physical constants, stride 0x68) + DAT_0076c478 (people-table base, sister of FUN_0043ab6e) + DAT_0695f004..069b3004 (projectile/aura table, stride 0xa8 dw) + DAT_073e03b4 (combat-table?) + DAT_005c4c9c/4ca0 (per-projectile-type physical constants, stride 0x24) + DAT_0438b7d8 (game-mode) + DAT_056dae30/38 (secondary state) + DAT_005c5938 + DAT_044e3798 (per-character record base, stride 0x2dfc8) |

## Caller contract

Every call site in `FUN_0043ae20` follows the pattern:

```c
iVar8 = FUN_0043865e(piVar14);  /* piVar14 = &slot[0] */
if (iVar8 != 0) {
  /* per-body cleanup, possibly damage-write to owner, possibly kill */
  goto LAB_004411e3;  /* or fall through */
}
```

So the SM's `ret` is consumed by the per-TYPE body in the integrator
(`scene1_records_b_tick`) to decide whether the slot:
- did nothing visible (ret=0 → continue body),
- fired an attack hit (ret=1 → damage-write to owner_a+0xe30/+0xe38),
- needs cleanup (ret=2 → kill).

The function also has side effects via globals (knockback vector,
damage output, post-hit timers) that downstream rendering / HUD code
reads.

## Phase structure

The function is a single linear body with conditional gotos.  No
deep switch dispatch.  Four major phases:

### Phase A — Entry gates (L35171-L35185, 14 lines)

```c
local_10 = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8;  /* per-character record base */
if (0 < DAT_0438be98) return 0;
if (0 < DAT_0438be9c) return 0;
if (0 < DAT_0438bea0) return 0;
if (DAT_0438b1c8 != 0) return 0;
DAT_06a46f98 = 1;                                   /* per-tick flag — PHC #21 resolved */
```

The 4 entry gates are game-state flags.  Most likely:
- `DAT_0438be98` — "scene paused" or "game over"
- `DAT_0438be9c` — "hitstop frame" (set by Phase D when player is hit)
- `DAT_0438bea0` — sibling pause flag
- `DAT_0438b1c8` — "input disabled / cutscene"

Effect of returning 0 from here: every integrator body that polls the
SM sees ret=0, so no slots ever apply damage or kill themselves
through the SM cascade.  Useful "frozen world" smoke flag if we
expose these.

### Phase B — Attacker scan via people-table (L35186-L35608, ~422 lines)

**Gate:** `(slot[1] in {0, 3}) && (DAT_056db0bc > 0.0)`
- `slot[1]` is the per-record role/state field; observed values across
  the binary: 0, 1, 2, 3 (likely state-machine state index — combat
  phase: 0=idle, 1=attacking, 2=knocked-back, 3=hit-recovery).
- `DAT_056db0bc` is **player HP** (float).  Set by FUN_0044b16c (in
  this function!) and read all over the combat path.  When HP ≤ 0
  the entire attacker scan is skipped — dead actors can't attack.

**Loop:** iterates `local_30 = &DAT_0076c478` (people-table NPC base)
at stride 0x2c2 dw = 0x2c2*4 = 0xb08 bytes per record (matches
people-table stride from prior survey work).  For each NPC record:
1. **Skip gates:**
   - `npc[-5] >= 1` → skip (some "live" gate at NPC offset -5)
   - `npc[0] != 0` → skip (NPC type-0 sentinel)
   - if `slot[1] == 3 && slot[5] != NULL && slot[5] != &npc[-0x2c2]`
     → skip (current target lock)
   - `npc[-0x1b8]` ∉ {1, 2} OR `npc[24] == 0` → skip (NPC state gate)
2. **Subtype filter (slot[0x47] vs NPC type ID list):**
   - Linear scan of 10 entries at `npc[0x15..0x1e]` — checks the slot's
     `target_id` (slot[0x47]) against a per-NPC 10-element ID list.
     If not found → skip.
3. **Per-NPC-type sub-iter count:**
   - NPC TYPE 0x44/0x45 → 7 sub-iters (multi-hit attacks)
   - NPC TYPE 0x46/0x47 → 2 sub-iters (paired-hit attacks)
   - Other → 1 sub-iter
4. **Per-sub-iter distance check + damage roll:**
   - Read NPC pose (`npc[-0x1c9..-0x1c7]` = pos.x/y/z;  `npc[-0x13]` =
     attack radius scalar)
   - For sub-iters > 0 OR NPC TYPE 0x46/0x47 → use auxiliary attack-anchor
     pose (`DAT_005c530c[]` / `&DAT_005c5314[]` index into NPC vertex
     table at `npc-0x2c2+iVar8*3+0x1bf`)
   - Compute `dx = npc_pos.x - slot[0x17]`, `dz = npc_pos.z - slot[0x19]`,
     `dist = sqrt(dx² + dz²)`
   - Attack-distance gate:
     `dist - slot[0x2a] < npc[-0x13] * dat_005c2440[type] * dat_005c2434[type]`
   - Y-band gate:
     `|dy - slot[0x2a]*0.8| < npc[-0x13] * dat_005c243c[type] * dat_005c2434[type]`
5. **Attack-angle math:**
   - For NPC TYPE 0x44/0x45 (special): `atan2(dz, dx)` minus NPC facing
     `npc[-0x1ba]` plus π normalized into [-π, π]; if |angle| ≥
     0.9424779 → skip (facing wrong way).  Also calls `FUN_005038ff` +
     `FUN_00451874(0x1e, 0x21, ...)` to display debug text "ANG: %f"
     (dev/QA leftover).
6. **Hit registration:**
   - Bump NPC's hit-history ring buffer: `npc[npc[0x1f] + 0x15] = slot[0x47];
     npc[0x1f] = (npc[0x1f] + 1) % 10`
7. **Type-0x53 special:** if slot TYPE is 0x53 (sword/strong-attack)
   AND `dat_005c23f0[npc_type].byte0x20 == 0` AND NPC type != 0x22:
   - Call `FUN_004319d6()` (stage-transition gate, PHC #24 hook installed)
     → return value gates kill_age in {0x78, 600}
   - Set `slot[4] = kill_age - slot[0x26]` (re-target some timer; 0-floor)
   - Set `DAT_0438bed8 = 4`
8. **General damage flow** (slot TYPE != 0x53):
   - Player branch (`slot[1] == 0`): read `DAT_056db0b4` (base damage),
     ftol → int
   - Other branch: read per-NPC-type damage curve from
     `*(int *)(&DAT_005c23f0[owner_type * 0x68].field_0x3c)` * `FUN_0041f46d(?)`
   - Apply modifiers via `FUN_0043647f(<button_id>)` — 4 input polls!
     Each held button doubles/halves damage (multi-button combo system).
   - Apply per-knockback-state modifier (`slot[7..0xc]` flags)
   - Charge-attack bonus: `slot[0x25] != 0 && slot[4] == 0` → angle-check
     into player facing → set hit charge bit + `FUN_00482a51(npc, 4)`
   - Set damage output `param_1 = (int *)__ftol()`
   - Side-effect: angle to NPC computed via atan2 + clamped quadrant
     bits OR'd into `local_1c` (knockback direction bits)
9. **Hit clamp + early-out path** (LAB_004390d3): if angle out-of-range
   → param_1 = 0 (no damage), but still emits hit-particle.

**Returns from Phase B:** ret=1 sites are scattered across the per-NPC
type handler (L35603+).  Fall-through to LAB_00439f28 (Phase C) if no
NPC matches.

### Phase C — Incoming-hit scan via projectile table (LAB_00439f28, L35613-L35773, ~160 lines)

**Gate (negated):** `slot[1] != 1 && slot[1] != 3`
- i.e. slot is in "idle" or "knocked-back" state — *susceptible to
  incoming hits*.

**Loop:** iterates `piVar11 = &DAT_0695f004` at stride 0xa8 dw = 0x2a0 bytes
per record, terminating at `&DAT_069b3004`.  Total records:
`(0x69b3004 - 0x695f004) / 0x2a0 = 0x54000 / 0x2a0 = 0xD2 = 210` projectiles.
This is the **projectile/aura table** — different from table B (slots
for spawned bodies) and table C (item drops).  Likely "active attack
effects" emitted by NPCs (sword swings, magic projectiles, AOE
hitboxes).

For each projectile:
1. **Skip gates:** projectile TYPE field at `proj[0]` checked against
   {-1, 9, 10, 0x12, 0x13, 0xc, 0xd, 0xb, 8} → skip; also `proj[0x77]
   in {3, 7}` → skip.
2. **Subtype filter:** same 10-entry list as Phase B at `proj[0x79..0x82]`.
3. **Distance/AABB check:** uses per-projectile-type constants from
   `DAT_005c4c9c[type*0x24]` (x-radius) and `DAT_005c4ca0[type*0x24]`
   (z-radius) scaled by `proj[-3]` (scale field at byte +0x14 from
   record base).
4. **On hit:**
   - Bump hit-history ring: `proj[proj[0x83] + 0x79] = slot[0x47];
     proj[0x83] = (proj[0x83] + 1) % 10`
   - Set `proj[-0x1a] = 5` (projectile state→active-hit)
   - For slot TYPE in {2, 0x54, 0x6d, 0x6f, 0x70}: OR `_DAT_056da1b8 |= 2`
     (sound flag)
   - Branch on `proj[0]` (projectile type):
     - 2 or 3: emit hit-particle 0x15 at midpoint + SE 0x159
     - 0: emit hit-particle 0x16
     - 15 (0xf): RNG-driven 5-shot scatter via FUN_00471089 + 0x15 spawns
     - 4 / 5 / 8: scene-state-gated combat decision via FUN_004412b6 +
       FUN_0048a348 + FUN_0043824b cascade (LAB_0043a491 path)
     - 6: just write `proj[0x77] = 1`
     - default: `proj[0x77] = 1` + emit hit-particle type 2

### Phase D — Player-distance + per-TYPE damage (LAB_0043a491 fall-through, L35774-L35936, ~163 lines)

Reached when slot[1] in {1, 2} (knocked-back / hit-recovery states).

```c
dx = DAT_056da1d8 - slot[0x17];  /* player.x - slot.x */
fVar3 = DAT_056da1dc - slot[0x18];  /* player.y - slot.y */
dz = DAT_056da1e0 - slot[0x19];  /* player.z - slot.z */
if (dx == 0 && dz == 0) dz = 0.01;
dist = sqrt(dx² + dz²);
local_2c = dist - slot[0x2a];  /* dist - reach */
```

`DAT_056da1d8/dc/e0` is the **player position** (engine FUN_00436f97
writes these every frame, currently still unported — see PHC #9).

Per-TYPE specialization (L680+):
- **TYPE 0x53** (heavy sword): if dist ≥ 2 → return 0; else
  AABB-on-Y reach → `DAT_056daabc=0; DAT_056daac4=0; return 1`
- **TYPE 0x27** (instant action): multiple return-1 branches with
  `DAT_056db108 = 500; DAT_056db10c = slot[2]` (timer + ID latch)
- **TYPE 0x24**: special LAB_004397b0 path with scale 1.0 / scale 0.3
- **TYPE 0x56 / 0x96** (ground-bounce): SE play (no arg shown — but
  rdata implies 0x158 or 0x168 like C8j-tick.15i); also writes
  knockback negatives: `slot[0x1a] = -0.2*local_18*local_c;
  slot[0x1c] = -0.2*local_18*local_8`
- **TYPE 0x1f / 0x51 / 0x6c** (special game-mode-gated): SE play +
  set local_1c flag
- **TYPE 0x11**: `if (DAT_056db0fc < 0x78) DAT_056db0fc = 0x78` (knockback
  cooldown clamp)
- **TYPE 0xa0**: full reset — `DAT_056daeb0=0; DAT_0438bed8=2;
  DAT_056db048=1; DAT_056db0f4=4; FUN_00482a51; DAT_056daabc=dx*0.1;
  DAT_056daac4=dz*0.1` (light push)
- **TYPE 0x13**: spawn particle 0x75 + write `slot[5]+0xa54 = 1`
  (owner blob field, likely "pickup-grabbed" flag)
- **TYPE in {0xe, 0x12, 0xa1}**: `DAT_056daeb0 = 0x46` (knockback type)
- **General fall-through:** `DAT_056daeb0 = 0x1e` (default) +
  `DAT_056daabc = local_18*local_c; DAT_056daac0 = 0x3ecccccd (=0.4);
  DAT_056daac4 = local_18*local_8`

**Hit-particle emission + return:**
- `local_1c == 0` (no knockback override): emit hit-particles via
  `FUN_0044b16c(player_pos, damage)` + SE + scene1_spawn(1, ...,
  scale=0.2) + scene1_spawn(0x19, ...) → return 1
- `local_1c == 2.8026e-45` (2 in float bits = type-specific knockback):
  `*piVar1 = 0;  return 2;` — **slot self-kills inside the SM!**
- else (other knockback): scene1_spawn(1, ..., scale=0.1) +
  scene1_spawn(0x19, ...) → return 2

## Helpers — port surfaces (host-installable hooks recommended)

| Helper | Size | Purpose | Port plan |
|--------|------|---------|-----------|
| FUN_0043647f | 61 B | `combo_held(button_id)` — scans `DAT_0438b93c[0..DAT_0438b938]` for matching button id.  Used 10× for combo damage modifiers. | Stand-in: `g_combat_button_held(int btn) → int`, default 0.  Drives the entire damage-multiplier chain in Phase B. |
| FUN_0041f46d | 57 B | `rng_damage_scale(arg)` — `1.0 + FUN_0041f319(arg) * (0.1 or 0.12)`.  Used 4× to add random spread to damage. | Stand-in: `g_combat_rng_damage_scale(int arg) → float`, default 1.0 (deterministic).  Tests inject. |
| FUN_00485712 | 317 B | `damage_apply(category, arg2, arg3)` — likely applies damage to an HP bar.  Called with literals `(0x9c7, 10, 5)` and `(arg, ?, ?)`. | Stand-in: `g_combat_damage_apply(int, int, int)`, default no-op observable counter. |
| FUN_00485c74 | 333 B | `enemy_damage_emit(damage_int)` — emits the damage number popup at NPC.  Last arg from Phase B's damage calc. | Stand-in: `g_combat_damage_emit(int)`, default no-op observable counter. |
| FUN_004412b6 | 2037 B | combat-action decision (1-arg).  Returns 1 to gate a "special action" branch (likely "throw/grab" mechanic when player TYPE in {4, 5, 8}). | Stand-in hook with default returns 0 (skip special). |
| FUN_00484e45 | 82 B | unknown — gates the cVar2 branch in LAB_0043a491.  Returns 0/non-zero. | Stand-in returns 0 (default-skip path). |
| FUN_00485413 | 55 B | `hud_effect_play(arg1..arg5)` — 5-arg setup.  Used once with `(1, 0, 2, 0, 0xb4)`. | Stand-in no-op observable. |
| FUN_0049933c | 439 B | `FUN_0049933c()` — used in RNG-3-bucket dispatch (Phase C "type 4/5/8" sub-arm). | Stand-in no-op. |
| FUN_0048a348 | 59 B | `FUN_0048a348(&str, 0)` — small wrapper.  Plays a particular event (string literal `&DAT_005c54f8`). | Stand-in no-op. |
| FUN_0043824b | 940 B | combat sub-action — Phase C "type 4/5/8" special.  Returns 0/1. | Stand-in returns 0. |
| FUN_0042e791 | 676 B | unknown — used once in Phase B for type-0x44/0x45 special path. | Stand-in no-op. |
| FUN_0044b16c | unknown | `damage_popup_at_pos(x, y, z, damage_int)` — emits floating damage text. | Stand-in no-op observable. |
| FUN_00451874 | unknown | `debug_text_overlay(x, y, fmt_buf)` — dev/QA text (calls sprintf to "ANG: %f").  Engine quirk — observable but not gameplay-affecting. | Stand-in no-op. |
| FUN_005038ff | unknown | `sprintf` — for the "ANG: %f" debug overlay. | Already named in port. |
| FUN_004319d6 | 170 B | stage-transition gate (PHC #24). | Already hook in port — `scene1_records_b_set_aux_4319d6_hook`. |

Plus already-ported callees: `sinf`, `cosf`, `sqrtf` (FUN_005031e4), `atan2f` (FUN_00503dd0), `rng_next15` (FUN_005041f6), `FUN_00471089` (rng_next_unit), `FUN_00447f4f` (scene1_spawn), `FUN_004147d5` (scene1_overlay_spawn), `FUN_0041331d` (scene1_pfo_table_a_alloc_passthrough), `FUN_00499519` (se_play), `FUN_00482a51` (aux_482a51 — already host-installable hook from C8j-tick.5).

## Globals — port surfaces

### Combat-state globals (all unported — port as a single
`scene1_combat_state` struct)

| Engine global | Size | Role | Notes |
|---------------|------|------|-------|
| DAT_0438be98 | int | entry gate — "scene paused/game over" | written elsewhere (174 refs across binary); probably set by FUN_0044b16c at HP=0 (L880) |
| DAT_0438be9c | int | entry gate — "hitstop frame" | set by Phase C type-4/5/8 path (L635) |
| DAT_0438bea0 | int | entry gate — sibling pause | unknown writer |
| DAT_0438b1c8 | int | entry gate — cutscene/input-lock | unknown writer |
| DAT_06a46f98 | int | per-tick flag (cleared by integrator at slot iter top, set by SM) | **PHC #21 RESOLVED** — writer found |
| DAT_06a46f94 | int | last-tick damage out | read by integrator type-4 sub-arm (FLAG_B<0 anim drive) |
| DAT_056da1d8/dc/e0 | float×3 | **player position** | written by FUN_00436f97 (PHC #9 — still unported) |
| DAT_056daabc/c0/c4 | float×3 | **knockback velocity vector** | applied by some downstream consumer (player.tick?) |
| DAT_056daeb0 | int | knockback type (0=none, 0x1e=default, 0x46=heavy, 0x4e=block) | per-TYPE table |
| DAT_056dab04 | int | post-hit state code (4 or 6) | |
| DAT_056db0bc | float | **player HP** | float; written via FUN_0044b16c reads/writes |
| DAT_056db0fc | int | knockback cooldown | |
| DAT_056db108 / db10c | int×2 | "active item" timer + ID | TYPE 0x27 latches |
| DAT_056db004 / db008 | int×2 | combat sub-state | Phase C type-4/5/8 |
| DAT_056db00c | int | RNG cooldown timer (= 0x3c on certain hit) | |
| DAT_056db00b0 / b4 / b8 | float×3 | base damage values per state | |
| DAT_056db014 / 01c | int×2 | scene-gate damage multipliers | |
| DAT_056db048 | int | knockback-active flag | |
| DAT_056db0f4 | int | hitstop ticks | |
| DAT_056dae9c / ea0 / ea4 | int×3 | secondary combat flags | |
| DAT_056dae30 / 38 | int×2 | scene-progression counters | |
| DAT_0438bed8 | int | "post-hit pose lock" (2 or 4) | |
| DAT_0438b7d8 | int | game-mode (5 = combat mode) | |
| DAT_005c23f0 / 2434 / 243c / 2440 | float per-TYPE | **per-TYPE damage table** (stride 0x68, per offset) | NB: same table as PHC #19's `DAT_005c2400`! Different fields of same struct. |
| DAT_005c4c9c / 4ca0 | float per-projectile-type | per-projectile-type AABB radii (stride 0x24) | unwriters in binary — likely .rdata table |
| DAT_005c5938 | int | combat-mode latch | |
| DAT_0076c478 | NPC[] | people-table base (1828 B stride 0x2c2 dw) | already known from sister-search PHC #6 |
| DAT_0695f004 / 069b3004 | proj[] | **projectile/aura table** (0xa8 dw stride, 210 records) | **new finding** — separate from tables A/B/C; consumer for active hitboxes emitted by sword swings / spells |
| DAT_044e3798 | char[0x2dfc8] | per-character record base | indexed by `DAT_0438b1e0` (current char id) |
| DAT_005c530c / 5314 | int[] | per-NPC-attack-anchor index tables | for NPC TYPE 0x44/0x45/0x46/0x47 multi-hit subtype |
| DAT_073e03b4 | int[] | combat sub-table for Phase C "type 5" decision | (iVar8 * 0xf + local_14) * 4 indexed |

### Slot fields read

Position/orientation (matches integrator):
- `slot[0x17/18/19]` = POS_X/Y/Z
- `slot[0x1a]` = ROT_X (also VEL_X write site for 0x56/0x96)
- `slot[0x1c]` = VEL_X (or aliased)
- `slot[0x2a]` = **reach radius** (NEW field — not used by C8j-tick bodies)
- `slot[0x47]` = **slot ID / target_id** (NEW field — used by NPC sub-filter)
- `slot[1]` = **combat state** (0=idle, 1=attacking, 2=KB, 3=hit-recovery)
- `slot[2]` = ID latch (used by 0x27)
- `slot[3]` = combat ID
- `slot[4]` = `kill_age` countdown (rewritten by 0x53 special)
- `slot[5]` = OWNER_PTR alias (matches integrator)
- `slot[7..0xc]` = per-state damage modifiers (read by Phase B damage roll)
- `slot[0x25]` = charge-attack bit
- `slot[0x26]` = age (matches integrator AGE)
- `slot[0x2e]` = `is_player` flag (used by player branch)

### Slot fields written

- `slot[0x1a]`, `slot[0x1c]` — knockback nudge in Phase D 0x56/0x96
  arm
- `slot[5]+0xa54` — owner blob "pickup-grabbed" (TYPE 0x13)
- `*slot` = 0 — self-kill in ret=2 Phase D path

### NPC record fields touched

- `npc[-0x1c9..-0x1c7]` = NPC pose
- `npc[-0x1ba]` = NPC facing yaw
- `npc[-0x1b9]` = NPC TYPE
- `npc[-0x1b8]` = NPC state (1 or 2 = "alive/hittable")
- `npc[-5]` = aliveness gate
- `npc[-0x13]` = attack radius scalar
- `npc[-0x2d]` = combat phase
- `npc[-0x2c..-0x2a]` = combat phase counters
- `npc[0x15..0x1e]` = 10-entry hit-history ring
- `npc[0x1f]` = ring write cursor
- `npc[0x24]` = aliveness alias
- `npc[0xc]` = block/dodge counter
- `npc[0x25]` = charge-attack flag
- `npc[4]` = countdown timer (set by 0x53 special)

### Projectile record fields touched

- `proj[0]` = projectile TYPE
- `proj[-0x23..-0x21]` = pose
- `proj[-3]` = scale
- `proj[-2]` = something (used in 0x15 emit)
- `proj[-0x1a]` = projectile state
- `proj[0x76]` = lifetime countdown
- `proj[0x77]` = transition state (0/1/2)
- `proj[0x79..0x82]` = 10-entry hit-history ring
- `proj[0x83]` = ring cursor
- `proj[1]` = "first hit" latch

## Why this is huge

The integrator's 73 SM calls fall into roughly 3 groups:
1. **Attackers** (slot[1] in {0,3}) — usually the **player slot** and
   sword/projectile bodies emitted by the player.  These call SM to
   *check if they hit any NPC*.
2. **Targets** (slot[1] in {1, 2}) — knocked-back / hit-recovery NPCs.
   These call SM to *check if they got hit again by an active
   projectile or by the player directly*.
3. **Self-cleanup** (TYPE 0x53/0x27/0x24/0x56/0x96/etc.) — bodies that
   call SM to *check if they should fire their type-specific damage
   formula on the player*.

So FUN_0043865e implements:
- The player→NPC attack collision check (Phase B)
- The NPC→player projectile hit-check (Phase C)
- The body→player distance-damage formula (Phase D)

This is the **entire combat hitbox + damage pipeline**.  Without it:
- Player swings deal no damage; sword animations play but enemies
  ignore the slot.
- NPC projectiles fly past the player; auras don't drain HP.
- Pickup bodies (TYPE 0x13) don't grab on touch.

With it ported, the player-vs-NPC combat loop in dungeon scenes
becomes functional even before any per-TYPE polish lands.

## Sub-chip ladder proposal (C8jb.* family)

Mirroring the C8j-tick.* approach (~16 sub-chips for the 25.7 KB
integrator), FUN_0043865e at 8 KB needs ~10-12 sub-chips.  Suggested
decomposition:

| Sub-chip | Scope | Eng lines | Est LoC | Surface |
|----------|-------|-----------|---------|---------|
| C8jb.0 (this doc) | Survey | n/a | 0 | none |
| C8jb.1 | Skeleton: Phase A entry gates + DAT_06a46f98=1 + ret=0 default; replaces `state_machine_call_ret` PHC #20 stand-in. | L35171-L35185 | ~60 | per-tick flag now set; bodies that gate on `DAT_06a46f94 != 0` (anim drive type 4) now see the real flag |
| C8jb.2 | Phase B head: attacker NPC scan iteration shell + early skip gates (NPC live + state filters + slot[0x47] subtype filter).  No collision logic yet — every iter falls through to next NPC.  Stub `g_combat_button_held` + `g_combat_rng_damage_scale` hooks. | L35186-L35226 | ~150 | iter count observable via test hook |
| C8jb.3 | Phase B collision math: distance check + AABB Y-band + per-NPC-type subtype anchor (DAT_005c530c/5314 lookup for 0x44-0x47 multi-hit). | L35227-L35260 | ~150 | hits now detectable via observable counter; no damage yet |
| C8jb.4 | Phase B attack-angle filter: atan2-based facing-check for 0x44/0x45.  No damage emit yet (dev "ANG: %f" text overlay omitted as no-op observable). | L35261-L35275 | ~80 | NPC 0x44/0x45 type tests pass |
| C8jb.5 | Phase B damage roll: hit-history ring bump + per-state damage formula + 4× FUN_0043647f input-poll modifiers + charge bit + LAB_004390d3 angle-clamp. | L35276-L35430 | ~250 | damage int computed; observable via hook |
| C8jb.6 | Phase B hit-effect emit: spawn-hit particles + SE + FUN_00485712/c74 hooks + return 1.  | L35431-L35608 | ~200 | NPC takes damage in test fixture |
| C8jb.7 | Phase C projectile-table scan shell + skip gates + AABB check.  | L35613-L35660 | ~150 | per-projectile-type hits detectable |
| C8jb.8 | Phase C hit branches: TYPE 2/3/0/15/4/5/6/8 per-projectile-type effects (hit-particles, SE, FUN_004412b6/0048a348/0049933c hooks).  | L35661-L35773 | ~250 | all projectile-type hit effects |
| C8jb.9 | Phase D head: player-distance check + early-TYPE arms (0x53 dist-gate, 0x27 timer-set, 0x24 special LAB_004397b0). | L35775-L35820 | ~150 | TYPE 0x53/0x27/0x24 player-distance hits |
| C8jb.10 | Phase D knockback type latches: TYPE 0x56/0x96 (ground-bounce + slot[0x1a/0x1c] = -0.2*vec), 0x1f/0x51/0x6c (SE+flag), 0x11 (DAT_056db0fc clamp). | L35820-L35870 | ~150 | TYPE 0x56/0x96/0x1f/0x51/0x6c/0x11 fully ported |
| C8jb.11 | Phase D damage emit + knockback vector writes (DAT_056daabc/c0/c4) + 0xa0 / 0xe / 0x12 / 0xa1 / 0x13 specials + final ret 0/1/2 dispatch. | L35870-L36036 | ~200 | full combat ret contract; integrator bodies see real ret=1 + ret=2 + slot kill |
| **C8jb.fin** | Wire over PHC #20 hook: replace `scene1_records_b_set_state_machine_hook` default with `scene1_combat_state_machine_tick`.  Verify canaries bit-exact (HOUSE should be unaffected — table B BSS-zero → no slots → no SM calls). | n/a | ~20 | combat hooks live |

**Total estimate:** ~10-12 sub-chips × ~150-250 LoC each = ~2 KLoC port for
8 KB engine.  Smaller than C8j-tick (~5 KLoC for 25 KB) since the SM
is a single linear body, not a 86-way dispatch.

## Helpers — parallel sub-ladders

Most helpers can stay as host-installable hooks initially (no-op
defaults).  The two priority ports for visible-pixel value:
1. **FUN_00485712 + FUN_00485c74** (damage apply + damage popup)
   — once ported, hits actually drain NPC HP + show numbers.
   Estimated ~300 LoC each (small standalone bodies).
2. **FUN_0044b16c** (damage popup at world pos) — small, paired with
   #1.  ~100 LoC.

The big helpers (FUN_004412b6 at 2037 B = combat-action decision) can
defer to a parallel C8jc.* ladder.

## Risk + open questions

### Q1 — Is `slot[1]` actually a state-machine state index?

Decomp shows values {0, 1, 2, 3} read.  C8j allocators write
`slot[1]=0` on most TYPEs.  Some integrator bodies write
`slot[1] = ?` but the pattern is type-specific.  Need to inventory
ALL writers of `slot[1]` to confirm the {0, 1, 2, 3} convention.

**Resolution path:** grep all callees + scene1_record_b_spawn for
slot[1] writes before C8jb.1 lands.

### Q2 — Is `DAT_056db0bc` actually player HP?

Strongly implied by:
- It's a float (not int) — HP is a float in many JRPGs.
- It's read as `> 0.0` gate on entire attacker scan (dead actors
  can't attack).
- It's written to 0.0 at L882 with `DAT_0438be98=1` set (game-over
  trigger — set entry gate when HP=0).
- It's overwritten by FUN_0044b16c — likely the damage applicator.

**Resolution path:** Frida read of `*(float*)0x056db0bc` during retail
dungeon combat — if it tracks player HP, confirmed.

### Q3 — Is the projectile table at DAT_0695f004 the same as C8j allocator's table B?

NO.  Different stride (0xa8 dw vs table B's 0x49 dw), different base
address.  Table B holds spawned bodies (entities + NPCs + drops);
DAT_0695f004 holds **active attack effects emitted by NPCs / player**
(sword swing hitboxes, magic projectile hitboxes, AOE auras).  This
is a **separate population** we haven't surveyed yet — likely written
by combat-action helpers (FUN_004412b6 + FUN_0043824b + the
"slash"/"throw"/"shoot" emit functions).

**Resolution path:** survey FUN_004412b6 (2037 B combat-action
decision) to identify the projectile-table writer.  Worth a separate
chip C8jc.0 (combat-action survey).

### Q4 — Does the SM clear the per-tick flag?

No — only sets it to 1.  The integrator clears it to 0 at slot iter
top (already wired in C8j-tick.1).  This means the flag is per-slot
"did SM fire this tick", not per-frame.  Useful smoke flag: count
slots that fire SM per frame.

### Q5 — How does Phase D get reached?

Phase A passes (gates clear) → Phase B skipped (slot[1] not in {0,3} OR
DAT_056db0bc == 0) → falls to LAB_00439f28 → Phase C skipped
(slot[1] in {1, 3}) → falls to LAB_0043a491 → Phase D.

So Phase D requires `slot[1] == 2` (knocked-back state).  That means
Phase D bodies are processing NPCs in their KB state — checking
distance to player to deal contact damage to the player (knockback
contact-damage cycle).

### Q6 — Do we know the player slot's index in table B?

Not from this function.  The player has a specific table B slot
allocated during scene init (PHC #9 zone — FUN_00436f97).  Once we
port that, we'll know which slot the player occupies.  Until then,
the C8jb chips that depend on "is this slot the player" can read
`slot[0x2e] != 0` (the `is_player` flag) as proxy.

## Recommended next chip: C8jb.1

Skeleton sub-chip.  Land:

1. New file `src/scene1_combat_sm.{c,h}`.
2. Public `int scene1_combat_sm_tick(int slot_idx)` — Phase A entry
   gates + sets DAT_06a46f98 to 1.  Default ret=0.
3. Host-installable hooks for the 4 entry gates (game-pause / hitstop
   / cutscene latch).  All BSS-zero defaults → gates always pass.
4. Wire over PHC #20 stand-in: replace `state_machine_call_ret` with
   `scene1_combat_sm_tick` when scene1_records_b_tick fires SM.
5. Host tests: 4 entry-gate tests (one per global, set→ret=0, clear→
   ret=0 still since no slot logic yet).
6. Smoke flag `--debug-combat-sm` (optional) to log per-tick SM-tick
   counts.

After C8jb.1: integrator's `g_scene1_records_b_tick_anim_drive`
(DAT_06a46f94) will go from "always 0" to "whatever Phase A flag
state" — which is `DAT_06a46f98 = 1`, but DAT_06a46f94 is the
damage-out, written by Phase D.  Actually still 0 until C8jb.11
lands.  Useful for "did SM execute at all" smoke validation.

## Cross-links

- **Integrator (parent)**: `docs/findings/scene1-records-b-tick.md`
  (FUN_0043ae20).  73 SM call sites listed there.
- **Pending human checks**: PHC #20 (this function), #21 (resolved —
  DAT_06a46f98 writer is L35185), #24 (FUN_004319d6 hook still pending).
- **Sister-search**: PHC #6 / `FUN_0043ab6e` — uses the same
  people-table base.
- **Player position**: PHC #9 — DAT_056da1d8/dc/e0 writer
  (FUN_00436f97).
- **Per-character record base**: `DAT_044e3798` indexed by
  `DAT_0438b1e0 * 0x2dfc8` — same as PHC #11 (Cc.1 camera_char_mode).

## Estimated total effort

~11 sub-chips × ~150-200 LoC each = ~1.5-2 KLoC across ~1-2 sessions
of focused chip landings.  Plus parallel C8jc.* ladder for the
combat-action emitter (FUN_004412b6, ~2 KB) if visible-pixel value
demands it.

Smaller scope than C8j-tick (8 KB vs 25 KB), but downstream impact
larger: Combat is THE unlock for actual gameplay-loop validation.
