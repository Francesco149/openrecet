# EP-08 — cache + proof re-keyed by full capture provenance (2026-07-16)

> Wave-0 EP-08 of `docs/plans/parity-evidence-roadmap.md` §6. Closes the M0
> adversarial-review **HOLE-2** (`parity-M0-adversarial-review.md`). Vocabulary:
> `docs/reference/parity-vocabulary.md`. Landed in two commits (B then A).

## Why

The M0 review found the container-provenance defense was **built but not wired**:
`resolve_observations` called `adapt_pixels`/`adapt_render_program` with
`expected_containers=None`, so `verify_source_containers` never ran. A foreign or
stale metrics doc with matching frame identities `(anchor,occ,offset)` but from a
**different capture** would be trusted (`differ==0` → false PASS). The review named
this THE hard gate: *no pixels/state producer's PASS may be trusted until it closes.*
The "real fix" = a content hash of the capture container **keyed by full provenance**,
then thread `source`/`expected_containers` from it — i.e. two coupled pieces.

## B — bind content pillars to the window's container (`62ece6e`, the HOLE-2 close)

The metrics doc is now bound to the EXACT container this window's identity join +
draw report were built from:

- `orv3_view.write_view_json` bakes `port_container_sha256` / `retail_container_sha256`
  = `sha256(v3cap.bin)` for each side (the content hash, not just the WSL path the
  viewer already had). ~0.3 s/side, only on `--view`.
- `parity_prove.resolve_observations` reads those two hashes → threads them as
  `source` (the in-process render bridge's provenance claim) **and**
  `expected_containers` into `adapt_render_program` **and** `adapt_pixels`. A pre-EP08
  view without the hashes ⇒ the check is SKIPPED **and** a caveat is emitted (never a
  silent trust). `resolve_observations` now returns a 4th value (caveats).

Vacuous-but-honest for render TODAY (its `source` and `expected` both come from
view.json — render's provenance IS view.json, regenerated in-process each `--view`).
Non-vacuous for the future out-of-band pixels/state producer: it bakes into `source`
the container **it** read; `expected_containers` = the container THIS window's join
used; a mismatch ⇒ INCONCLUSIVE.

Regression (`test_parity_prove.test_container_provenance`, +8 checks): foreign
`source` → INCONCLUSIVE (the HOLE-2 attack), omitted `source` under a bound view →
INCONCLUSIVE (strict), matching → PASS + `render-metrics.json` stamped, legacy view →
skip + caveat.

## A — v3 studio cache re-keyed by full provenance (`5713074`, roadmap §6 EP-08)

The cache key was `sha256(trace+arm)[:8]` (32 bits, trace+arm only) — a rebuilt d3d
proxy or an edited frida agent **never** invalidated the cached container, so B's
container hash was only trustworthy once the container itself is provenance-bound.

- **SHARED dir key** = `sha256(common_provenance)+arm`, **128 bits** (`[:32]`), over
  `common_provenance` = `{cache_schema, trace_sha (⇒ {savefile} save), proxy_sha,
  assets_manifest_sha, recet_ini_sha}`. A proxy/assets/trace/schema change re-drives
  **both** sides (they all affect both containers; the proxy is staged next to the
  port AND the retail exe).
- **PER-SIDE provenance** `{pe_sha256, agent_sha256}` stored in `v3meta.prov`,
  validated on lookup by `side_provenance` + `_staleness`. Port = its exe (agent
  `@none`); retail = its exe + the frida agent. A rebuilt exe or edited agent
  re-drives **only that side** — so a port fix still never invalidates the retail
  cache (the load-bearing design invariant). PE/agent are kept OUT of the shared key
  precisely so they can't cross-invalidate.
- `find_extent` rejects a missing/empty `v3cap.bin` (corrupt) and **logs every stale
  decision** with the reason (`pre-EP08` / shared-drift / per-side-drift / corrupt) —
  "explain every stale decision."

Acceptance mapping (roadmap §6 EP-08): changing port PE → port only; retail PE → retail
only; agent → retail only; proxy/assets/config/trace/schema → both; ≥128 visible bits;
save via the trace's `{savefile}` sha; corrupt rejected. All exercised in
`test_orv3.test_provenance_keying` by monkeypatching the provenance paths to temp files
and flipping one byte at a time.

**One-time consequence:** every pre-EP08 entry (8-hex key, `prov=None`) is now STALE ⇒
the next `orv3_window` re-drives it once (retail = the serialized load-stretch,
minutes). Old dirs orphan under `runs/studio-v3-cache/` (gitignored, regenerable).
`orv3_window.port_stale`'s mtime check is kept as a harmless conservative extra (the PE
hash is the authoritative guard now; a no-op rebuild still re-drives the port via mtime,
as before).

## Residual / follow-ups (logged, not blocking)

- **Proof `tools` group is still current-on-disk**, not read from the window's stored
  `v3meta.prov`. EP-08 binds the CACHE to proxy+agent (a served window matched them at
  drive time), but a tool rebuilt *between drive and prove* is misrecorded in the proof.
  Fix = thread `v3meta.prov` into `gather_provenance`'s `tools`. Disclosed by the
  from-cache caveat. (EP-02/EP-05 territory, not EP-08's cache acceptance.)
- **HOLE-3** (environment operator-attested) and **HOLE-4** (exceptions not
  gate-enforced) remain by-design deferrals (CI-05 / EP-07); unchanged.
- `orv3_window.port_stale` could be dropped in favor of the PE-hash guard to avoid
  no-op-rebuild re-drives (a papercut, not a bug). Deferred to keep this commit focused.

## Verdict

HOLE-2 is CLOSED: a foreign/stale content-metrics doc is now rejected (INCONCLUSIVE),
and the container it is bound to is itself provenance-keyed, so a pixels/state producer
may now ship a PASS-capable adapter (the gate the M0 review set). The evidence compiler's
Wave-0 (EP-00→EP-08) is complete.
