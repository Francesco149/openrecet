# Documentation ownership and staleness policy

> **Status:** authoritative repository convention  \
> **Last verified:** 2026-07-16

OpenRecet accumulates large amounts of reverse-engineering evidence. Historical detail is
valuable; contradictory “current” instructions are not. Every document belongs to one
class.

## Document classes

| Class | Paths/examples | Rule |
|---|---|---|
| Entry/policy | `CLAUDE.md`, `PLAN.md`, `AGENT-WORKFLOW.md`, this file | must describe current practice; checked on relevant changes |
| Current front | `FRONT.md` | only open/forward work; one hand-edited source |
| Derived status | `STATUS.md`, `port-ledger.*`, `port-debt.*` | generated; never hand-edit generated sections |
| Active plans | `plans/README.md`, non-archive plan files | stable IDs, status header, dependencies, acceptance, build log |
| Operational guides | `trace-workflow.md`, live-probe and focused playbooks | commands must work now; legacy flow goes to archive |
| Durable findings | `findings/` | evidence and conclusions; may grow, not carry live priority |
| Dated audits | `audits/` | snapshot/decision record; add a supersession banner, do not rewrite history |
| Archive | `archive/`, `plans/archive/` | retained for provenance; explicitly non-operational |
| Narrative history | `PROGRESS.md` | append-only chronology; never source current status from it |

## No duplicated live truth

- Coverage/function/debt counts come from generated status. README and plans link to it
  rather than copying a number.
- Current target lives in `FRONT.md`; plans describe durable scope and work packages.
- Tool command ownership lives in one operational guide. Other docs link to it.
- A superseded file is moved to archive or receives a clear dated banner and pointer.
- Historical conclusions remain dated. Do not silently “freshen” their old measurements.

## Stable terminology

- Use reasoning tiers `R3`, `R2`, and `R1`; do not encode current provider/model names in
  workflow policy.
- Do not use double-bracket private auto-memory/wiki references in authoritative docs.
  Link a real repository file/heading.
- Distinguish source inventory, runtime coverage, proof, human confirmation, join, and
  replay. Definitions live in `plans/parity-evidence-roadmap.md`.
- Avoid “current,” “today,” or mutable numeric claims without a date or generated source.

## Active plan requirements

An active plan starts with:

```text
Status
Last verified/adopted date
Owner/reasoning tier where relevant
Scope and supersession pointer
```

Executable work packages include:

- stable ID;
- reasoning tier;
- dependencies;
- read/write set or planned files;
- ordered implementation procedure;
- acceptance and negative test;
- stop/escalation conditions;
- dated build log.

Completed plans move to `plans/archive/` unless retained as an explicitly marked
done-as-scoped reference.

## Link and convention checks

Run:

```sh
nix develop --command python3 tools/ci/check_docs.py
```

The checker validates local Markdown links and rejects known fragile conventions in the
current entry points, operational guides, front, and every non-archive plan. New plan
files are discovered automatically. Archive/audit/finding prose is intentionally
excluded from style/staleness checks, but local links may be audited separately when
those files are actively edited.

When adding another authoritative entry point or operational guide outside `docs/plans/`,
add it to `CURRENT_ENTRY_AND_GUIDE_DOCS` in the checker and to the source-of-truth table
in `PLAN.md`.

## Change procedure

1. Identify which document owns the truth.
2. Update that source first.
3. Regenerate derived views.
4. Replace duplicates with links.
5. Archive or banner superseded instructions.
6. Run the documentation checker.
7. Review the diff for accidental rewriting of historical evidence.
