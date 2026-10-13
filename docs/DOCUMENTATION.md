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

- Use reasoning tiers `R3`, `R2`, and `R1` when describing delegation.
- Do not use double-bracket private auto-memory/wiki references in authoritative docs.
  Link a real repository file/heading.
- Distinguish source inventory, runtime coverage, proof, human confirmation, join, and
  replay; never let one stand in for another.
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

No automated checker. `tools/ci/check_docs.py` (link check + a CI ban on model names
in docs) was retired on 2026-10-13 with the rest of the 2026-07 proof tooling
(`archive/2026-10-13-agent-era-cleanup.md`). Check local links by hand when editing an
entry point.
