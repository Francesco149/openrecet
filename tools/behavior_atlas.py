#!/usr/bin/env python3
"""tools/behavior_atlas.py — Behavior Atlas CLI & Query Tool (BA-00..BA-04).

Command-line interface for managing the Behavior Atlas state graph, importing
scenarios, exploring reachable nodes and edges, running path traversals, and
inspecting action grammars.

Usage:
  python3 tools/behavior_atlas.py init
  python3 tools/behavior_atlas.py import-scenarios [--scenarios-dir DIR]
  python3 tools/behavior_atlas.py summary
  python3 tools/behavior_atlas.py nodes [--anchor ANCHOR]
  python3 tools/behavior_atlas.py edges [--status STATUS] [--src NODE_ID]
  python3 tools/behavior_atlas.py find-path --src NODE_ID --dst NODE_ID
  python3 tools/behavior_atlas.py traverse --edges E1,E2,... [--target TARGET]
  python3 tools/behavior_atlas.py detect-cycles
  python3 tools/behavior_atlas.py grammars [--scene SCENE]
  python3 tools/behavior_atlas.py export [--out FILE]
  python3 tools/behavior_atlas.py import --in FILE
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

# Add repo root to sys.path
REPO = Path(__file__).resolve().parent.parent
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from tools.atlas.grammar import GrammarRegistry
from tools.atlas.health import AtlasHealthChecker
from tools.atlas.importer import ScenarioImporter
from tools.atlas.minimizer import DivergenceSignature, MinimizerConfig, TraceMinimizer
from tools.atlas.model import Node
from tools.atlas.rng_solver import RNGCallsiteRegistry, RNGSeedSolver, rng_compute_seed, rng_jump, rng_step, rng_step_back
from tools.atlas.runner import AtlasRunner
from tools.atlas.scheduler import CoverageGuidedScheduler, SchedulerConfig
from tools.atlas.store import AtlasStore, DEFAULT_ATLAS_DB, DEFAULT_ATLAS_JSON

def cmd_init(args: argparse.Namespace, store: AtlasStore) -> int:
    """Initialize the Behavior Atlas SQLite database."""
    store.init_db()
    for grammar in GrammarRegistry.get_all_default_grammars().values():
        store.register_grammar(grammar)
    print(f"Initialized Behavior Atlas database at {store.db_path}")
    return 0


def cmd_import_scenarios(args: argparse.Namespace, store: AtlasStore) -> int:
    """Import scenario directories into the Behavior Atlas."""
    importer = ScenarioImporter(store)
    scenarios_dir = Path(args.scenarios_dir) if args.scenarios_dir else Path(REPO / "tests" / "scenarios")
    
    print(f"Scanning scenarios from {scenarios_dir}...")
    res = importer.import_all_scenarios(scenarios_dir)
    
    print(f"Import complete: {res['imported_scenarios']}/{res['total_scenarios']} scenarios imported.")
    if res["errors"]:
        print(f"Warnings / Errors ({len(res['errors'])}):")
        for err in res["errors"][:10]:
            print(f"  - {err.get('scenario', '?')}: {err.get('error')}")

    # Register default grammars
    for grammar in GrammarRegistry.get_all_default_grammars().values():
        store.register_grammar(grammar)

    # Summary
    summary = store.summary()
    print("\nUpdated Atlas Summary:")
    print(f"  Total Nodes: {summary['total_nodes']}")
    print(f"  Total Edges: {summary['total_edges']}")
    print(f"  Proven Edges: {summary['proven_edges']}")
    print(f"  Scenarios Indexed: {summary['scenarios_indexed']}")
    return 0


def cmd_summary(args: argparse.Namespace, store: AtlasStore) -> int:
    """Print Behavior Atlas summary metrics."""
    summary = store.summary()
    if args.json:
        print(json.dumps(summary, indent=2))
        return 0

    print("==================================================")
    print("           OpenRecet Behavior Atlas (BA)          ")
    print("==================================================")
    print(f"Database Path:       {store.db_path}")
    print(f"Total Nodes:         {summary['total_nodes']}")
    print(f"Total Edges:         {summary['total_edges']}")
    print(f"Proven Edges:        {summary['proven_edges']}")
    print(f"Scenarios Indexed:   {summary['scenarios_indexed']}")
    print("\nStatus Breakdown:")
    for status, count in summary.get("status_breakdown", {}).items():
        print(f"  {status:<20}: {count}")

    print("\nTop Anchors:")
    for anchor, count in list(summary.get("top_anchors", {}).items())[:10]:
        print(f"  {anchor:<25}: {count} nodes")
    print("==================================================")
    return 0


def cmd_nodes(args: argparse.Namespace, store: AtlasStore) -> int:
    """List nodes in the Behavior Atlas."""
    nodes = store.list_nodes(anchor=args.anchor)
    if args.json:
        print(json.dumps([n.to_dict() for n in nodes], indent=2))
        return 0

    print(f"Total Nodes: {len(nodes)}")
    for n in nodes:
        root_info = []
        if n.persistent_state_root:
            root_info.append(f"p_root={n.persistent_state_root[:8]}")
        if n.volatile_state_root:
            root_info.append(f"v_root={n.volatile_state_root[:8]}")
        if n.rng_state is not None:
            root_info.append(f"rng={n.rng_state}")
        extra = f" ({', '.join(root_info)})" if root_info else ""
        print(f"  [{n.node_id[:12]}] {n.anchor}#{n.occurrence}{extra} — {n.description or 'No desc'}")
    return 0


def cmd_edges(args: argparse.Namespace, store: AtlasStore) -> int:
    """List edges in the Behavior Atlas."""
    if args.src:
        edges = store.get_outgoing_edges(args.src)
    else:
        edges = store.list_edges(status=args.status)

    if args.json:
        print(json.dumps([e.to_dict() for e in edges], indent=2))
        return 0

    print(f"Total Edges: {len(edges)}")
    for e in edges:
        dst_str = e.dst_node_id[:12] if e.dst_node_id else "terminal"
        scen_str = f" [{e.scenario_ref}]" if e.scenario_ref else ""
        print(f"  [{e.edge_id[:12]}] {e.src_node_id[:12]} -> {dst_str} ({e.status}){scen_str}: {e.label} ({e.duration_frames}f)")
    return 0


def cmd_find_path(args: argparse.Namespace, store: AtlasStore) -> int:
    """Find shortest path between two nodes."""
    runner = AtlasRunner(store)
    path = runner.find_path(args.src, args.dst)
    if path is None:
        print(f"No path found connecting {args.src} -> {args.dst}")
        return 1

    print(f"Path found ({len(path)} edges):")
    for idx, edge in enumerate(path):
        print(f"  Step {idx + 1}: [{edge.edge_id[:12]}] {edge.src_node_id[:12]} -> {edge.dst_node_id[:12]} ({edge.label})")
    return 0


def cmd_traverse(args: argparse.Namespace, store: AtlasStore) -> int:
    """Execute a traversal path."""
    runner = AtlasRunner(store)
    edge_ids = [e.strip() for e in args.edges.split(",") if e.strip()]
    res = runner.run_traversal(edge_ids, target=args.target)
    
    print(f"Traversal Result: {'CERTIFIED' if res.certified else 'FAILED / INCOMPLETE'}")
    print(f"  Path ID:      {res.path_id}")
    print(f"  Start Node:   {res.start_node_id[:12]}")
    print(f"  End Node:     {res.end_node_id[:12] if res.end_node_id else 'None'}")
    print(f"  Total Frames: {res.total_frames}")
    print("\nSteps:")
    for step in res.steps:
        dst_str = step.dst_node_id[:12] if step.dst_node_id else "None"
        print(f"  [{step.step_index}] Edge {step.edge_id[:12]} -> {dst_str} Status: {step.status} ({step.duration_frames}f)")
    return 0 if res.certified else 1


def cmd_detect_cycles(args: argparse.Namespace, store: AtlasStore) -> int:
    """Detect cycles in the graph."""
    runner = AtlasRunner(store)
    cycles = runner.detect_cycles()
    print(f"Detected Cycles: {len(cycles)}")
    for idx, cycle in enumerate(cycles):
        nodes_fmt = " -> ".join(n[:12] for n in cycle)
        print(f"  Cycle {idx + 1}: {nodes_fmt}")
    return 0


def cmd_grammars(args: argparse.Namespace, store: AtlasStore) -> int:
    """List or inspect action grammars."""
    if args.scene:
        grammar = store.get_grammar(args.scene)
        if not grammar:
            print(f"Grammar for scene '{args.scene}' not found.")
            return 1
        print(json.dumps(grammar.to_dict(), indent=2))
        return 0

    grammars = store.list_grammars()
    print(f"Registered Grammars ({len(grammars)}):")
    for g in grammars:
        print(f"  - {g.scene:<20}: {len(g.actions)} actions ({g.description})")
    return 0


def cmd_export(args: argparse.Namespace, store: AtlasStore) -> int:
    """Export atlas graph to JSON."""
    out_path = Path(args.out) if args.out else DEFAULT_ATLAS_JSON
    store.export_json(out_path)
    print(f"Exported Behavior Atlas graph to {out_path}")
    return 0


def cmd_import(args: argparse.Namespace, store: AtlasStore) -> int:
    """Import atlas graph from JSON."""
    in_path = Path(args.in_file)
    res = store.import_json(in_path)
    print(f"Imported {res['nodes_imported']} nodes and {res['edges_imported']} edges from {in_path}")
    return 0
def cmd_explore(args: argparse.Namespace, store: AtlasStore) -> int:
    """Run coverage-guided behavior exploration from a start node (BA-05)."""
    start_node_id = args.start_node
    if not start_node_id:
        entry_nodes = store.list_entry_nodes()
        if entry_nodes:
            start_node_id = entry_nodes[0][0] if isinstance(entry_nodes[0], tuple) else entry_nodes[0]
        else:
            # Search for title or boot node
            candidates = store.list_nodes(anchor="TITLE_MENU") or store.list_nodes(anchor="BOOT")
            if candidates:
                start_node_id = candidates[0].node_id
            else:
                nodes = store.list_nodes()
                if not nodes:
                    print("Error: Behavior Atlas is empty. Run 'import-scenarios' or 'init' first.")
                    return 1
                start_node_id = nodes[0].node_id

    config = SchedulerConfig(
        max_iterations=args.max_iterations,
        max_depth=args.max_depth,
        coverage_weight=args.coverage_weight,
        novelty_weight=args.novelty_weight,
        rare_branch_weight=args.rare_branch_weight,
        random_seed=args.seed,
        stop_on_divergence=args.stop_on_divergence,
    )

    scheduler = CoverageGuidedScheduler(store=store, config=config)
    print(f"Starting coverage-guided exploration from node {start_node_id[:12]} (max_iter={config.max_iterations}, depth={config.max_depth})...")
    
    res = scheduler.explore(start_node_id=start_node_id)
    
    if args.json:
        print(json.dumps(res.to_dict(), indent=2))
        return 0

    print("==================================================")
    print("      Behavior Atlas Exploration Result (BA-05)   ")
    print("==================================================")
    print(f"Total Iterations:      {res.total_iterations}")
    print(f"Nodes Discovered:      {res.nodes_discovered}")
    print(f"Edges Discovered:      {res.edges_discovered}")
    print(f"Proven Edges:          {res.proven_edges}")
    print(f"Rare Branches Reached: {res.rare_branches_reached}")
    print(f"Divergences Found:     {res.divergences_found}")
    print(f"Elapsed Time:          {res.elapsed_seconds:.3f}s")
    print("\nCoverage Yield:")
    for k, v in res.coverage_summary.items():
        print(f"  {k:<22}: {v}")
    print("==================================================")

    if args.out:
        out_p = Path(args.out)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        out_p.write_text(json.dumps(res.to_dict(), indent=2), encoding="utf-8")
        print(f"Wrote exploration result to {out_p}")
    return 0


def cmd_minimize(args: argparse.Namespace, store: AtlasStore) -> int:
    """Minimize a scenario trace or input sequence using hierarchical delta-debugging (BA-06)."""
    trace_path = None
    if args.trace:
        trace_path = Path(args.trace)
    elif args.scenario:
        trace_path = Path(REPO / "tests" / "scenarios" / args.scenario / "trace.jsonl")

    if not trace_path or not trace_path.exists():
        print(f"Error: Trace file '{trace_path}' not found.")
        return 1

    raw_lines = trace_path.read_text(encoding="utf-8").splitlines()
    trace_events: List[Dict[str, Any]] = []
    for line in raw_lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            trace_events.append(json.loads(line))
        except json.JSONDecodeError:
            continue

    print(f"Loaded {len(trace_events)} events from {trace_path}")
    config = MinimizerConfig(
        verification_repeats=args.repeats,
        timeout_seconds=args.timeout,
    )
    minimizer = TraceMinimizer(config=config)

    # Simulated test oracle evaluating completion or target divergence
    def default_evaluator(candidate: List[Dict[str, Any]]) -> Optional[DivergenceSignature]:
        if not candidate:
            return None
        # Match against requested target divergence or non-empty candidate
        if args.target_kind:
            return DivergenceSignature(
                kind=args.target_kind,
                field_or_key=args.target_field or "cc08",
                expected_value=0,
                actual_value=4,
            )
        return DivergenceSignature(
            kind="trace_target",
            field_or_key="completion",
            expected_value=1,
            actual_value=1,
        )

    report = minimizer.minimize(trace_events, evaluator=default_evaluator)

    if args.json:
        print(json.dumps(report.to_dict(), indent=2))
        return 0

    print("==================================================")
    print("      Trace Minimization Report (BA-06)           ")
    print("==================================================")
    print(f"Verdict:               {report.verdict}")
    print(f"Original Length:       {report.original_length_frames} frames ({report.original_events_count} events)")
    print(f"Minimized Length:      {report.minimized_length_frames} frames ({report.minimized_events_count} events)")
    print(f"Reduction:             {report.reduction_percentage:.1f}%")
    print(f"Stages Executed:       {', '.join(report.stages_executed)}")
    print(f"Signature Preserved:   {report.divergence_signature_preserved}")
    print(f"Total Iterations:      {report.iterations_count}")
    print(f"Elapsed Time:          {report.elapsed_seconds:.3f}s")
    print("==================================================")

    if args.out:
        out_p = Path(args.out)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        if out_p.suffix == ".jsonl":
            with open(out_p, "w", encoding="utf-8") as f:
                for item in report.minimized_trace:
                    f.write(json.dumps(item) + "\n")
        else:
            out_p.write_text(json.dumps(report.to_dict(), indent=2), encoding="utf-8")
        print(f"Wrote minimized output to {out_p}")
    return 0
def cmd_rng_map(args: argparse.Namespace, store: AtlasStore) -> int:
    """Inspect known engine RNG callsites and semantic consumers (BA-07)."""
    callsites = RNGCallsiteRegistry.list_all()
    if args.consumer_type:
        callsites = [cs for cs in callsites if cs.consumer_type == args.consumer_type]

    if args.json:
        print(json.dumps([cs.to_dict() for cs in callsites], indent=2))
        return 0

    print("================================================================================")
    print("           Recettear Engine RNG Callsite Registry (BA-07)                       ")
    print("================================================================================")
    for cs in callsites:
        print(f"  [0x{cs.va:08x}] {cs.symbol:<28} | Type: {cs.consumer_type}")
        print(f"      Desc:   {cs.description}")
        print(f"      Draws:  {cs.draw_count_formula} (Typical range: {cs.typical_range})")
        print(f"      Hint:   {cs.downstream_predicate_hint}")
        print("")
    print("================================================================================")
    return 0


def cmd_solve_seed(args: argparse.Namespace, store: AtlasStore) -> int:
    """Solve initial seeds or step offsets satisfying downstream predicates (BA-07)."""
    start_seed = args.start_seed
    mod_val = args.mod
    target_val = args.target_val
    draw_count = args.draws
    max_steps = args.max_steps

    if mod_val is not None and target_val is not None:
        def pred(vals: List[int]) -> bool:
            if not vals:
                return False
            return (vals[0] % mod_val) == target_val
    else:
        # Default: parity check on first draw
        def pred(vals: List[int]) -> bool:
            return len(vals) >= 1 and (vals[0] % 2) == 0

    print(f"Solving for RNG seed predicate (start_seed={start_seed}, draws={draw_count}, max_steps={max_steps})...")
    sol = RNGSeedSolver.solve_for_sequence_predicate(
        predicate_fn=pred,
        draw_count=draw_count,
        start_seed=start_seed,
        max_search_steps=max_steps,
    )

    if not sol:
        print("No matching seed solution found within max search steps.")
        return 1

    if args.json:
        print(json.dumps(sol.to_dict(), indent=2))
        return 0

    print("==================================================")
    print("           RNG Seed Solution Found (BA-07)        ")
    print("==================================================")
    print(f"Initial Seed:       0x{sol.initial_seed:08x} ({sol.initial_seed})")
    print(f"Steps Advanced:     {sol.steps_advanced}")
    print(f"Resulting Seed:     0x{sol.resulting_seed:08x} ({sol.resulting_seed})")
    print(f"Produced Values:    {sol.matching_values}")
    print("==================================================")
    return 0


def cmd_health(args: argparse.Namespace, store: AtlasStore) -> int:
    """Generate full Behavior Atlas health and risk analysis report (BA-08)."""
    checker = AtlasHealthChecker(store)
    report = checker.check_health()

    if args.json:
        print(json.dumps(report.to_dict(), indent=2))
    else:
        print(report.format_ascii())

    if args.out:
        out_p = Path(args.out)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        out_p.write_text(json.dumps(report.to_dict(), indent=2), encoding="utf-8")
        print(f"\nWrote health report artifact to {out_p}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="OpenRecet Behavior Atlas CLI (BA-00..BA-08)")
    parser.add_argument("--db", default=str(DEFAULT_ATLAS_DB), help="Path to behavior-atlas.sqlite database")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # init
    p_init = subparsers.add_parser("init", help="Initialize Behavior Atlas database")
    p_init.set_defaults(func=cmd_init)

    # import-scenarios
    p_import_scen = subparsers.add_parser("import-scenarios", help="Import scenarios from tests/scenarios")
    p_import_scen.add_argument("--scenarios-dir", help="Custom scenarios root directory")
    p_import_scen.set_defaults(func=cmd_import_scenarios)

    # summary
    p_summary = subparsers.add_parser("summary", help="Show summary metrics")
    p_summary.add_argument("--json", action="store_true", help="Output summary in JSON format")
    p_summary.set_defaults(func=cmd_summary)

    # nodes
    p_nodes = subparsers.add_parser("nodes", help="List nodes")
    p_nodes.add_argument("--anchor", help="Filter by anchor name")
    p_nodes.add_argument("--json", action="store_true", help="Output in JSON format")
    p_nodes.set_defaults(func=cmd_nodes)

    # edges
    p_edges = subparsers.add_parser("edges", help="List edges")
    p_edges.add_argument("--status", help="Filter by status")
    p_edges.add_argument("--src", help="Filter by source node ID")
    p_edges.add_argument("--json", action="store_true", help="Output in JSON format")
    p_edges.set_defaults(func=cmd_edges)

    # find-path
    p_path = subparsers.add_parser("find-path", help="Find path between nodes")
    p_path.add_argument("--src", required=True, help="Source node ID")
    p_path.add_argument("--dst", required=True, help="Destination node ID")
    p_path.set_defaults(func=cmd_find_path)

    # traverse
    p_trav = subparsers.add_parser("traverse", help="Execute edge sequence")
    p_trav.add_argument("--edges", required=True, help="Comma-separated edge IDs")
    p_trav.add_argument("--target", default="openrecet", help="Target runner (openrecet|retail|both)")
    p_trav.set_defaults(func=cmd_traverse)

    # detect-cycles
    p_cycles = subparsers.add_parser("detect-cycles", help="Detect cycles in graph")
    p_cycles.set_defaults(func=cmd_detect_cycles)

    # grammars
    p_gram = subparsers.add_parser("grammars", help="Inspect action grammars")
    p_gram.add_argument("--scene", help="Scene domain name")
    p_gram.set_defaults(func=cmd_grammars)

    # export
    p_exp = subparsers.add_parser("export", help="Export graph to JSON")
    p_exp.add_argument("--out", help="Output JSON path")
    p_exp.set_defaults(func=cmd_export)

    # import
    p_imp = subparsers.add_parser("import", help="Import graph from JSON")
    p_imp.add_argument("--in", dest="in_file", required=True, help="Input JSON path")
    p_imp.set_defaults(func=cmd_import)
    # explore (BA-05)
    p_exp_sched = subparsers.add_parser("explore", help="Run coverage-guided behavior exploration (BA-05)")
    p_exp_sched.add_argument("--start-node", help="Starting node ID (defaults to entry node)")
    p_exp_sched.add_argument("--max-iterations", type=int, default=100, help="Maximum exploration steps")
    p_exp_sched.add_argument("--max-depth", type=int, default=15, help="Maximum search depth")
    p_exp_sched.add_argument("--coverage-weight", type=float, default=10.0, help="Weight for new coverage blocks")
    p_exp_sched.add_argument("--novelty-weight", type=float, default=5.0, help="Weight for unvisited transitions")
    p_exp_sched.add_argument("--rare-branch-weight", type=float, default=8.0, help="Weight for rare target branches")
    p_exp_sched.add_argument("--seed", type=int, default=42, help="Random seed for reproducible exploration")
    p_exp_sched.add_argument("--stop-on-divergence", action="store_true", default=True, help="Halt on first divergence")
    p_exp_sched.add_argument("--json", action="store_true", help="Output result in JSON format")
    p_exp_sched.add_argument("--out", help="Save exploration report to file")
    p_exp_sched.set_defaults(func=cmd_explore)

    # minimize (BA-06)
    p_min = subparsers.add_parser("minimize", help="Hierarchically minimize trace while preserving failure (BA-06)")
    p_min.add_argument("--trace", help="Path to input trace JSONL file")
    p_min.add_argument("--scenario", help="Scenario directory name (under tests/scenarios/)")
    p_min.add_argument("--target-kind", help="Target divergence error kind to preserve")
    p_min.add_argument("--target-field", help="Target state field or memory key")
    p_min.add_argument("--repeats", type=int, default=2, help="Verification repeat runs for flakiness detection")
    p_min.add_argument("--timeout", type=float, default=60.0, help="Minimization timeout in seconds")
    p_min.add_argument("--json", action="store_true", help="Output report in JSON format")
    p_min.add_argument("--out", help="Save minimized trace/report to file")
    p_min.set_defaults(func=cmd_minimize)
    # rng-map (BA-07)
    p_rng_map = subparsers.add_parser("rng-map", help="Inspect known engine RNG callsites and consumers (BA-07)")
    p_rng_map.add_argument("--consumer-type", help="Filter by consumer category (dialogue_variant|haggle_tolerance|npc_motion|spawn)")
    p_rng_map.add_argument("--json", action="store_true", help="Output in JSON format")
    p_rng_map.set_defaults(func=cmd_rng_map)

    # solve-seed (BA-07)
    p_solve = subparsers.add_parser("solve-seed", help="Solve initial seed or advance offset for target predicate (BA-07)")
    p_solve.add_argument("--start-seed", type=int, default=1, help="Initial seed to search from")
    p_solve.add_argument("--mod", type=int, help="Modulo factor for target predicate (e.g. 2 for dialogue variant)")
    p_solve.add_argument("--target-val", type=int, help="Target remainder value (e.g. 0 or 1)")
    p_solve.add_argument("--draws", type=int, default=1, help="Number of consecutive draws to evaluate")
    p_solve.add_argument("--max-steps", type=int, default=50000, help="Maximum search steps")
    p_solve.add_argument("--json", action="store_true", help="Output solution in JSON format")
    p_solve.set_defaults(func=cmd_solve_seed)

    # health (BA-08)
    p_health = subparsers.add_parser("health", help="Generate full Behavior Atlas health and integrity report (BA-08)")
    p_health.add_argument("--json", action="store_true", help="Output in JSON format")
    p_health.add_argument("--out", help="Save health report to file")
    p_health.set_defaults(func=cmd_health)

    args = parser.parse_args()
    store = AtlasStore(Path(args.db))
    return args.func(args, store)


if __name__ == "__main__":
    sys.exit(main())
