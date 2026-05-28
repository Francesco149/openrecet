# Phase D.7 — Memory-access watch (implementation plan)

> Detailed executable plan. Pointed to from `harness-roadmap.md` §D.7.
> Status: **planned, not built.** Higher near-term leverage than E.4 because
> the blocker it resolves (the HOUSE shop_table render gap) is already active
> and on the critical path to visible gameplay.

## Why this, why now

The HOUSE/shop scene has ported walker + furniture-render chips (PII.3a/3b
done) but visible shop_table pixels are still blocked on a **writer we
haven't ported** — a memory region the engine fills before the render walker
reads it, whose writer doesn't show up cleanly in the decompile (it's reached
via indirect dispatch, or the Ghidra output drops it). See memory
[[openrecet_house_visible_blockers]] / [[openrecet_scene1_render_ladder]].

You can't port a writer you can't find by reading. **Frida's
`MemoryAccessMonitor`** finds it the other way round: mark the region
write-protected in retail, let the engine run, and every write traps with the
faulting instruction's address. That address → the engine function → the chip
to port. This is a *capability unblocker*, not verification — it directly
makes pixels appear, which is why it beats E.4 for the current front.

It's also reusable: several ledger entries are "no writer in decompile" of the
same class. Build it once, point it at each.

## The tool

Two pieces, mirroring the existing Frida-harness shape (`frida_capture.py` +
`openrecet-agent.js`):

### Agent side — `installMemoryWatch(regions)` RPC (`openrecet-agent.js`)

```js
// regions: [{ base: <module-relative or absolute VA>, size: <bytes>,
//             label: <str>, access: "w" | "rw" }]
rpc.exports.installMemoryWatch = (regions) => {
  MemoryAccessMonitor.enable(
    regions.map(r => ({ base: ptr(IMAGE_BASE + r.base), size: r.size })),
    { onAccess(d) {
        send({ kind: "mem_access",
               op: d.operation,                 // "read" | "write"
               from: d.from.sub(module.base),   // faulting insn, module-rel
               address: d.address.sub(IMAGE_BASE),
               region: d.rangeIndex,
               frame: g_manual_frame_counter });
    }});
};
```

Notes carried from prior Frida pain:
- **Write-only by default** (`access:"w"`) — read-tracing a hot region floods.
  Optional `"rw"` for the rare case the reader is what's unknown.
- `MemoryAccessMonitor` re-arms per page after each trap; expect a burst then
  silence. Batch `send()`s if a region turns out hot (same per-frame batching
  as `d3d_trace`), else per-access is fine.
- **Classifier-clean output** ([[feedback_classifier_clean_output]]): all
  debugger/agent chatter to a log file; the driver's stdout is one JSON line.

### Driver side — `tools/mem_watch.py`

- Spawns/attaches retail via the existing `frida_capture` plumbing
  (`--frida-remote cutestation.soy:27042` default — [[feedback_frida_remote]]).
- Drives retail to the scene where the region gets written (HOUSE-INGAME):
  reuse the `--auto-z-spam` / auto-3D HOUSE drive mode from
  [[openrecet_e0_followon_state]] / [[openrecet_e2_call_tracer]].
- Calls `installMemoryWatch` **before** the write happens (install during the
  pre-INGAME pause, or freeze the tick first — see E.4 Tier 2 / D.3).
- Collects `mem_access` events, groups by faulting `from` VA, and prints a
  ranked table: `writer_va → {n_writes, first_frame, sample target offsets}`.
- Cross-references each `writer_va` against `docs/port-ledger.json` and
  `functions.csv` to name the owning engine function and report whether it's
  already ported/stubbed/unported → the chip to port.
- Snapshot/restore + `kill_retail.py` cleanup in a `finally`
  ([[feedback_frida_kill_cleanup]]); restart frida-server if it has degraded
  ([[feedback_frida_server_leak]]).

## First target (confirm against current code before running)

The audit's candidate was the shop_table slot-flag region near
`stage_record + 0x2c750 .. +0x2c77c` (≈40 bytes, ~10 slot flags) written
during HOUSE-INGAME boot. **Caveat:** Cf.minimal (FUN_00436f97) already landed
since those notes, and `pending_human_checks` carries a staleness banner — so
**first** re-derive the actual current gap:
1. Run the port + retail call-trace diff for a HOUSE frame
   (`call_trace_diff.py`) and the render-trace diff (`render_diff.py`) to
   pinpoint which draw reads stale/zero data.
2. Identify the exact region that draw reads (from the walker port's
   documented `DAT_`/`stage_record` offsets).
3. Point `mem_watch` at *that* region. Expected outcome: one or a few writer
   VAs; map them via the ledger; the unported one is the chip.

Resolving it should fix the Cf.* shop_table visibility (and likely informs the
diagnosed orientation/scale bugs, which are downstream of the same writer).

## Sequencing & cost

| step | effort |
|------|--------|
| `installMemoryWatch` RPC + `mem_watch.py` driver | ~0.5 session |
| confirm current HOUSE gap (call/render-trace diff) | folded into the run |
| port the identified writer chip | normal chip session |

Cheap relative to E.4. No new build infra — pure Frida + existing drivers.

## Cross-cutting (Frida realities, pre-known)

- Use the **Frida-safe VA discipline** already established — but
  `MemoryAccessMonitor` watches *data* pages, not code trampolines, so the
  CRT/MFC trampoline-crash class ([[openrecet_e0_followon_state]]) doesn't
  apply here. The risk instead is over-broad regions trapping hot loops:
  start with the smallest region that covers the suspect field.
- Don't cycle enable/disable repeatedly in one run (same Frida-state caution
  as the tick-freeze). Enable once, collect, tear down.
- retail CWD must be `vendor/original/` for assets ([[feedback_openrecet_run]]).

## Open questions to resolve at build time

- Does `MemoryAccessMonitor` survive the engine's own page-protection changes
  on that region? If the engine `VirtualProtect`s it, the monitor may disarm —
  fall back to a hardware write breakpoint (`Process.setExceptionHandler` +
  debug registers) if so.
- Is the writer reached once (init) or per-frame? Determines whether to watch
  during the pre-INGAME pause only or across several frames.
