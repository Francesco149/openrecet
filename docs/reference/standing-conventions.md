# OpenRecet Standing Conventions & Feedback Registry

> **Status:** Tracked in-repo. Canonical for all models (Gemini 3.7 Flash, Claude, etc.) under the OMP harness.
> Auto-loaded via `CLAUDE.md` summary.

---

## 1. Toolchain & Environment (OMP + Nix Devshell)

- **`nix develop --command <cmd>` is MANDATORY:** The NixOS WSL2 host does not provide global `python3`, `mingw32`, `make`, `ghidra`, or `frida`. All commands must use the nix develop prefix.
- **Multimodal Vision:** When running on Gemini 3.7 Flash, image files (`.png`, `.jpg`) read with the `read` tool are directly delivered as native multimodal images into context. `inspect_image` is not required.
- **Model Identity & Co-Author Slug:** Always co-author commits using the running model's slug (e.g. `Co-authored-by: Google Gemini <gemini-3.7-flash-tiered>` or `OPENRECET_AI_COAUTHOR`).

---

## 2. Human Verification Protocol & Trace Viewer Launch

- **Frictionless Setup Rule:** Whenever an item is gated on human verification (eyeball, animation feel, gameplay cadence, visual artifacts):
  1. Never ask the user to manually configure state, poke memory, or reconstruct test scenarios.
  2. ALWAYS provide the exact 1-command line with `--launch` that materializes/slices the window, updates the Windows shortcut pointer (`C:\openrecet-studio\studio-current.txt`), and launches the native Windows viewer (`viewer.exe`) on their desktop:
     ```bash
     nix develop --command python3 tools/trace_studio_v3/orv3_window.py <scenario> --window <OFFSET>:<COUNT> --launch
     ```
     (or `cmd.exe /c C:\openrecet-studio\open-studio.bat` if already built).
  3. Clearly state the exact expected behavior vs observed divergence to save maximum human time.
  4. Maintain and report all open human verification gates at the conclusion of every session until resolved, ordered by downstream scope unblocked.

---

## 3. The Porting Loop & Deterministic Tracing

- **Deterministic Trace as Ground Truth:** Reproducible port↔retail traces side-by-side in Trace Studio v3 are the foundation.
- **Scout Live First:** Use the `openrecet` MCP daemon to explore retail live (anchors, RNG stream, state pokes, engine function calls) before baking a deterministic `tests/scenarios/` trace.
- **Bilateral Pinning:** Always pin non-deterministic origins (`{phasepin}`, `{rngseed 19937}`, `{bgnpcseed}`).
- **Load Pins Are Extend-Only Min-Gates:** `csloadpin` and `tutloadpin` must exceed retail's real load duration (e.g. CS 72, TUT 36); otherwise retail finishes before the pin and desynchronizes.
- **Retail bg_npc Non-Determinism:** Retail shop window NPCs wander based on load duration; use bilateral `{bgnpcseed:[seed,cursor]}` pinning.
- **RNG Golden Captures:** When capturing call-queue goldens, use the engine-thread atomic `seed_at_call` + `seed_after_call` window (never a separate final-seed RPC) + fresh centroid (`FUN_0048439a`).

---

## 4. Build, Decompilation & Code Integrity

- **C Header Dependencies:** Every C Makefile must include `-MMD -MP` and `-include $(DEPS)` to prevent silent struct offset corruption during incremental rebuilds.
- **Verify 1:1 Before Declaring Done:** Always compare port vs retail rendering in Trace Studio before committing; never ship render code on decompile reading alone.
- **Full Port, Not MVP:** Tag temporary shortcuts with `PORT-DEBT(tag, ...)`. Retire them systematically.
- **Engine Quirks:** Log retail ground-truth quirks in `docs/findings/engine-quirks.md`.

---

## 5. Output & Agent Efficiency

- **Terse Prose Mode:** Direct max-reasoning depth into architecture, decomp, and cross-subsystem parity. Write all prose, responses, and commits telegraphic and dense.
- **Batching:** Batch independent tool calls into a single turn; front-load exploration plans.
- **Subagent Caution on Math/Parity:** Delegate searches and mechanical edits freely; require strict binary/host test gates before accepting decompiled logic from lower reasoning tiers.
