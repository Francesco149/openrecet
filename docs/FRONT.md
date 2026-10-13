<!--
  The ONE hand-edited status block. tools/gen_port_ledger.py injects everything below the
  marker into docs/STATUS.md's "Current front". Keep it short (60-second read) and ONLY
  open/forward-looking: when an item resolves, move its story to PROGRESS.md / findings in
  the same edit. Old snapshots: docs/archive/FRONT-*.md. PORT-DEBT: docs/port-debt.md.
-->
<!-- FRONT:BEGIN -->
**Methodology:** in-process retail host (rhost), adopted 2026-10-13 by owner decision
(`docs/audits/2026-10-13-methodology-reset.md` §3, Phase 0 approved). Pilot after
Phase 0: dungeon hybrid (§3 Phase 1).

### Phase 0 — foundation (in progress)
- ✅ `tools/rhost/` productized:
  - deterministic heap / handles / stacks / time / loads / file overlay;
  - quiet off-screen windows;
  - Windows queue runner via `ssh win` (WSL not needed);
  - `det` gate; corpus; record; flatten; hook table; D3D proxy arm.
  - Determinism: only DLL-pointer slots differ run-to-run (README).
- ☐ **★ NEXT: corpus triage**:
  - re-record the `cad868` first-customer / day-2 family natively (`rhost.py record`);
    their traces need Frida-era pins;
  - flatten every completing legacy trace;
  - calibrate `volatile.txt` over the corpus.
- ☐ **x87: convert the remaining 283 affected fns** (`docs/findings/x87-float-semantics.md`):
  - neither `-std` is retail-exact; mirror the disasm with `src/x87.h`;
  - verify per fn with a P1 oracle or an rhost hook;
  - go flow by flow with the re-verification program.
  - Done: the haggle family (oracle-exact under c11 + gnu11). Port now builds gnu11.
- ◐ **Port-side determinism**: port↔retail traces should align by frame number.
  - Boot→title already aligns (`title-options` `TITLE_SETTINGS_READY` f39 both).
  - `--det-loads` (DEFER twin) cut the house-load gap from 3,475 frames to **1**:
    port `LOADING_END` f72 vs retail f73, then a constant +1.
  - ★ next: find that off-by-one (where the load flags clear relative to the deferral
    point, port vs rhost); then compare `state` fields frame by frame.
  - Port runs need their own cwd: it writes `openrecet-calltrace-*.jsonl` into the cwd.
- ◐ **v3 viewer fed by rhost:**
  - retail D3D stream from rhost (`--d3d --cap`): ✅ captured + replayed BIT-EXACT (house-idle f600-602);
  - port stream from the port;
  - join by frame number.
- ◐ wine/Linux target:
  - ✅ `rhost.py --target wine -j N`;
  - ⚠ RNG diverges from Windows at new game: retail's SE path runs only with working
    audio (`DAT_096430fc`), and headless wine has no DirectMusic. Windows native = RNG
    truth (findings §10);
  - ☐ wine DirectMusic, or a fix for the audio flag;
  - ☐ bounded podman container.
- ☐ **Day-2 shop / multi-customer haggle**: recording needed (Human queue).

### Re-verification program (owner: "re-verify everything with the new methodology")
Every earlier "1:1"/"verified" claim is UNVERIFIED (`docs/findings/confirmed-parity-ledger.md`).
Re-check each flow by shadowing its ported functions over rhost scenarios, in this order:
1. **Customer service + haggle beyond the first customer.** Earlier evidence covered only
   customer #1. Needs multi-customer scenarios (record).
2. **End of tutorial.** One very long cutscene plus load seams — the stress test for long
   cutscenes and loaders under DEFER.
3. Title / intro / prologue; house free-roam, pause menus, save commit (`save_compare.py`);
   guild / world map; day 2.
Per flow: list the ported fns it runs (rhost `--coverage` ∩ port ledger), add shadow hooks,
and record the evidence line in the findings doc.

### Human queue (report at session end; ranked by what it unblocks)
1. **Record a Day-2 shop session** (multi-customer haggle; only customer #1 was ever
   checked). From maximus:
   `python3 tools/rhost/rhost.py record day2-shop --save 5816458d8f4f6508a3c2ca89640d3a6eb234cec49b54b7a4f1307567f1223903`
   - a window opens on the Windows desktop: Load Game → 3rd slot (Day 2, 155 pix);
   - open the shop, serve ~5–10 customers (sell + buy, haggle several rounds);
   - close the window. Keyboard only (pads are hidden). The trace self-checks on finish.
<!-- FRONT:END -->
