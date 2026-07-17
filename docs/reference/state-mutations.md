# ST-05 — semantic mutations (the causal layer under the state pillars)

> **Status:** CONSUMER LANDED 2026-07-17 (roadmap `../plans/parity-evidence-roadmap.md`
> §7 ST-05). The Frida post-write / TTD CAPTURE PLATFORM is deferred BEHIND this
> consumer (roadmap rule 11 — build consumers before platforms). R3 design here;
> schema `../schemas/state-mutation-v1.json`; consumer `../../tools/parity/state_mutation.py`;
> gate `../../tools/test_state_mutation.py` (44 checks).

## What a mutation is, and why

A **mutation** is a single named WRITE to a canonical-state field —
`{logical_frame, seq, path, class, type, old, new, owner_va, callsite_va}`. The
`path` is the SAME vocabulary the state pillars localize to: a VOLATILE
`subsystem/field` (`state-volatile-v1.json`, e.g. `customer_service/gold`) or a
PERSISTENT region path (`state-map-v1.json` `Locus.path()`, e.g. `bank0/closeness[37]`).

The two state pillars answer *equality*: the `state` pillar — "is the once-per-frame
state EQUAL each frame?"; the `save` pillar — "are the on-disk bytes EQUAL?". A
mutation stream is the layer BENEATH: it answers **causality** — "WHICH write first
diverged, and WHO wrote it?" That is exactly the provenance ST-04's first-divergence
report leaves `null`: ST-04 says *player px is wrong at frame F*; ST-05 says *the write
that made it wrong was `owner_va` at frame F, old→new*.

## The class gate (R3)

Every mutation carries a class — the R3 decision "is this change semantic, derived, or
noise?" (roadmap §2, ST-05 acceptance):

- **semantic** — a real game-state change the port must reproduce bit-for-bit (gold,
  inventory, shop placement, day/time, a flag, closeness, news, loot, HP, a VM PC step,
  a save-slot commit). ALWAYS compared. The first semantic write that diverges is the
  **first wrong write**.
- **derived** — a value COMPUTED from semantic state (a cached total, a checksum, a
  re-derived index). Compared, but a derived-only divergence points UPSTREAM to a
  semantic input — never the root cause on its own.
- **noise** — a capture-origin / nondeterministic write with a per-side origin (the
  volatile analogue of the state pillar's benign exclusions — `rngcalls`' hook-install
  origin, uninitialized padding, a pointer/handle). EXCLUDED, only with a recorded
  reason. **A mutation of UNKNOWN class is NOT noise — it is a STOP for R3**, never
  silently dropped (roadmap §2 fail-closed rule).

`COMPARED = {semantic, derived}`; noise is the only excluded class.

## The three consumer duties (host-verified, no capture)

1. **Reconstruct a subtree.** Replay the compared classes under a path prefix in join
   order, last-write-wins ⇒ the subtree's value at a frame. `verify_reconstruction`
   cross-checks it against the state pillar's captured per-frame fields — every WRITTEN
   path's reconstructed value must equal the captured value (a completeness check). A
   field never written in-window holds its window-start value (unknowable from the
   stream alone — correctly absent).
2. **Dedup / idempotence.** A post-write hook firing twice — or a batched + per-write
   observation of one store — must not double-apply. Dedup key `(logical_frame, seq,
   path)`; a CONFLICTING double-observation (same key, different `new`) is a capture
   fault ⇒ error.
3. **First wrong write ≤ first state-root divergence.** The invariant that LINKS ST-05
   to ST-04. Walk frames in join order comparing each side's CUMULATIVE value per path
   (an unwritten path holds the shared window-start, recovered from a write's `old`, so
   a one-sided write IS a real divergence). The first `(frame, path)` whose cumulative
   value differs is the first wrong write; if the stream is complete it precedes-or-
   equals that field's state-root divergence frame. A wrong write found AFTER the state
   already diverged ⇒ the stream missed the causal write ⇒ **INCONCLUSIVE, never a
   pass** (`check_ordering`).

## Fills ST-04's provenance seam

`attach_provenance(st04_report, port_muts, retail_muts, required)` sets the divergent
leaf's `first_divergence.provenance = {owner_va, callsite_va, path, kind, old, new, seq,
same_leaf}` and attaches the ordering check. Reachable from the CLI:
`state_diff.py <scen> --window OFF:COUNT --mutations` auto-loads
`{port,retail}-state-mutation.json` from the window dir. Composition is host-tested
end-to-end (a diverging-gold window + matching streams → the writer VA on the leaf).

## The deferred capture platform (what a producer must emit)

Per rule 11 the consumer defines the contract; the PLATFORM lands next when a scenario
needs it. A producer emits `state-mutation.json` = `{schema_version:1, side, source,
mutations:[…]}` in the schema shape, by **post-write observation at a known owner**
(prefer a Frida `Interceptor.onLeave` at the writing function, reading old (onEnter) →
new (onLeave); TTD/memory-watch only to DISCOVER an unknown writer). The event catalog
(`state-mutation-v1.json` `semantic_events`) grounds each roadmap event to its path
family; owners are attested as each hook lands (only `save_slot_commit → FUN_004905a8`
is certain today — the rest are `attested-at-capture`, an R1 backfill, never fabricated).

## Cross-refs

`state-volatile-v1.json` (volatile paths) · `state-map-v1.json` (persistent paths) ·
`parity-state-producer.md` §ST-04 (the report this fills) · `../plans/parity-evidence-roadmap.md`
§7 ST-05 · engine owners: `../findings/customer-service-haggle-RE.md` §23 (closeness), `news-daily-RE.md`
(news), quirk #133 (encyclopedia), `parity-save-producer.md` (save_io_commit_slot).
