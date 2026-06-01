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

## Open (next, for the anchors + driver port)

1. **Find the script/event sequencer** that advances the prologue line-by-line
   (consumes the advance button, picks the next line, drives `FUN_0048407f`'s
   pose changes). Likely in the 0x46c / 0x47 cluster above; trace the caller of
   the text-box (`0x47c29d`) and the advance-button reader.
2. **Text-reveal counter** (for `TEXT_ANIM_START`/`TEXT_ANIM_END`): the
   per-char scroll-reveal index in the `0x47c29d` text-box path. START = index
   leaves 0 for a new line; END = index == line length.
3. Whether the prologue is one INGAME sub-mode or a sequence of scripted events.

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
