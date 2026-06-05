# Opening prologue (cutscene + dialogue) — ground truth (Phase 0)

> Started 2026-06-01. Goal: structural parity through the post-new-game opening
> prologue (Recette wakes / Tear explains the debt) before HOUSE free-roam. The
> port currently FAKES this (`scene1_intro_events.c` stub). Plan:
> `~/.claude/plans/binary-purring-goose.md`.

## The big correction (empirically verified — overrides the planning guesses)

The opening prologue does **NOT** run in scene-states 2/3. It runs entirely in
**scene-state 1 (INGAME)**.

Probe (retail, `runs/intro-state-probe`, spam-A intro trace, seed-pinned):
`DAT_0438b1c0` (g_scene_state) goes `0 → 1 at frame 72` and **stays 1** through
the whole prologue (to frame 5441+). `DAT_0964357c` (state-2 cutscene counter)
and `DAT_0076b96c` (state-3 dialog counter) stay 0 the entire time.

Confirmed by call-trace (`runs/scenarios/intro-prologue-retail-20260601T172452Z`,
calltrace bound to the 1st HOUSE_FREEROAM, 1600-frame window over the prologue):
- `FUN_0049d8a4` (scene-state 2) — **ABSENT**.
- `FUN_0041ee24` (scene-state 3) — **ABSENT**.
- `FUN_0048670f` (free-roam controller) — present **once** (the boundary, when
  the prologue ends and real free-roam begins).

→ The state-2/3 drivers (`FUN_0049d8a4`/`FUN_0041ee24`) are some **other** scene
(ending / dungeon-clear / etc.), NOT the opening. `FUN_0041ee24`'s trailing
yes/no (`FUN_00434ed2`) is its own ESC-skip prompt, off the opening path.

## What actually drives the prologue (INGAME state 1)

Per-frame in the prologue the INGAME tick (`FUN_004427d3` → `FUN_004536cb` state-1
arm) runs, every frame (n≈1600 over the 1600-frame window unless noted):

- **`FUN_0048407f`** (795 B) — the per-frame **actor pose / anim / facing**
  driver in INGAME (sets `DAT_056db05c` facing, `DAT_056daae8`/`dab00` anim,
  steps `FUN_00482a71`, calls companion `FUN_0048a4d1`, dust `FUN_00447f4f`).
  Reads `DAT_0438cc08` (cc08, the `!= 4` branch). Runs in BOTH cutscene and
  free-roam — it is the actor tick, not the dialogue driver itself. (This is the
  old "biggest single chip" cutscene pointer; it is real, but it is the actor
  animator, not the text engine.)
- **Dialogue text engine** — heavy `FUN_005038ff` (sprintf-family, n=11099) +
  string/glyph code. The per-frame text-box function is in the **`0x0047c29d`
  region**: `FUN_0047c29d` (n=1600) with sprintf call-sites at `0x0047c336` /
  `0x0047c35c` (each x1600/frame). The glyph/measure path pulls in the large
  `0x004d…0x0050…` C++ string/STL block (117 prologue-only VAs — the SJIS/EN
  text-measure + draw subsystem absent from steady free-roam).
- **`cc08` (`DAT_0438cc08`) stays 0** during the prologue (free-roam's cc08=1 via
  `FUN_004850ec` is only set at the *real* free-roam boundary). So the prologue
  is gated by something other than cc08==1; `FUN_0048407f` branches on cc08 but
  the dialogue sequencing is elsewhere (the script/event driver — see Open).

## Prologue-only call-graph (vs steady free-roam)

117 VAs fire in the prologue but never in steady free-roam
(`runs/scenarios/house-wall-collide-both-…/retail`). The actionable clusters:
- **Dialogue/UI text**: `0x47c29d`, `0x47cbcb`, `0x47d464`, `0x47183b`,
  `0x471905`, `0x47193c`, `0x473c0c` (the 0x47 text/UI region).
- **Script/event sequencer (candidates)**: `0x4063c7`, `0x404e61`, `0x405a52`,
  `0x4346bf`, `0x4349e5`, `0x46bf38`/`0x46c01e`/`0x46c090`/`0x46c295`/`0x46c320`/
  `0x46c869`/`0x46c9a2`/`0x46ddea` (the 0x46c effect/hikari/text block — note
  `0x46c9a2` is also the steady-state dust RNG consumer from
  `scene1-rng-stream-parity.md`).
- **Text rendering / string**: the `0x4d…`–`0x50…` block (glyph measure/draw,
  `operator_new`/`malloc` for string buffers).
- Staging: `0x44baad`, `0x452d07`, `0x45281c` (fade/character setup).

## RESOLVED — the dialogue subsystem (sequencer + reveal counter + anchor signals)

> Done 2026-06-01. Supersedes the "Open" guesses below. **Correction:**
> `0x47c29d` is the glyph-cache **debug overlay** (`OVER_FONT`/`TOTAL=` counter
> over the per-character rasterizer pool `DAT_073de66c`, stride 0x1c) — NOT the
> dialogue line engine. The real prologue dialogue subsystem is the **`0x46c`
> cluster**, gated by `DAT_0438b1c8 == 1` (dialogue active):

| VA | function | role |
|----|----------|------|
| `0x46c01e`* | dialogue **init/reset** | zeroes the reveal state; sets text-speed `DAT_005c78dc = (&DAT_005c78e0)[DAT_056e5784]` (the 0..2 text-speed setting) |
| `0x46c295` | **script loader** | `PTR_DAT_005c78d0 = &DAT_0735f4f8` (the command table); `FUN_0046ddea` parses the event script; `DAT_073a6bd4 = 0` (cmd index); sets `DAT_073a3e20=1` (disable) on parse-fail |
| `0x46c2cb` | ESC **skip-event** prompt | the trailing yes/no (`FUN_00434def` "Do you want to skip this event?") — confirms the §Drivers note; off the Z-spam path |
| **`0x46c320`** | per-frame **update** | advances the reveal counter, reads the advance button, runs the script command loop. Called from the INGAME state-1 arm `FUN_004536cb` (L50514/50630) gated on `DAT_0438b1c8==1` |
| **`0x46c9a2`** | per-frame **draw** + reveal-completion | draws the box text char-by-char; sets the "fully revealed" flag. **Also the steady free-roam dust RNG consumer** — see the unification note below |

*the init reset block is at `all.c:67055-67099` (zeroes `DAT_073a3e00`/`e04`/`e08`).

### The two anchor signals (single global each, per-frame observable)

