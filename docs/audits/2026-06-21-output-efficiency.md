# Output-token efficiency audit — 2026-06-21

> **Q (user):** cut session token burn (output-dominated) without quality loss — e.g.
> caveman (compress what I write) / headroom (compress what I read)?
> **Verdict:** premise half-right. Output DOES dominate cost, but **~84% of output =
> REASONING** (redacted thinking), not markdown. Prose-compression ceiling ≈ **2%**. Real
> headroom = reasoning **OVERHEAD** (turn count), NOT depth. **Max-thinking stays ON** (user
> runs it deliberately for decomp/parity; depth is load-bearing). Lever = DIRECT thinking.

## Method
- Parsed 376 transcripts (`~/.claude/projects/-opt-src-openrecet/*.jsonl`) via
  `tools/output_token_audit.py` (reproducible; takes a projects-dir arg for siblings).
- **Billed output = Σ `usage.output_tokens` DEDUPED by message `id`.** Trap: CC stores 1
  content-block/jsonl-line but stamps EACH line with the whole turn's usage → naive Σ
  multi-counts (first pass read 170M, ~3-5× inflated; true ≈ **57.7M**).
- **Thinking text REDACTED** (block present, content stripped) yet billed ⇒ thinking tok est
  = `billed − visible` (visible = measured text+tool_use chars → tok @ 3.5-4.0 c/t). Slight
  overcount of thinking (uncounted tool-JSON overhead); conclusion robust even @ 70%.

## Findings (376 sessions, 48,348 turns, 57.7M billed output tok)
Billed OUTPUT distribution:

| bucket | all | recent-40 |
|---|---|---|
| **THINKING (reasoning)** | **83.8%** | **88.9%** |
| visible response prose | 2.9% | 2.8% |
| doc .md prose (Write/Edit) | 1.9% | 1.2% |
| **compressible prose total** | **4.7%** | **4.0%** |
| code files (.c/.h/…) | 5.0% | 2.1% |
| bash commands | 4.8% | 4.1% |
| misc tool args | 1.5% | 0.9% |

Where thinking goes (turn type):

| type | turns | %out | %row=thinking |
|---|---|---|---|
| mechanical (tools, ≤300ch prose, no write) | 33,163 | 42.3% | 87.2% |
| analysis/response (prose>300ch, no write) | 5,895 | 32.1% | 91.1% |
| authoring (Write/Edit) | 9,290 | 25.5% | 71.6% |

Turn shape:
- **89.8% of turns = exactly 1 tool call; avg 1.11 tools/turn.**
- Single-tool MECHANICAL turns = **61.4% of turns, 36.7% of output (21.2M tok)**, ~all thinking.
- ~48k turns ⇒ ~48k re-orientation thinking preambles (~1.2k tok each).

## Held-out non-lossy test (prose compression)
- Compressed a real `esc-skip-event.md` slab (intro + MAJOR CORRECTION + investigations +
  port-status): **12,705 → 6,624 chars = −47.9%**, all hex/identifiers/code/paths VERBATIM.
- Fresh **Sonnet** sub-agent given ONLY the terse note (no repo) answered 5 orientation Qs →
  recovered ALL facts incl. RELATIONAL ones (why the −1 finding was retracted; both halves of
  the open contradiction). ⇒ telegraphic RE-prose is **non-lossy for re-orientation**.
- Caveat: I authored both the compression + the questions (not fully blind). Relational-Q
  recovery is still real signal.

## Levers (ranked) — what actually moves the 84%
1. **Fewer/fatter turns** — batch independent probes, front-load plans. Cuts reasoning
   OVERHEAD not DEPTH ⇒ lossless. Upper bound on the single-tool-mechanical slice ~18% of
   output; real = a chunk (dependency chains can't batch). **Biggest lever.**
2. **Cheaper-model sub-agents for mechanical + search** (grep/measure/build/find) — same
   reasoning, ~5-12× cheaper/tok. Targets the 37% mechanical slice.
3. **Persist conclusions tersely** ⇒ future-me reads not re-derives (cross-session reasoning
   compression). The real payoff of terse docs.
4. **Terse output house-style** — ~2% direct, free, non-lossy. Adopt as default, not strategy.

**TRAPS (don't):** compress reasoning DEPTH / "think terse" — load-bearing for decomp/parity,
user runs max-thinking deliberately. headroom-style INPUT compression — input is not the cost
driver here (though shrinking bulky tool outputs indirectly helps lever 1 by fitting more/turn).

## Adopted convention
`CLAUDE.md` → **"Output-efficiency (TERSE MODE)"** block: max-thinking ON (direct, don't cut);
terse prose (code/hex/ids VERBATIM) + batch turns + delegate mechanical/search + persist terse.

## REVERT (if quality loss observed)
- Disable the WRITING style: `git revert <CLAUDE.md TERSE-MODE commit>` (or delete the marked
  block). The batching/delegation levers are behavioral — just stop applying them.
- This doc + `tools/output_token_audit.py` STAY (analysis infra, harmless).
- Re-measure effect anytime: `nix develop --command python3 tools/output_token_audit.py` —
  compare thinking% / tools-per-turn / prose-share before vs after.
