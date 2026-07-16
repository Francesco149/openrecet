# OpenRecet — project charter and program map

> **Status:** authoritative project-level policy
>
> **Last verified:** 2026-07-16
>
> **Live implementation front:** `FRONT.md`
>
> **Long-horizon evidence/tooling program:** `plans/parity-evidence-roadmap.md`

This file owns stable goals and constraints. It deliberately does not duplicate live
coverage counts, the active gameplay gap, or detailed implementation backlogs.

## 1. Objective

Produce a 32-bit Win32 drop-in replacement for `recettear.exe` that a legitimate
Recettear owner can play from beginning to end and cannot distinguish behaviorally
from the reference executable under the same game data, configuration, inputs, and
environment.

The target includes more than visible screenshots:

- simulation, state transitions, RNG consumption, and quirks;
- frame sequence, D3D8 command program, and pixels;
- input, focus, window, filesystem, save, and configuration behavior;
- sound/music events, timing, fades, and audible output;
- real-time pacing and load/transition behavior;
- every reachable path, content record, failure mode, and ending.

Frame-exact comparison remains required on the locked preservation reference host.
Cross-host runs may use structural/state claims, but never weaken the same-host bar.

## 2. Preservation and legal boundary

- Repository contains only original reimplementation source, tools, tests, schemas,
  and documentation.
- Never commit retail executable bytes, game assets, extracted resources, user saves,
  golden retail frames, PCM, or traces containing proprietary payloads.
- Local proof artifacts may use licensed retail data. Shareable proof output contains
  hashes, metrics, normalized metadata, and original project data only.
- The unpacked retail executable is the behavioral oracle; its exact identity must be
  recorded by hash in every durable parity proof.
- This is a behavioral reimplementation, not a byte-identical rebuild.

## 3. Load-bearing platform decisions

| Area | Decision | Until |
|---|---|---|
| Language | C11 | permanent unless separately decided |
| ABI | i686 Win32 | full parity complete |
| Compiler | mingw-w64 cross compiler | full parity complete |
| Floating point | x87; no SSE arithmetic or fast-math | full parity complete |
| Graphics | Direct3D 8 fixed-function path | full parity complete |
| Runtime oracle | Native Windows/WSLInterop, same host for both sides | full parity complete |
| Assets | User-owned retail files loaded at runtime | permanent |
| Portability backend | deferred | after preservation release gate |

Do not add a renderer abstraction, move to x86-64/SSE, or normalize retail quirks for
cleanliness before full parity. Those changes destroy already-established equivalence
assumptions.

## 4. Evidence doctrine

Parity is a scoped claim, never a global adjective. Every claim names:

1. retail and port build identities;
2. starting save/state and configuration;
3. exact input/action sequence;
4. normalization hooks used;
5. scenario/anchor range and seeds;
6. measured pillars: reachability, timing, state, save, render program, pixels,
   audio, external effects, and coverage;
7. explicit exceptions and their scope.

Joining two frame streams proves correspondence, not equality. Same-side replay proves
capture/replayer consistency, not retail/port equivalence. Source annotations prove
inventory, not runtime execution. The detailed claim vocabulary and implementation
plan live in `plans/parity-evidence-roadmap.md`.

The deterministic pinned lane and the real-time unpinned lane answer different
questions and must remain separate:

- **Pinned lane:** normalize known nondeterministic origins to test identical
  data-to-output behavior frame by frame.
- **Real-time lane:** preserve native scheduling, focus, input, audio, and presentation
  to test the shipped experience.

Neither lane can substitute for the other.

## 5. Definition of done

### A function or chip

- Retail behavior grounded in decompile/disassembly and a runtime observation.
- Port body faithful, including odd edge behavior.
- Host tests cover observed and boundary inputs where feasible.
- Relevant call/state/render/audio evidence captured.
- Required parity pillars pass for named scenarios and seeds.
- No unexplained residual is labeled phase, benign, or accepted.
- New knowledge and engine quirks are persisted; temporary shortcuts carry
  `PORT-DEBT`.

### A scenario edge

- Deterministically reaches the intended start and completion state.
- Retail and port inputs, frame identities, and required state roots correspond.
- Its parity contract produces a content-addressed proof bundle.
- Coverage contribution is recorded across code, VM, content, assets, audio, and
  transitions.
- Reproducible from committed, non-proprietary inputs plus the owner's local game.

### The preservation release

- Certified behavior graph covers a complete playthrough plus all enumerated choices,
  content families, failure paths, settings, and supported input modes.
- No reachable observed retail branch is silently absent from the port.
- Save compatibility is bidirectional across the supported lifecycle.
- Locked-host frame/audio/timing gates pass, with every exception explicit and owned.
- No unresolved parity-capping `PORT-DEBT` remains.

## 6. Program structure

| Scope | Source of truth |
|---|---|
| Current gameplay/RE target | `FRONT.md` |
| Derived implementation inventory | `STATUS.md`, `port-ledger.*`, `port-debt.*` |
| Long-horizon proof, coverage, and exploration work | `plans/parity-evidence-roadmap.md` |
| Active per-subsystem plans | `plans/README.md` |
| Operational trace workflow | `trace-workflow.md` |
| Durable findings | `findings/INDEX.md` |
| Historical decisions/audits | `audits/`, `archive/` |
| Agent/reasoning split | `AGENT-WORKFLOW.md` |
| Documentation ownership/staleness rules | `DOCUMENTATION.md` |

`STATUS.md` is a generated source-inventory view. Until the evidence-roadmap ledger
migration lands, its `verified`/`ported` labels must not be read as whole-game parity
proof.

## 7. Macro sequencing

Gameplay continues frame-0-forward while reusable tooling lands at the point it removes
repeated uncertainty. The broad convergence sequence is:

1. Early-game shop loop and save/load behavior.
2. General customer/economy/day progression.
3. Town facilities, menus, encyclopedia, and event variants.
4. Dungeon traversal, combat, AI, loot, bosses, and party variants.
5. Remaining scripts, endings, movies, settings, and failure/recovery paths.
6. Full behavior-atlas traversal, configuration matrix, debt retirement, and release
   proof.
7. Only then: portability/backend work.

This sequence is not a feature-priority shortcut. A reached feature is not complete
until its proof and coverage obligations are recorded.

## 8. Build and validation entry points

```sh
nix develop --command make -C src
nix develop --command make -C tests run
nix develop --command python3 tools/run_python_tests.py
nix develop --command python3 tools/ci/check_docs.py
```

Run Windows binaries only through `tools/run-openrecet.sh` or the scenario/probe
supervisors. Use `tools/scenario-test.py` for deterministic target runs and
`tools/trace_studio_v3/orv3_window.py` for identity-aligned render windows.

## 9. Historical record

The founding May 2026 phase plan is preserved at
`archive/bootstrap-plan-2026-05-19.md`. The June methodology audit remains a dated
decision record at `audits/2026-06-09-methodology-audit.md`; this charter and the July
evidence roadmap supersede its live prioritization, not its historical findings.