- **`TEXT_ANIM_START`** = `DAT_073a3e00` (reveal counter) **resets to 1**
  (`0x46c9a2`, `all.c:67768-67769`: `if (DAT_073a6d74==1) DAT_073a3e00 = 1;`).
  Per frame the update (`0x46c320`) climbs the counter `1,2,…` clamped at
  `0x800`; on a new line the draw forces it back to 1. A per-frame sampler sees
  the value **decrease** (e.g. `0x800 → 1`) exactly on the new-line frame →
  clean rising/new-line edge. (`DAT_073a6d74` is the one-frame "new line" pulse,
  set by a script command and cleared in the draw — too transient to sample;
  the counter reset is the robust proxy.)
- **`TEXT_ANIM_END`** = `DAT_073a3e04` (the "fully revealed / awaiting input"
  flag) **rises 0→1** (`0x46c9a2`, `all.c:67771-67801`). The draw sets it 1,
  then clears to 0 while the per-char reveal budget
  `local_10 = (DAT_073a3e00-4)·DAT_005c78dc / 32` is still being consumed by the
  `DAT_073a6bd0` (= line char-count) glyphs; it settles at 1 once the whole line
  draws with budget to spare. It gates the post-complete blink timer
  `DAT_073a3e08` (`0x46c320`, `all.c:67238-67240`). So `e04` 0→1 = "line
  finished revealing, settled, awaiting advance" — exactly the user's
  "text animation finished" edge. Both edges **recur per line**.

Advance button (skip typewriter / next line): C = `0x10`
(`(DAT_073dddfe|DAT_073dddd4)&0x10`), A = `0x40`
(`(DAT_073dddfa|DAT_073dddd0)&0x40`) — both jump `DAT_073a3e00 = 0x800`
(`0x46c320`, `all.c:67226-67232`). Next-line advance: the script command loop
walks `PTR_DAT_005c78d0 + DAT_073a6bd4*0xc` (12-byte `{fn,arg1,arg2}` triplets);
a command returning **2** breaks → `DAT_073a6bd4++` (next line); **3** = special
(set a flag, return); **0** = end (`all.c:67321-67335`).

### Glossary of the dialogue globals

| addr | meaning |
|------|---------|
| `DAT_0438b1c8` | dialogue-active gate (`==1` while the prologue dialogue runs) |
| `DAT_073a3e00` | per-char reveal counter (1‥0x800) — **START signal** |
| `DAT_073a3e04` | "fully revealed / awaiting input" flag — **END signal** |
| `DAT_073a3e08` | post-complete blink/idle timer (ticks while `e04!=0`) |
| `DAT_073a3e20` | dialogue-disabled (script parse failed / done) |
| `DAT_073a6bd0` | current line char count |
| `DAT_073a6bd4` | script command index |
| `DAT_073a6a38` | glyph-table byte offset of the current line (`<0` = no line) |
| `DAT_073a6d74` | one-frame "new line" pulse |
| `PTR_DAT_005c78d0` | script command table base (`&DAT_0735f4f8`) |
| `DAT_005c78dc` | reveal speed (px per counter tick), from `DAT_005c78e0[DAT_056e5784]` |
| `DAT_005c78ec` | text updates/frame (speed-up while advance held) |

### Unification: `FUN_0046c9a2` IS the dust RNG consumer

`scene1-rng-stream-parity.md` flagged `0x46c9a2` (3800 B, reached via the render
root `FUN_004547ab → FUN_0046c090 → FUN_0046c9a2`) as the last unported steady
per-frame free-roam LCG consumer blocking foot-dust *phase* parity, and the
2026-06-01 ambient-motes PROGRESS entry calls it out as "Not yet closed." It is
the **same function** as the dialogue text-box draw here. So porting `0x46c9a2`
closes **both** fronts: (a) the dialogue reveal/draw + the `TEXT_ANIM_END`
signal, and (b) the free-roam RNG-stream completeness for dust positions. Its
LCG reads (the `0x46cf81` int caller in the rng-callers table) are the box's
sparkle/effect emits — they run in free-roam too, which is why it's a *steady*
consumer, not prologue-only.

## Open (older guesses — superseded by RESOLVED above)

1. ~~Find the script/event sequencer~~ → `0x46c320` (update) + `0x46c295` (loader).
2. ~~Text-reveal counter~~ → `DAT_073a3e00` (START) / `DAT_073a3e04` (END), above.
3. Whether the prologue is one INGAME sub-mode or a sequence of scripted events
   → **scripted events**: the `PTR_DAT_005c78d0` command table walked by
   `DAT_073a6bd4`, parsed from the event script by `FUN_0046ddea`.

## Reproduce

```
# state probe:
python3 tools/frida_capture.py --input-segtrace tests/scenarios/house-walk-down/trace.jsonl \
  --watch state=0x0438b1c0:s32 --watch dlg=0x0076b96c:s32 --watch cut=0x0964357c:s32 \
  --rng-seed 1 --run-dir runs/intro-state-probe --remote cutestation.soy:27042 \
  --hide-window --turbo --silent-audio --max-frames 5400 --no-montage
# prologue call-trace:
python3 tools/scenario-test.py intro-prologue --target retail --frida-remote cutestation.soy:27042
```

Scenario: `tests/scenarios/intro-prologue/` (spam-A head + a `{calltrace:[0,1600]}`
bound to the 1st HOUSE_FREEROAM = the prologue segment).

## Phase-0 capture results (retail, anchors validated)

`runs/dlg-anchor-probe` — the spam-A intro trace replayed on retail (seed 1)
with `--anchor-trace`. The new `TEXT_ANIM_START`/`END` edges fire cleanly:

- **46 dialogue lines** in the opening prologue: exactly **46 `TEXT_ANIM_START`
  + 46 `TEXT_ANIM_END`** pairs, each START→END 1–4 frames apart (the spam-A
  trace skips the typewriter via the `0x10` skip, so reveal is near-instant).
- The lines run **between the two `HOUSE_FREEROAM` anchors** — 1st at frame
  11792 (prologue start), first line END at 12114, last line END ~13122, then
  `LOADING_START 13285 / LOADING_END 13295 / HOUSE_FREEROAM 13295` (the real
  free-roam boundary the `scene1_intro_events` stub fakes with its 2nd load).
  Confirms the prologue is one INGAME-state-1 span gated by `DAT_0438b1c8==1`,
  not a scene-state hop.

