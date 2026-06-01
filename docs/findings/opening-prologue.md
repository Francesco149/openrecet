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

**Validation note (46 anchors vs msg-count).** No single `iv0_*` has exactly 46
`msg:` lines; the opening may be a multi-script sequence and/or `<KEY>`/`<C>`
splits a `msg:` into several reveal cycles. The exact opening script index
(which `5c7a2c/5c7a30` the new-game path selects) is the first thing to pin in
the port — drive new-game on retail and read `DAT_005c7a2c/30` at the
`FUN_0046c295` load.

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
