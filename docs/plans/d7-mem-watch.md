# Memory-access watch — current usage

> **Status:** TOOL DONE-AS-SCOPED; invoke for bounded writer hunts  \
> **Built/validated:** 2026-05-29  \
> **Historical build plan:** `archive/d7-mem-watch.md`

`tools/mem_watch.py` and the Frida agent's memory-watch support locate retail code
touching a small data region and map the faulting VA to the implementation ledger.

Use it only after a current state/render investigation identifies an exact field/region:

```sh
nix develop --command python3 tools/mem_watch.py \
  --region <VA>:<SIZE>:<LABEL> <other bounded drive arguments>
```

Important limits:

- Frida `MemoryAccessMonitor` is page-granular and triggers on the first page access,
  so read-hot pages can exhaust re-arm attempts before the wanted write.
- Start with the smallest region and a cold/discrete write window.
- The all-thread hardware-watchpoint experiment crashed retail and is not part of the
  tool.
- Enable once, collect, and tear down; do not repeatedly cycle the monitor.
- Output identifies candidate accesses, not semantic ownership or parity. R3 must
  interpret the writer and decide the next chip.

New unknown-write work should feed the canonical state/mutation or call-capsule consumer
defined in `parity-evidence-roadmap.md` ST-05/CC-04 rather than creating an isolated
capture.