Per-line reference images: `tests/scenarios/intro-dialogue-lines/` — rebases on
`TEXT_ANIM_END` and `{capture:0}`s each of the 46 settled lines:

```
python3 tools/scenario-test.py intro-dialogue-lines --target retail \
  --frida-remote cutestation.soy:27042
```

(`--target both` once the dialogue driver `0x46c9a2` is ported — currently only
the retail/Frida side emits the text anchors; the port's `anchor_world`
dialogue fields are zero until then.)

## RESOLVED — the `.ivt` script language (interpreter ground truth)

> Done 2026-06-01. The `0x46c` cluster is a **text event-script interpreter**.
> `FUN_0046ddea` (the "parser", 5119 B of code) is a **compiler**: it reads a
> line-based text script and emits the 12-byte `{fn,arg1,arg2}` command triplets
> the update loop (`0x46c320`) walks. The scripts are **real proprietary data
> files** present at runtime — porting the interpreter means reading the game's
> own scripts, NOT embedding any.

**Script file path.** `FUN_0046ddea` builds `iv/iv%d_%d.ivt` from
`DAT_005c7a2c`/`DAT_005c7a30` (the scene/sub indices, set by the scene-entry
selector around `all.c:45251-45550`), then `FUN_005038b0`-opens it (falls back
to a raw-read of an uncompressed copy via `FUN_004346bf` when the pack lookup
misses). Files live in `iv/` in the install dir (215 of them; SJIS-encoded,
**English text inline in the EN build**). They are game assets — never
redistribute; the port reads them from the retail install at runtime, same as
meshes/bmps/SE.

**Line format.** CR/CRLF/LF tolerant. Lines beginning `\r \n / \t` are skipped
(blank / `//` comment / indented continuation). A `;` in a line triggers a
syntax-error `MessageBoxA`. Each significant line is one script command.

**Command vocabulary** (keyword strings at `0x5c7a..0x5c7b`, confirmed against
real scripts e.g. `iv0_1.ivt`):

- **Background / scene**: `bgset:<bmp>`, `bgscroll:<spd>`, `polybg:<xfile>`,
  `lighton:<a>:<b>`, `lightoff`, `windowpos:<x>,<y>`, `windowset:...`,
  `color:...`.
- **Character ops** (`chr:<N>:<op>...`): `dir:left|right`, `grp:<tga> W,H`,
  `col:r,g,b,a`, `colto:r,g,b,a`, `fadeframe:<n>`, `move:x,y`, `moveto:x,y`,
  `speed:<f>`, `center:<n>`, `anim:...`, blend modes `normal_shade` /
  `normal_add` / `add` / `add_shade`, `disp`.
- **Flow / timing**: `wait:<frames>`, `skipon` / `skipoff` (toggles the ESC
  "skip this event?" prompt, `0x46c2cb`).
- **Audio**: `se:<bin>`, `music:<f>`, `holdmusic:...`, `mfadein:<n>`,
  `mfadeout:<n>`, `fadein`/`fadeinb`/`fadeout`/`fadeoutb`/`fadeframe:<n>`.
- **Message**: `msg:<a>:<b>:<text>` — the dialogue beat. In-text markup:
  `<BR>` (line break within the box), `<KEY>` (wait-for-advance = one
  `TEXT_ANIM` cycle), `<C>` (clear box), `<W>` (?). The reveal counter
  (`DAT_073a3e00`) climbs over the line's glyphs; `<KEY>` is the per-line
  advance gate the `TEXT_ANIM_START/END` anchors straddle.

**Command return contract** (the `0x46c320` loop, `all.c:181-195`): each
triplet's `fn(arg1,arg2)` returns `0`=stop-this-frame (don't advance the
index), `2`=advance one then break (yield a frame — e.g. `wait`/`msg`-await),
`3`=special flag-set + return, other-nonzero=advance and run the next command
same frame. So setup commands (`chr:*`, `bgset`, …) return nonzero-continue and
fall through instantly; `wait`/`msg` yield.

