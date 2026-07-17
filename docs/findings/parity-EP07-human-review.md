# EP-07 — bridge human confirmations (additive, non-hashed, verdict-preserving)

**Landed 2026-07-17.** The proof bundle's `human_review` field goes from an unused
`null` stub to a first-class, working human-attestation layer that is ADDITIVE and
cannot override a machine verdict. Roadmap: `../plans/parity-evidence-roadmap.md` EP-07.

## The blocker the prior session recorded (commit f29f553)

`human_review` was a required top-level field but sat in the HASHED core
(`canonical.NON_HASHED = ("proof_id","envelope")` did NOT list it) ⇒ attaching a
review would CHANGE `proof_id`, contradicting §4.4 ("proof_id excludes … human display
notes"). EP-07 was deferred pending an R3 canonicalization decision.

## R3 decision — `human_review` → `NON_HASHED` (not the envelope)

Two options were sanctioned (move to the envelope, OR add to `NON_HASHED`). Chose
**add to `NON_HASHED`**:

- **Smallest blast radius on the FROZEN schema.** The schema SHAPE is UNCHANGED —
  `human_review` stays a required, first-class top-level field (sibling to
  `exceptions`, not demoted into the envelope grab-bag). No required-field removal ⇒
  not a breaking schema change ⇒ **no major version bump**. The change is confined to
  the *canonicalization rule* (`canonical.py`), which §4.4 already places under R3
  authority ("Canonicalization rules … require R3 approval").
- **Conforms to §4.4's frozen intent.** §4.4 already says proof_id excludes "human
  display notes"; the old placement was a latent inconsistency. This is a correction.
- **Envelope stays clean.** `NON_HASHED` remains "proof_id + the two non-hashed
  metadata keys"; the envelope's description no longer over-claims exclusivity over
  human notes (structured attestation is its own top-level non-hashed sibling).

**Rejected: move to envelope** (bigger schema churn, required→optional move, arguably a
major bump) and **a v2 bump** (manufactures a version history for bundles that don't
meaningfully exist — proof_ids are advisory, the durable key is `contract_sha256`, and
no bundle ever carried a non-null review).

## What landed

- **`tools/parity/canonical.py`** — `NON_HASHED = ("proof_id","envelope","human_review")`.
- **`tools/parity/prove.py:attach_human_review(proof, review, *, required_pillars)`** —
  returns a NEW proof with `human_review` populated. Additive: asserts
  review-neutrality under the CURRENT rule (`proof_id_of(reviewed) == proof_id_of(proof)`
  — recomputed, NOT compared to the possibly-stale stored id). Read-only over the machine
  gate (`gate()` reads `pillars`). Stamps `machine_verdict` (the §4.1 gate at review
  time). A CONFIRMING verdict over a non-PASS gate is recorded `confirmed-despite-<MACHINE>`
  (e.g. `confirmed-despite-FAIL`) — explicit + scoped, never a silent pass. `HUMAN_VERDICTS
  = ("confirmed","rejected","noted")`; malformed reviews raise `ValueError`. `summarize()`
  carries `human_review` but the exit code stays machine-driven.
- **`tools/parity_review.py`** — the CLI. `parity_review.py <bundle_dir> --reviewer …
  --date … --scope … [--verdict confirmed|rejected|noted] [--notes] [--confirmed-pillars
  a,b] [--required-pillars a,b] [--json]`. `required_pillars` auto-resolve from the
  bundle's own `inputs.scenario_contract.id`, VERIFIED against the recorded
  `contract_sha256` (drift ⇒ exit 2, "pass --required-pillars"). Writes the review back
  into the SAME content-addressed bundle in place (a non-hashed amendment, like the
  envelope; proof_id unchanged ⇒ same CAS path). **Exit = the MACHINE gate's code** — a
  human verdict never moves it. Caveats a bundle whose stored proof_id predates this rule.
- **Schema** (`parity-proof-v1.schema.json`) — `human_review` object gains `verdict`
  (enum incl. `confirmed-despite-{FAIL,INCONCLUSIVE}`), `machine_verdict`
  (`PASS|FAIL|INCONCLUSIVE`), `confirmed_pillars`; descriptions note it is non-hashed,
  additive, and can't override a machine verdict. Envelope/top descriptions updated.
- **Docs** — `../reference/parity-proof-format.md` (canonicalization + a new "Human
  review" section + the R3-refinement note + the eleven→twelve group miscount fix).

## Acceptance — VERIFIED

EP-07 acceptance: *"human confirmation cannot override a failed machine-required
pillar; deferred divergences retain explicit failing/exception scope."*

- **Host tests:** `test_parity_prove.py` `test_human_review` + `test_review_cli` (18
  new checks; suite 72/0) — attach over PASS keeps verdict/proof_id/exit; attach
  `confirmed` over FAIL → `confirmed-despite-FAIL`, proof_id unchanged, gate STILL exit
  1; INCONCLUSIVE → `confirmed-despite-INCONCLUSIVE`; `rejected` preserved; malformed →
  raise; a STALE-ID bundle still accepts a review (regression). `test_parity_schema.py`
  — `human_review ∈ NON_HASHED` + mutating it does not change proof_id.
- **End-to-end on the REAL `house-firstcust-arrprobe` bundle** (copied to scratch,
  non-destructive): auto-resolved `required_pillars` from the live contract, attached
  `verdict=confirmed` → recorded **`confirmed-despite-FAIL`** (arrprobe's honest
  sub-perceptual pixel/render FAIL), `machine_verdict=FAIL`, CLI **exit 1**, staleness
  caveat emitted. This is EP-07's raison d'être: our most human-1:1-confirmed scene
  (visually 1:1, not bit-exact) can now carry a scoped, auditable attestation that never
  flips the machine's honest FAIL.

## Notes / follow-ups

- **NB one-time:** pre-EP-07 bundles' `proof_id`s hashed a `human_review:null` into the
  core ⇒ stale under the new rule ⇒ they re-address on the next `orv3_window`/`parity_prove`
  drive. Advisory only (durable key = `contract_sha256`); no persisted bundle carried a
  real review, so nothing relied-upon changes.
- **DEFERRED (opt-in, not started):** the `confirmed-parity-ledger` → structured-review-
  records migration stays "where practical" (roadmap EP-07 "Planned files"), not a rewrite.
