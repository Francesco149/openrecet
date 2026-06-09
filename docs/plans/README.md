# docs/plans — index (status at a glance)

One line per plan; the plan file's own header carries the detailed status. Done
plans move to `archive/` (per archive-don't-delete). Strategy + the ranked
tooling roadmap live in `../audits/2026-06-09-methodology-audit.md`.

## Active

| plan | one-liner |
|---|---|
| `shop-display-roundtrip.md` | ACTIVE ARC — display interaction + pause save/quit/reload roundtrip (see FRONT for the live gap list) |
| `town-map-port.md` | world-map mode 8: T1–T4 + tooltip landed, backlog CLOSED 2026-06-08; town scenes off the map are the follow-on |
| `un-mvp-structural-parity.md` | the standing PORT-DEBT retirement strategy (registry: `../port-debt.md`) |
| `freeroam-structural-parity.md` | master 77-fn free-roam work list from the live call-graph survey |
| `freeroam-render-depth-parity.md` | phases 0–2 done; dust occlusion (char/dust depth relationship) still open |
| `house-controller-unmvp.md` | foundation + chips 1–4 landed; remaining chips queued |
| `house-player-controller.md` | W1 landed; W4 collision resolver blocked on furniture placement |
| `execution-flow-trace.md` | core LANDED 2026-06-05; field coverage grows with every chip (the standing annotate-as-you-port loop) |
| `tas-framework.md` | vision: grow anchor coverage until a whole playthrough replays deterministically |
| `trace-studio-v2.md` | phases 0–4 COMPLETE; Phase 5 (New-Game cross-replay: intro-video force-skip + mid-load actor spawn) queued — hardest, last |

## Done-as-scoped (kept here for the build log they carry)

| plan | one-liner |
|---|---|
| `d7-mem-watch.md` | mem-watch tool BUILT + VALIDATED 2026-05-29; run it when a "no writer in decompile" hunt next comes up |
| `e4-per-call-io-capture.md` | Tier 1 (stateful-leaf oracle injection) LANDED; Tiers 2–3 deliberately build-when-needed |

## Archived (`archive/`)

| plan | outcome |
|---|---|
| `archive/public-release-detour.md` | DONE 2026-05-29 — asset-free exe, README/ko-fi, nightly CI |
| `archive/esc-skip-event.md` | DONE 2026-06-02 — engine choice box + skip_event landed; the deferred FORCE_SKIP_AT golden was superseded by the TAS `{esc}` op |