**Opening script — PINNED (retail probe, 2026-06-01).** The opening prologue is
a **two-script sequence**: `iv1_1.ivt` (`5c7a2c=1, 5c7a30=1`) then `iv1_2.ivt`
(`1,2`). Probe (`runs/intro-script-probe`, house-walk-down trace, seed 1, turbo,
14000 frames): `DAT_0438b1c8` (dlg gate) first hits 1 at engine-frame **11664**
with `5c7a2c/30 = 1,1`; while the gate is up the pair takes both `(1,1)` and
`(1,2)`. Message counts: **iv1_1 = 16 msg + iv1_2 = 30 msg = 46** — exactly the
46 `TEXT_ANIM_END` anchors Phase 0 captured. iv1_1 = Tear wakes Recette ("She is
still asleep… WAKE UP, PLEASE!… Today is the day we set for opening the store!");
iv1_2 = downstairs ("Sorry I kept you waiting!… Capitalism, ho!… we need to take
care of a few matters before we open"). So the port loads iv1_1, runs it to its
end command, then loads iv1_2 — the script transition is what the
`scene1_intro_events` stub's fake 2nd-load gate was approximating.

### Implementation map — parser → command-table → handlers

`FUN_0046ddea` is a **one-pass compiler**. For each non-comment line it runs a
chain of `FUN_00479f4d(keyword, line, len)` (strncmp-prefix) tests; the first
match parses the line's numeric args (`FUN_00503d03`=atoi, `FUN_00503c2b`+
`__ftol`=atof→int) and stores one (or two) 12-byte command triplets:

```
(&DAT_0735f4f8)[i*3 + 0] = handler fn ptr   // 0 terminates the program
(&DAT_0735f4f8)[i*3 + 1] = arg1             // DAT_0735f4fc
(&DAT_0735f4f8)[i*3 + 2] = arg2             // DAT_0735f500
```

The update loop (`0x46c320`) calls `handler(arg1,arg2)`; return code: `0`=stop
frame (don't advance i), `2`=advance+break (yield a frame), `3`=special (flag +
return 1), other-nonzero=advance+run next same frame. Setup commands return
continue and collapse into one frame; `wait`/`msg`-await yield.

Handlers are tiny stubs at `0x46d8xx–0x46ddxx` (Ghidra labels them `LAB_*`
inside the `0x46ddea` span — they are separate fns preceding the parser):

| keyword | handler | args | notes |
|---|---|---|---|
| `color:i:r,g,b,a` | `0x46d8d3` | i, packed-rgba | |
| `bgset:<bmp>` | `0x46d912` | bg slot | name→`DAT_07350df0[slot*0x100]`, `DAT_073a3df0++` |
| `polybg:<x>` | `0x46d912` | 0 | name→`DAT_0734fff0[*0x100]`, `DAT_073a3dfc++` |
| `bgscroll:<f>` | `0x46d8a5` | ftol(spd) | |
| `windowset` | `0x46d8c6` | | |
| `windowpos:x,y` | `0x46d8e6` | x, y | |
| `skipon`/`skipoff` | `0x46d8fc` | 0/1 | toggles `0x46c2cb` ESC prompt |
| `fadein:f:r,g,b,a` | `0x46dd2c` | packed, frames | (`fadeinb`/`fadeoutb` = no-op marker) |
| `fadeout:f:r,g,b,a` | `0x46dd53` | packed, frames | |
| `lighton:a:b` | `0x46dd7a` | a, b | |
| `lightoff` | `0x46ddb1` | | |
| `wait:<n>` | `0x46dcd6` | frames | **yield (ret 2)** |
| `music:<n>` | `0x46dcef` | n | |
| `holdmusic` | `0x46dce3` | | |
| `mfadein:<n>`/`mfadeout:<n>` | `0x46dd02`/`0x46dd15` | n | |
| `se:<bin>` | `0x46d885` | se idx | name→`DAT_0734b9b0[idx*0x100]`, `DAT_0735dd80++` (cap 0x3f) |
| `chr:N:dir:L/R` | `0x46da1e` | N, 0/1 | |
| `chr:N:move:x,y` | `0x46da33`+`0x46dc0a` | N, x / y | two triplets |
| `chr:N:moveto:x,y` | `0x46da6e`+`0x46dc30` | N, x / y | two triplets |
| `chr:N:center:n` | `0x46da59` | N, n | |
| `chr:N:speed:f` | `0x46dc45` | N, ftol(f) | |
| `chr:N:anim:..` | `0x46dc97` | N, .. | |
| `chr:N:fadeframe:n` | `0x46dc82` | N, n | |
| `chr:N:normal_shade/normal_add/add_shade/add` | `0x46dcac` | N, 0/1/2/3 | blend mode |
| `chr:N:col:r,g,b,a` | `0x46da83` | N, packed | |
| `chr:N:colto:r,g,b,a` | `0x46db20` | N, packed | fade toward |
| `chr:N:grp:<tga> W,H` | `0x46dc97`(name path) | N, name-table idx | registers chrname in `DAT_07357830`/dims `DAT_073a3ab8/abc` |
| `chr:N:disp` | `0x46da09` | N | |
| `msg:a:b:<text>` | msg-parser | builds glyph/line table | sets `DAT_073a6a38`(glyph byte-offset), `DAT_073a6bd0`(char count), `DAT_073a6a30`(line idx); `<BR><KEY><C><W>` markup |

Aux tables the parser fills (all per-script, reset on load): bg-name
`DAT_07350df0`, polybg `DAT_0734fff0`, se-path `DAT_0734b9b0`, chrname
`DAT_07357830`+dims `DAT_073a3ab8`, glyph table `DAT_073652b8` (stride 0x40),
per-line `(offset,count)` walked by `DAT_073a6bcc/a30`.

**Defer boundary (this port = structural):** port faithfully = the data model
(command table + aux tables), the parser, the loader, the update sequencer
(`0x46c320`: reveal counter, advance button, command walk), the reveal-
completion state in `0x46c9a2` (the `DAT_073a3e04` END logic + `DAT_073a3e00`
START reset) and the `TEXT_ANIM_*` anchor emission. **Defer:** the D3D draw
calls in `0x46c9a2` (bg/chr/window/glyph blits), glyph **rasterization**, and
exact per-glyph **advance widths** — under the spam-A trace the reveal is forced
to `0x800`, so the END edge fires as soon as a non-empty line is current
regardless of glyph widths (use char-count × nominal advance as the budget
proxy). The chr-anim float loop + `thunk_FUN_005041f6` jitter reads in `0x46c320`
/`0x46c9a2` are the **dust RNG-stream** consumers — port them to close that front
too ([[scene1-rng-stream-parity]] unification).

### RESOLVED — handler bodies + runtime tick (raw-disasm, 2026-06-01)

> The `0x46d8xx–0x46ddxx` handler stubs are computed-call-only (absent from the
> decompiled C). Disassembled from `vendor/unpacked/` (image base 0x400000).
> This nails the **return-code contract** (drives walk yield-timing) and the
> **msg-path state writes** (drive the `TEXT_ANIM_*` anchors).

**Return codes (the `0x46c320` walk contract).** Every handler ends `push $N;
pop %eax; ret`. Observed: **all `chr:*` handlers, `color/bgset/polybg/bgscroll/
rmb/windowset/windowpos/skip*/fadein/fadeout/light*/music/holdmusic/mfade*/se/
msg-speaker/msg-clear` → ret 1** (advance + run next command same frame).
**Yield/block ops:** `wait` (`0x46dcd6`, sets `DAT_073a6d7c=arg`, **ret 2**),
`msg`-show (`0x46d97b`, **ret 2**), `msg`-waitkey (`0x46d93c`, **ret 0** until
`DAT_073a3e08>=15` AND advance-edge `(DAT_073dddfe|DAT_073dddd4)&0x10`, then
**ret 2** + SE 0x144), `end:` (`0x46dd76`, **ret 3** → walk's special path:
`DAT_04510b38[scene]=1` + `DAT_056e5790[scene]=1` seen-flags, c320 returns 1).

**Two keywords the landed parser was MISSING** (both present in `iv1_1.ivt`):
- **`rmb:a,b`** → handler `0x46d926`, args `atoi(a)+1, atoi(b)+1` → `DAT_073a6d98
  /DAT_073a6d9c` (screen-shake jitter counters; the `thunk_FUN_005041f6` reads
  in `0x46c9a2` gated on them are **dust-RNG-stream consumers**).
- **`end:`** → handler `0x46dd76` (ret 3, the script terminator). NOTE: the
  parser also appends a NULL-fn `IVE_OP_END` row; a NULL handler in the walk just
  *idles* at the last command (LAB_0046c518, ret 0) — it does **not** end the
  script. The real end is the `end:`→ret-3 path. Every prologue script ends with
  an explicit `end:` line.

**msg-path state writes (the anchor source).** `msg:a:b:text` compiles to
SPEAKER + SHOW [+WAITKEY][+CLEAR][+WAIT(10)]:
- **SPEAKER `0x46d9f3`**: `DAT_073a6da0=a` (speaker idx), `DAT_073a3e10=b`
  (portrait/face idx). ret 1.
- **SHOW `0x46d97b`** (the reveal beat): `DAT_073a6a38 = (a1==-1 ? DAT_005c7a28
  : a1)`; `DAT_005c7a28 += a2` (running glyph-offset accumulator);
  **`DAT_073a3e00=0`** (reveal counter), **`DAT_073a3e08=0`** (dwell);
  `DAT_073a6bd0=a2` (line glyph count); **`DAT_073a6d74=1`** (new-line flag);
  `DAT_073a6a30++` (line index); saves per-line offset/count to
  `(&DAT_073a6a3c)[line]` / `(&DAT_073a6bd8)[line]`. ret 2 (yield).
- **CLEAR `0x46d9e1`**: `DAT_073a6a38=-1`, `DAT_073a6a34=-1` (no current line →
  `0x46c9a2` skips the completion block; box closes). ret 1.

**Anchor mechanics (combined `0x46c320`-then-`0x46c9a2` per frame).** SHOW sets
`e00=0`+`d74=1` (frame N). `0x46c9a2` (runs after c320, same frame): if
`-1<DAT_073a6a38` and `d74==1` → **`DAT_073a3e00=1`** (START edge) + `d74=0`;
tentatively `e04=1`, then `e04=0` if the reveal budget `(e00-4)*DAT_005c78dc/32`
doesn't cover the line's glyph advances. `0x46c320` next frames: while `e00>0`,
`e00++` (×`DAT_005c78ec` steps/frame); **advance-edge `0x10` forces `e00=0x800`**;
held `0x40`+scene-FF-flag forces `0x800`; held `0x20`→2 steps. Once `e04!=0`,
`e08++` each tick (the WAITKEY dwell). So per line: **START** = `e00` →1 (frame
after SHOW), **END** = `e04` 0→1 (budget covers line; immediate next frame under
advance-spam since `0x10` slams `e00`→0x800). Port edges:
`anchor_world.text_reveal==1` / `text_revealed` (`anchor_trace.c`).

**Input masks.** `DAT_073dddd0`(p1)/`DAT_073dddfa`(p2) = **held** at record
offset 0; `DAT_073dddd4`(p1)/`DAT_073dddfe`(p2) = **pressed-edge** at offset +4
(stride 0x2a). Bit **0x10** = face button A (binding slot 4 = confirm/advance) —
the bit the `intro-dialogue-lines` trace pulses. The port keeps only the held
mask (`g_input_state[N].buttons`); the runtime derives the edge as `cur & ~prev`.

**Box open/close gate.** `DAT_073a3e14` (0..15) ramps up when a line is shown,
down when cleared; the `wait` counter `DAT_073a6d7c` only decrements while the
box is fully open (==0xf) or fully closed (<1) — so the box anim delays `wait`.
Modeled for frame-fidelity; the per-line anchor rebase absorbs it either way.

### LANDED + live-validated — runtime wired, all 46 anchors fire (2026-06-01)

The interpreter (`scene1_dialogue_run`) + driver (`scene1_intro_dialogue`) are
wired (scene.c arm / sim.c tick / main.c anchor feed). A port-side
`scenario-test intro-dialogue-lines --target openrecet` run
(`runs/scenarios/intro-dialogue-lines-openrecet-*`) emits **all 46
TEXT_ANIM_START + 46 TEXT_ANIM_END anchors** (92 rows in `anchors.jsonl`) — the
full iv1_1 (16) + iv1_2 (30) sequence, where before the port emitted none.

**Open: port-side capture is 32/46 (pacing).** The port advances ~53 frames per
line (ENDs span frames 2925–5379); each trace segment budgets 320 input frames
of A-pulsing before its `wait TEXT_ANIM_END`+capture. So the port races ahead —
several lines END inside one segment's button phase, and that segment's single
wait/capture misses the extra ENDs (→ 32 captured). The reveal-slam + dwell-gate
logic is **raw-disasm-verified faithful** (0x46c5d6 edge-0x10 → reveal=0x800;
WAITKEY dwell `>=0xf`), so this is a port↔retail *cadence* question, not a logic
bug: either retail also races (the 320-frame segments are over-budgeted and the
trace wants re-tuning to the true per-line cadence) or retail paces slower for a
reason not yet visible port-side. **Resolving it needs a retail anchor-cadence
capture (Frida remote) to diff against** — the box/text draws are deferred to the
visual pass regardless, so the per-line *pixel* parity is not blocked on this.

### RESOLVED — port↔retail cadence is frame-exact (retail capture, 2026-06-01)

Captured the retail side via Frida (`--target retail`, remote
`cutestation.soy:27042`, run `…retail-20260601T193256Z`) and diffed the 46-line
reveal/advance cadence (TEXT_ANIM_END inter-line gaps) against the port:

```
retail gaps: 106 166 106 46 46 118 118 46 22 22 46 106 22 22 46 389 22 46 22 78 …
port   gaps: 106 166 106 46 46 118 118 46 22 22 46 106 22 22 46 286 22 46 22 78 …
                                                       ^^^ only difference
44 / 45 inter-line gaps are FRAME-EXACT identical.
```

The single divergence is gap #16 — the **iv1_1 → iv1_2 script transition**:
retail 389 frames, port 286 (Δ103). That is the **inter-script load screen**
retail shows between the two scripts, which the port deliberately does not
reproduce (the driver loads iv1_2 in-memory; the load-gate/HOUSE_FREEROAM dance
stays with the intro-events stub — see `scene1_intro_dialogue.h` scope). So the
interpreter itself is **bit-exact in pacing** to retail; the lone gap is the
known-deferred load screen.

Both sides "race" through lines at ~22–118 frames each (the advance-spam slams
the reveal — NOT 320 frames/line); the 320-frame trace segments are just
over-budgeted. So the earlier "32/46 captured" port-side number is **purely a
segtrace wait/capture timing artifact** (fast-firing anchors vs the per-segment
wait), not an engine divergence — retail captured 46/46 of the identical
sequence. Per-line PIXEL parity remains deferred (text/box draws not yet ported).

### RESOLVED — the script-load / gate / transition subsystem (gap #16, 2026-06-01)

The +103-frame gap #16 (iv1_1→iv1_2) is a **real loading-overlay bracket** retail
shows between the two scripts. Full retail anchor timeline (Frida,
`…retail-20260601T193256Z`, `agent.log`):

```
frame   71  NEW_GAME + LOADING_START #1        (title→ingame; HOUSE scene load begins)
frame 3011  LOADING_END #1 + HOUSE_FREEROAM #1 (2940-frame scene load done)
frame 3332  iv1_1 line 1 START   → 16 lines, last END @ 4418
frame 4581  LOADING_START #2                   (iv1_2 inter-script load)
frame 4649  LOADING_END #2 + HOUSE_FREEROAM #2 (68-frame load)
frame 4807  iv1_2 line 1 END     → 30 lines, last END @ 5937
```

So retail fires **exactly 2 LOADING brackets + 2 HOUSE_FREEROAM** (matching what
the `scene1_intro_events` stub fakes — but the stub fires #2 in the *wrong place*,
~10 f after #1, before any dialogue):
- **#1** = the new-game HOUSE scene load (port: `scene_post_fade_init` →
  `worker_load_spawn`). **iv1_1 has NO bracket of its own** — it's loaded under #1
  and its first line just appears ~321 f after HF #1.
- **#2** = the iv1_1→iv1_2 load (the gap-#16 +103). This is the piece the port
  skipped.

**The gate `DAT_0438b1c8`** is a 3-state machine: `0`=idle, `2`=armed/loading,
`1`=running. Arming a script (`FUN_0044ba2c(scene,sub,p3)`): refuse if gate≠0,
else write selector `DAT_005c7a2c/30`, set gate=**2**, call `FUN_00452d07(p3)`
(the loader). `FUN_00452d07` spawns a **secondary worker thread** (`CreateThread`
→ `LAB_00452aab`) that reads the .ivt + its bg/chr/se assets and, on completion
(`FUN_004528b3`), flips gate **2→1** and the dialogue runs (`FUN_0046c320` each
frame while gate==1). On `end:` (ret 3) the per-frame pump (`FUN_004538xx`
L50630) sets gate=**0**, clears `DAT_0438bf74`, fires a transition, then
`FUN_0044baad()` — which arms the **queued** next script (`DAT_06a4706c` set by
`FUN_0044ba6b`): gate=2 again → load #2 → gate=1 → iv1_2. After the last script
nothing is queued → done. (The exact opening-arm entry is the scene-1 new-game
event path; not pinned, not needed for the structural port.)

**The transition effect** (`FUN_0045281c`/`FUN_004526f5`/`FUN_004526ab`/
`FUN_00452569`) is a 10×10 grid "shatter/melt" over a 640×480 space (cell stride
0xc; x=col·64−288, y=216−row·48):
- `FUN_00452569`: seed per-cell RNG velocities (3 rot `06a47130/34/38` + 3 pos
  `06a475fc/600/604`, each `(rand−0.5)·k·0.5`) via `FUN_00471089`.
- `FUN_0045281c(mode,dur)` = **shatter-OUT**: `DAT_0438bf7c=−1`, flat grid, runs
  `dur` frames then auto-clears (`bf7c=0`). Typical `dur=0x11` (17).
- `FUN_004526f5(mode,dur)` = **melt-IN**: `bf7c=1`, grid pre-advanced 30 steps
  (starts fully shattered), counts to `dur+1` and **holds** (cleared elsewhere).
- `FUN_004526ab`: per-frame counter `DAT_0438bf78` ±1; `FUN_00452809` sets the
  `DAT_0438bf74` "transition active" flag. The grid render (warped quads scaled
  by `bf78`) is **render — deferred to the visual pass**; only the `dur`-frame
  timing matters for cadence.

**Determinism caveat:** bracket #2's 68-frame duration is a real async asset
load (iv1_2's TGAs), so it is **not byte-reproducible** — the port models it as a
fixed/representative hold (PORT-DEBT) since the dialogue assets aren't loaded/
rendered yet. The structure (a loading bracket in the right place → LOADING/HF #2)
is what closes the front; the exact 103 stays environment-dependent.

### RESOLVED — the DRAW pass (FUN_0046c9a2 ported, 2026-06-02)

The deferred visual side is now ported as `src/scene1_dialogue_draw.c`
(`scene1_dialogue_draw`, hooked into main.c's INGAME render after the 3D scene,
before the HUD). Built in layers; all verified vs the `golden-retail/cap_*.png`
goldens (feed). Most infra was reused: `render_quad` (2D quads) + `font_draw_text`
(glyphs) + `sprite_load` (TGAs).

**Asset load** (FUN_0046bf38): bg names → `g_bg[]` (1024×512), `ive_window.tga`
→ `g_window`, `chrname.tga` → `g_nameplate`, chr grp names → `g_chr[]`. Loaded on
the first draw of each script (sim has no D3D device), keyed by a driver
generation counter. iv1_1 = bedroom (2D bg); iv1_2 = the shop (live 3D HOUSE, no
ive bg).

**Handler field map** (the 0x46da09–0x46dcac stubs, raw-disasm; standee struct
base = `&DAT_073a3e70`, stride 0x70 / 28 ints, 200 entries):
| handler | VA | writes | field |
|---|---|---|---|
| disp | 0x46da09 | active = a2 (**=1**, LAB_0046efd4) | 11 |
| dir | 0x46da1e | mirror = a2 | 12 |
| move:x | 0x46da33 | x current(1)+target(3) | 1,3 |
| moveto:x | 0x46da6e | target(3) only | 3 |
| move/moveto:y | 0x46dc0a/0x46dc30 | y current(2)+target(4) | 2,4 |
| center | 0x46da59 | offset | 7 |
| speed | 0x46dc45 | tween speed = a2/DAT_0051958c | 5,6 |
| anim/grp | 0x46dc97 | graphic index = a2 | 14 |
| fadeframe | 0x46dc82 | — | 9 |
| col | 0x46da83 | current(15-18)+target(19-22) from packed argb | 15-22 |
| colto | 0x46db20 | target(19-22) only | 19-22 |
| blend | 0x46dcac | mode = a2 | 27 |

The port keeps the **settled** pose only: move/colto SNAP current=target (the
per-frame tween + speed/fadeframe are PORT-DEBT — the goldens are at the settled
per-line anchor). col/colto channel order: field15=b,16=g,17=r,18=a; the draw
repacks `a<<24|b<<16|g<<8|r` (the engine's order, R/B-swapped vs D3DCOLOR).

**Standee draw** (46c9a2 153-209): skip if field11==0 or `chrname[field14]`
empty; src=(0,0,chr_w,chr_h); blend field27 ≥2 = additive (SRCALPHA/ONE) else
normal, bit0 → COLOROP ADD(7) vs MODULATE(4); dst=(field1,field2,w+.5,h+.5);
field12==1 → mirrored quad.

**windowpos:x,mode** (0x46d8e6) → `DAT_005c7980`=x off / `DAT_005c7984`=mode.
**windowset:N** (0x46d8c6) → `DAT_005c797c` top-banner. **bgset:N** (0x46d912) →
`DAT_073a6d90` active bg + clear scroll. **bgscroll** (0x46d8a5) → `DAT_073a6d94`.
**rmb:a,b** (0x46d926) → `DAT_073a6d98/9c` shakes.

**Box** (210-282) via `ive_box_scale` (FUN_0046c86f: cos(n·3π/15) wobble, alpha
(n)·0x56, closing-shrink path). 4 position modes off `DAT_005c7984`:
0=centre (src lower strip 0,176..416,352, **no nameplate**), -1=left, 1/other=
mirrored (src upper 0,0..416,176, **with nameplate**), 2=text-only/no frame. Box
X = speaker-standee centre (`local_c` = halfwidth + x ± centre-offset). dst_y =
`DAT_005c7980` + 88 − sy·88. **The prologue uses mode 1** (nameplate drawn;
proven by retail showing the Tear/Recette name tabs).

**Nameplate + arrow** (283-345): name image from `chrname.tga` in a 7-tall ×
128×32 grid indexed by `DAT_073a3e10` (the msg `b` arg / portrait idx); alpha
(box_open−4)·0x3c; only when mode≠0/2 ∧ box_open≥5. Blinking next-line arrow
(window-tex cell, `(blink/5)%20` capped 4) when `DAT_073a3e04` (END) set.

**Glyph text** (350-388) via FUN_00405a52 (truncate row to the per-frame reveal
budget `(reveal−4)·speed/32`, SJIS-aware) → FUN_0047d464. The dialogue glyph
scale 0.65·(`_DAT_0052912c`/100) with the default font-size global **76** =
0.65·0.76 = 0.494 = `font_draw_text`'s built-in factor, so `font_draw_text`
(scale 1.0) is reused verbatim. Text x = box `local_c` − 16; per-row y +=30 from
`DAT_005c7980`+56. PORT-DEBT: the font-size (`_DAT_0052912c`) + text-speed
(`DAT_056e5784`) settings aren't wired (assume defaults 76 / normal).

**"ESC Key: Event Skip" tip** (the draw tail, 67831-67843; ported 2026-06-02 as
`draw_skip_tip`) — the very last quad: a fixed bottom strip from the boot-time
`data_win.tga` atlas (`DAT_073d8678`, src 288,384..488,416 → dst 440,440 200×32,
colour 0xffffffff). Gated `DAT_073a3e18 > 1 ∧ DAT_073a6db0 == 0`. `DAT_073a3e18`
is the free-running per-tick counter `FUN_0046c320` bumps at the top of every
frame (reset by `FUN_0046c0ae`) — so the tip appears from the dialogue's 2nd
frame onward. **`DAT_073a6db0` (the skip-disable flag) is only ever *written* 0**
(reset in `FUN_0046c0ae`, no setter in the corpus) → the gate is effectively
always true; the "skip disabled" branch is dead in this build. The port reuses
`g_sysassets.data_win_tga` (already loaded), so no per-script asset load. Tip
text pixel-matches retail (feed `cap_00` zoom). **User-verifiable: visible
bottom-right of every prologue line.** Remaining Layer 4: the `rmb` screen-shake
RNG reads (`DAT_073a6d98/9c`-gated; closes the foot-dust RNG-phase front) and the
choice/menu fade overlay (`DAT_073a6da4`; no choices in the prologue).

**Open follow-up (user-flagged 2026-06-02):** the dialogue **box (bubble) edge**
has a slight port↔retail difference at its border — text + tip are pixel-perfect,
but the box frame edge is subtly off. Suspect a **texture-filtering** mismatch on
the `ive_window.tga` box quad (POINT vs LINEAR / box-filtered mip — cf.
`engine-quirks.md §54` and the chr-sprite POINT-filter fix). Drill into the box
quad's sampler state vs retail before assuming a geometry/UV bug. Not yet
investigated.

### RESOLVED — standee tween, char-reveal, per-script skip, FX anchors (2026-06-02 PM)

The deferred animation layer is now ported (user-verified bit-identical for the
slide-in, fade-from-black, effect sprites, and per-line text). See
`engine-quirks.md` §84 for the mechanics. Summary of what landed:

- **Standee move/colto tween** (`ive_run_tween` in `scene1_dialogue_run.c`):
  `moveto` sets target-only; current slides by `speed` (×1000 fixed-point /
  1000.0); `colto` computes per-frame deltas (field19-22) + countdown (field10),
  applied to current colour (field15-18). Drives Tear's `-390→-100 @5px/frame`
  slide-in and the kuro fade-from-black (`col …,255 → colto …,0` over
  `fadeframe:240`). Effect pop-ups (sigh/zzz) snap to full alpha then fade out.
- **Char-based reveal** (`ive_completion` + `ive_row_count`): the END /
  book-icon flag latches when the `(reveal-4)·32/32`-char budget clears every
  row; a settled line now auto-completes (~char-length frames) so ONE advance
  press moves on (was: press to slam, press again to advance).
- **Per-script skip** (`scene1_intro_dialogue_skip_to_end`): ESC-skip ends only
  the CURRENT script — iv1_1 → iv1_2 (the 2nd dialogue over the free-roam map),
  iv1_2 → free control — mirroring the engine's `end:`→`FUN_0044baad`
  queued-next-script teardown (NOT a jump straight to free-roam).
- **`EXTRA_SPRITE_*` catch-all anchors** (`anchor_trace.c` + the Frida agent):
  START / FADED_IN / FADEOUT / END over `fx_alpha` (max alpha of active index>=2
  standees). Lets a TAS trace frame any effect sprite's fade deterministically.
  Scenarios: `intro-opening` (fade+slide), `intro-sigh` (effect-sprite check),
  `intro-fade` (phase-anchored fade).

**Remaining real deltas (NOT 1:1, tracked — do not handwave):**
1. **Dialogue box-edge "halo" — ROOT-CAUSED 2026-06-05: box SCALE / bounce-anim
   PHASE, NOT a filter/decode delta.** The earlier "scaling/texture-filter
   mismatch (POINT vs LINEAR / box-mip)" guess is **wrong**. `--d3d-trace-verts`
   shows the box texture/UVs/diffuse/center are **bit-identical** port↔retail; the
   box *dst scale* differs because the squash-and-stretch open/bounce animation
   (`ive_box_scale` = `FUN_0046c86f`) is caught at a different `box_open`/branch on
   each side at TEXT_ANIM_END (cap_00 port sx0.9875/sy1.0125 = open-branch n=15 vs
   retail 1.0/1.0; cap_01 ~opposite bounce phases, user-confirmed squish). Magnified
   1.6× → the rim offset traces the bubble outline = the "halo". Same family as
   note #6 below + the db054 phase class. See confirmed-parity-ledger row "Dialogue
   box-edge = box SCALE/bounce-anim PHASE". NEXT: per-frame Frida watch of
   `box_open` (`DAT_073a3e14`) + reveal cursor (`DAT_073a6a38`) on retail vs port.
2. **FPS overlay** — the bottom-right `Fps` counter (benign environment
   artifact; see `benign-divergence-registry`).
3. **Absolute prologue timing** — the synthetic load brackets arm the scripts at
   a different offset than retail (phase; §85). Per-effect render is bit-exact;
   only the wall-clock start drifts.
4. **iv1_2 opening freeroam-sprite anims (NOT ported)** — during the iv1_2 (2nd
   dialogue) opening, after the fade-from-black and during the character
   slide-in, retail plays HOUSE freeroam-sprite animations the port doesn't:
   (a) **Recette looks up at Tear + blinks** (≥3 blink cycles), and (b) **Tear
   strikes an angry pose with a billboard radial-lines effect** (manga anger
   marks). Both are freeroam character/effect anims that run *during* the
   dialogue, NOT the dialogue standees. Reference capture: the **`intro-iv2-gap`**
   scenario (reaches iv1_2 via the 2nd HOUSE_FREEROAM, idles through the opening,
   captures HF#2 +14..+138 every 2 frames; zoom crop `(520,578) 104×150` frames
   both Recette + Tear — port|retail paired by capture index). cap_04 = eyes
   open → cap_05 = eyes closed on retail; port static.

   **CHARACTERIZED (2026-06-02 PM, follow-up RE).** The visible characters in the
   pre-box opening window (cap_00–53) are the **small HOUSE freeroam chibi
   sprites** (player Recette + companion Tear), NOT the dialogue standees — the
   big `512×512` portrait standees (`02tear_00.tga` / `01recette_04.tga`) + the
   dialogue box only slide in **~cap_54** (verified: cap_60 shows the box +
   nameplate "Recette" + the eyes-closed `recette_04` portrait). So this is a
   **HOUSE freeroam cutscene that plays BEFORE the iv1_2 dialogue box.** Both
   freeroam actors are already drawn by the ported pipeline (walker `FUN_00456f56`
   → leaf `FUN_0045a56f`), so the gap is **animation-index selection + frame
   advance**: retail sets cutscene anim indices on the actors (Recette look-up,
   Tear angry) and plays their frame cycles (the blink is a frame cycle of an
   idle/reaction anim; the radial-burst is a 2-frame effect billboard), while the
   port leaves them in a static default frame. The per-character `.idx` animation
   blocks (`chr_sprite_meta`) already carry these poses — what's missing is the
   driver that selects + ticks them in this window.
   - **NOT** the `iv1_2.ivt` `chr:4 giku.tga` / `chr:5 hatena.tga` effect
     standees: those are EXTRA_SPRITE effects that fire MUCH later in the script
     (lines 82/57, after ~8 advanced msgs) and ride the **big portraits**, not
     the freeroam sprites. The idle scenario never advances that far. The
     pre-box radial-lines are a separate freeroam effect (source TBD).
   - **Full iv1_2.ivt RE'd** (extracted via `recettear-repacker/lnk_unpack`,
     SJIS): opening = `fadeinb:240` → chr:0(Tear) slide `-500→-100` → `wait:60`
     → chr:1(Recette) slide `480→240` grp `recette_04` → `wait:60` → first
     `msg`. Confirms the standee layer carries no look-up/blink/anger ops in the
     opening — the anims are freeroam-side.
   - **Next step (per repo methodology):** a retail Frida call-graph over
     cap_00–20 to pin the driver fn that sets the freeroam actors' anim index in
     this window (is it a HOUSE intro-event state machine, or emergent
     companion/player reaction?), then port that + ensure the actor frame-advance
     ticks. Sub-chips: freeroam idle eye-blink (likely reusable across all
     free-roam), scripted look-up/angry pose select, radial-burst effect.
5. **Dialogue text doesn't fade to transparent on dismiss** — when a line is
   dismissed, retail fades the glyph text out as the box closes; the port pops it
   off. Investigate the box-close alpha applied to the text in `FUN_0046c9a2`.
   Same `intro-iv2-gap` scenario captures the dismiss window.
6. **Animation PHASE misalignments (flagged 2026-06-04, user-observed; investigate
   later).** Several intro/dialogue animations are captured at a different *phase*
   than retail (or than the prior golden), even though the capture anchoring is
   honest. Two specific sightings so far:
   - **Speech-bubble bounce-in** (`intro-dialogue-lines`, cap_16+): the dialogue
     box's bouncy appear animation lands on a different bounce frame. cap_00–15
     (pre-bubble iv1_1 lines) stay bit-exact; the bubble lines drift.
   - **Standee slide-in tween** (`intro-iv2-blink`, the CONV_POSE_BLINK capture):
     the big portrait standees' slide-in is at a divergent tween position.
   These are PHASE (timing-origin) diffs, NOT artificial-alignment artifacts:
   audited 2026-06-04, **no scenario uses `{gframe}`/`{phasepin}`/`{rngseed}` or
   any frame-shift/best-match** — captures are pure `{wait:ANCHOR}` + anchor-
   relative `{capture:N}`, and `intro-dialogue-lines` is **46/46 bit-identical
   across two current-build runs** (deterministic per-anchor). So the phase that
   drifts is the *animation's* load/RNG-dependent origin (same class as the
   db054 walk-cell phase, engine-quirks §94 / `phase_probe`), surfaced *because*
   the anchoring is honest. To probe: `tools/phase_probe.py` + the per-frame
   phase-counter method (`reference_phase_divergence_method`) on the bubble's
   bounce counter and the standee tween field, vs retail on a synced trace.
   Likely one shared origin-pin fixes multiple anims. Deferred per user
   (alongside dust / NPC-RNG / Tear-wing particles).
