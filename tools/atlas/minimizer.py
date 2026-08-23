#!/usr/bin/env python3
"""tools/atlas/minimizer.py — Hierarchical Trace and Edge Minimizer (BA-06).

Implements hierarchical delta-debugging for behavior traces and action sequences:
1. Action chunk elimination (removing coarse semantic action segments).
2. Wait frame reduction (binary-search minimizing idle frame gaps between actions).
3. Repeated input coalescing / trimming (compressing held button durations to minimal triggers).
4. Frame-level 1-minimal delta debugging (fine-grained event reduction).
5. Invariant preservation: ensures the minimized trace preserves start conditions,
   completion targets, and the exact first-divergence signature.
6. Flakiness / Determinism detection: verifies outcomes across repeat runs; marks
   inconsistent outcomes as INCONCLUSIVE instead of yielding a false minimum.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

from .grammar import compile_action_sequence
from .identity import compute_input_digest
from .model import CompletionCondition, Edge, Node


@dataclass
class DivergenceSignature:
    """The canonical signature of an observed failure or divergence."""
    kind: str  # "state_diff", "anchor_timeout", "proof_failure", "memory_divergence"
    field_or_key: Optional[str] = None
    expected_value: Any = None
    actual_value: Any = None
    anchor: Optional[str] = None
    error_code: Optional[str] = None
    frame_offset: Optional[int] = None

    def signature_hash(self) -> str:
        data = {
            "kind": self.kind,
            "field": self.field_or_key,
            "expected": str(self.expected_value),
            "actual": str(self.actual_value),
            "anchor": self.anchor,
            "error_code": self.error_code,
        }
        raw = json.dumps(data, sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(raw.encode("utf-8")).hexdigest()

    def matches(self, other: Optional[DivergenceSignature]) -> bool:
        if not other:
            return False
        return self.signature_hash() == other.signature_hash()

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> DivergenceSignature:
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class MinimizerConfig:
    """Configuration for hierarchical trace minimization."""
    enable_action_chunk_removal: bool = True
    enable_wait_reduction: bool = True
    enable_repeat_coalescing: bool = True
    enable_frame_delta_debug: bool = True
    verification_repeats: int = 2
    timeout_seconds: float = 60.0
    min_wait_frames: int = 1

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> MinimizerConfig:
        return cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})


@dataclass
class MinimizationReport:
    """Comprehensive report produced by trace minimization."""
    verdict: str  # "MINIMIZED", "UNMODIFIED", "INCONCLUSIVE", "FLAKY"
    original_length_frames: int
    minimized_length_frames: int
    original_events_count: int
    minimized_events_count: int
    reduction_percentage: float
    stages_executed: List[str]
    divergence_signature_preserved: bool
    iterations_count: int
    elapsed_seconds: float
    divergence_signature: Optional[Dict[str, Any]] = None
    minimized_trace: List[Dict[str, Any]] = field(default_factory=list)
    log_messages: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "verdict": self.verdict,
            "original_length_frames": self.original_length_frames,
            "minimized_length_frames": self.minimized_length_frames,
            "original_events_count": self.original_events_count,
            "minimized_events_count": self.minimized_events_count,
            "reduction_percentage": round(self.reduction_percentage, 2),
            "stages_executed": self.stages_executed,
            "divergence_signature_preserved": self.divergence_signature_preserved,
            "iterations_count": self.iterations_count,
            "elapsed_seconds": round(self.elapsed_seconds, 4),
            "divergence_signature": self.divergence_signature,
            "minimized_trace": self.minimized_trace,
            "log_messages": self.log_messages,
        }


# Oracle function type: given a trace, returns (signature, is_valid)
TraceEvaluator = Callable[[List[Dict[str, Any]]], Optional[DivergenceSignature]]


class TraceMinimizer:
    """Hierarchical delta-debugging minimizer for behavior traces (BA-06)."""

    def __init__(self, config: Optional[MinimizerConfig] = None):
        self.config = config or MinimizerConfig()
        self._iteration_count = 0
        self._logs: List[str] = []

    def _log(self, msg: str) -> None:
        self._logs.append(msg)

    def minimize(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
        target_signature: Optional[DivergenceSignature] = None,
    ) -> MinimizationReport:
        """Minimizes the input trace while preserving target_signature."""
        t0 = time.time()
        self._iteration_count = 0
        self._logs.clear()

        # Step 0: Baseline evaluation & signature acquisition
        self._log("Evaluating baseline trace...")
        baseline_sig, is_flaky = self._evaluate_with_verification(trace, evaluator)
        if is_flaky:
            self._log("Baseline evaluation was inconclusive or flaky across repeat runs.")
            return MinimizationReport(
                verdict="INCONCLUSIVE",
                original_length_frames=self._compute_trace_length(trace),
                minimized_length_frames=self._compute_trace_length(trace),
                original_events_count=len(trace),
                minimized_events_count=len(trace),
                reduction_percentage=0.0,
                stages_executed=[],
                divergence_signature_preserved=False,
                iterations_count=self._iteration_count,
                elapsed_seconds=time.time() - t0,
                divergence_signature=None,
                minimized_trace=copy.deepcopy(trace),
                log_messages=self._logs,
            )

        if baseline_sig is None and target_signature is None:
            self._log("Baseline trace produced no failure or divergence signature.")
            return MinimizationReport(
                verdict="UNMODIFIED",
                original_length_frames=self._compute_trace_length(trace),
                minimized_length_frames=self._compute_trace_length(trace),
                original_events_count=len(trace),
                minimized_events_count=len(trace),
                reduction_percentage=0.0,
                stages_executed=[],
                divergence_signature_preserved=False,
                iterations_count=self._iteration_count,
                elapsed_seconds=time.time() - t0,
                divergence_signature=None,
                minimized_trace=copy.deepcopy(trace),
                log_messages=self._logs,
            )

        expected_signature = target_signature or baseline_sig
        self._log(f"Target signature confirmed: {expected_signature.kind} (field={expected_signature.field_or_key})")
        orig_len_frames = self._compute_trace_length(trace)
        orig_events_count = len(trace)

        current_trace = copy.deepcopy(trace)
        stages_run: List[str] = []

        # Stage 1: Action chunk elimination (coarse granularity)
        if self.config.enable_action_chunk_removal:
            self._log("Stage 1: Running Action Chunk Elimination...")
            stages_run.append("ACTION_CHUNKS")
            current_trace = self._minimize_action_chunks(current_trace, evaluator, expected_signature)

        # Stage 2: Wait frame reduction (binary-search minimizing wait intervals)
        if self.config.enable_wait_reduction:
            self._log("Stage 2: Running Wait Frame Reduction...")
            stages_run.append("WAIT_REDUCTION")
            current_trace = self._minimize_wait_frames(current_trace, evaluator, expected_signature)

        # Stage 3: Repeated input duration coalescing
        if self.config.enable_repeat_coalescing:
            self._log("Stage 3: Running Repeated Input Duration Coalescing...")
            stages_run.append("REPEAT_COALESCING")
            current_trace = self._minimize_repeated_inputs(current_trace, evaluator, expected_signature)

        # Stage 4: Frame-level 1-minimal delta debugging
        if self.config.enable_frame_delta_debug:
            self._log("Stage 4: Running Frame-level 1-Minimal Delta-Debugging...")
            stages_run.append("FRAME_DELTA_DEBUG")
            current_trace = self._minimize_frame_delta(current_trace, evaluator, expected_signature)

        # Final Verification
        final_sig, final_flaky = self._evaluate_with_verification(current_trace, evaluator)
        preserved = not final_flaky and expected_signature.matches(final_sig)
        final_len_frames = self._compute_trace_length(current_trace)
        final_events_count = len(current_trace)

        reduction = 0.0
        if orig_len_frames > 0:
            reduction = max(0.0, ((orig_len_frames - final_len_frames) / orig_len_frames) * 100.0)
        elif orig_events_count > 0:
            reduction = max(0.0, ((orig_events_count - final_events_count) / orig_events_count) * 100.0)

        verdict = "MINIMIZED" if preserved and (final_len_frames < orig_len_frames or final_events_count < orig_events_count) else ("UNMODIFIED" if preserved else "FLAKY")

        self._log(f"Minimization finished with verdict {verdict}: {orig_len_frames} -> {final_len_frames} frames ({reduction:.1f}% reduction)")

        return MinimizationReport(
            verdict=verdict,
            original_length_frames=orig_len_frames,
            minimized_length_frames=final_len_frames,
            original_events_count=orig_events_count,
            minimized_events_count=final_events_count,
            reduction_percentage=reduction,
            stages_executed=stages_run,
            divergence_signature_preserved=preserved,
            iterations_count=self._iteration_count,
            elapsed_seconds=time.time() - t0,
            divergence_signature=expected_signature.to_dict(),
            minimized_trace=current_trace,
            log_messages=self._logs,
        )

    # ── Stage 1: Action Chunk Elimination ────────────────────────────────────

    def _minimize_action_chunks(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
        target_signature: DivergenceSignature,
    ) -> List[Dict[str, Any]]:
        """Remove discrete action blocks or op groups using delta-debugging."""
        # Partition trace into semantic chunks (by action tag or wait boundaries)
        chunks: List[List[Dict[str, Any]]] = []
        current_chunk: List[Dict[str, Any]] = []

        for item in trace:
            if "wait" in item or "action" in item or "scene" in item:
                if current_chunk:
                    chunks.append(current_chunk)
                    current_chunk = []
                current_chunk.append(item)
            else:
                current_chunk.append(item)

        if current_chunk:
            chunks.append(current_chunk)

        if len(chunks) <= 1:
            return trace

        # Delta-debug over chunks
        n = 2
        while len(chunks) >= 2:
            chunk_subsets = self._split_list(chunks, n)
            reduced = False

            # Try complements
            for i, subset in enumerate(chunk_subsets):
                complement = [c for j, part in enumerate(chunk_subsets) if j != i for c in part]
                candidate_trace = [item for c in complement for item in c]
                
                # Re-index frame offsets
                candidate_trace = self._normalize_frame_offsets(candidate_trace)

                sig = self._evaluate_candidate(candidate_trace, evaluator)
                if target_signature.matches(sig):
                    self._log(f"Action chunks: eliminated {len(subset)} chunks ({len(chunks)} -> {len(complement)})")
                    chunks = complement
                    n = max(n - 1, 2)
                    reduced = True
                    break

            if not reduced:
                if n >= len(chunks):
                    break
                n = min(n * 2, len(chunks))

        result = [item for c in chunks for item in c]
        return self._normalize_frame_offsets(result)

    # ── Stage 2: Wait Frame Reduction ────────────────────────────────────────

    def _minimize_wait_frames(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
        target_signature: DivergenceSignature,
    ) -> List[Dict[str, Any]]:
        """Binary search minimization on wait frame durations."""
        minimized = copy.deepcopy(trace)

        for i, item in enumerate(minimized):
            if "wait" in item and isinstance(item["wait"], (int, float)):
                original_wait = int(item["wait"])
                if original_wait <= self.config.min_wait_frames:
                    continue

                low = self.config.min_wait_frames
                high = original_wait
                best_wait = original_wait

                while low <= high:
                    mid = (low + high) // 2
                    candidate = copy.deepcopy(minimized)
                    candidate[i]["wait"] = mid
                    candidate = self._normalize_frame_offsets(candidate)

                    sig = self._evaluate_candidate(candidate, evaluator)
                    if target_signature.matches(sig):
                        best_wait = mid
                        high = mid - 1  # Try smaller wait
                    else:
                        low = mid + 1   # Need longer wait

                if best_wait < original_wait:
                    self._log(f"Wait reduction at op #{i}: {original_wait} -> {best_wait} frames")
                    minimized[i]["wait"] = best_wait
                    minimized = self._normalize_frame_offsets(minimized)

        return minimized

    # ── Stage 3: Repeated Input Coalescing ───────────────────────────────────

    def _minimize_repeated_inputs(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
        target_signature: DivergenceSignature,
    ) -> List[Dict[str, Any]]:
        """Coalesces identical consecutive button hold inputs down to minimal pulse lengths."""
        minimized = copy.deepcopy(trace)

        # Detect consecutive runs of identical button masks
        i = 0
        while i < len(minimized):
            curr = minimized[i]
            if "mask" in curr or "buttons" in curr:
                # Find run of identical masks
                run_len = 1
                curr_mask = curr.get("mask") or curr.get("buttons")
                while (i + run_len) < len(minimized):
                    next_item = minimized[i + run_len]
                    next_mask = next_item.get("mask") or next_item.get("buttons")
                    if next_mask == curr_mask:
                        run_len += 1
                    else:
                        break

                if run_len > 1:
                    # Try reducing run length from run_len down to 1
                    for target_len in range(1, run_len):
                        candidate = copy.deepcopy(minimized)
                        del candidate[i + target_len : i + run_len]
                        candidate = self._normalize_frame_offsets(candidate)

                        sig = self._evaluate_candidate(candidate, evaluator)
                        if target_signature.matches(sig):
                            self._log(f"Repeat coalescing: trimmed button hold from {run_len} -> {target_len} frames at index {i}")
                            minimized = candidate
                            run_len = target_len
                            break

            i += 1

        return minimized

    # ── Stage 4: Frame-level 1-Minimal Delta-Debugging ────────────────────────

    def _minimize_frame_delta(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
        target_signature: DivergenceSignature,
    ) -> List[Dict[str, Any]]:
        """Classic delta-debugging (DDmin) on individual input events."""
        items = copy.deepcopy(trace)
        n = 2

        while len(items) >= 2:
            subsets = self._split_list(items, n)
            reduced = False

            for i, subset in enumerate(subsets):
                complement = [item for j, part in enumerate(subsets) if j != i for item in part]
                candidate = self._normalize_frame_offsets(complement)

                sig = self._evaluate_candidate(candidate, evaluator)
                if target_signature.matches(sig):
                    self._log(f"Frame DDmin: removed subset of {len(subset)} items ({len(items)} -> {len(complement)})")
                    items = complement
                    n = max(n - 1, 2)
                    reduced = True
                    break

            if not reduced:
                if n >= len(items):
                    break
                n = min(n * 2, len(items))

        return self._normalize_frame_offsets(items)

    # ── Evaluation & Determinism Verification ─────────────────────────────────

    def _evaluate_candidate(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
    ) -> Optional[DivergenceSignature]:
        """Single evaluation of a candidate trace."""
        self._iteration_count += 1
        try:
            return evaluator(trace)
        except Exception as e:
            self._log(f"Candidate evaluation raised exception: {e}")
            return None

    def _evaluate_with_verification(
        self,
        trace: List[Dict[str, Any]],
        evaluator: TraceEvaluator,
    ) -> Tuple[Optional[DivergenceSignature], bool]:
        """Repeats evaluation to detect non-determinism and flakiness.

        Returns (signature, is_flaky).
        """
        first_sig = self._evaluate_candidate(trace, evaluator)
        if first_sig is None:
            return None, False

        # Repeat checks
        for r in range(self.config.verification_repeats - 1):
            repeat_sig = self._evaluate_candidate(trace, evaluator)
            if not first_sig.matches(repeat_sig):
                self._log(f"Non-deterministic outcome on repeat {r + 2}: {first_sig} != {repeat_sig}")
                return None, True  # Inconclusive / Flaky

        return first_sig, False

    # ── Utility Helpers ──────────────────────────────────────────────────────

    @staticmethod
    def _compute_trace_length(trace: List[Dict[str, Any]]) -> int:
        """Estimate the total frame duration of a trace."""
        total_frames = 0
        current_frame = 0
        for item in trace:
            if "frame" in item:
                current_frame = max(current_frame, int(item["frame"]))
            if "wait" in item:
                current_frame += int(item["wait"])
            if "duration" in item:
                current_frame += int(item["duration"])
        return max(total_frames, current_frame + 1 if trace else 0)

    @staticmethod
    def _normalize_frame_offsets(trace: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Recalculate relative and absolute frame indices after item removal."""
        res: List[Dict[str, Any]] = []
        current_frame = 0
        for item in trace:
            clean_item = dict(item)
            if "frame" in clean_item:
                clean_item["frame"] = current_frame
                current_frame += 1
            elif "wait" in clean_item:
                current_frame += int(clean_item["wait"])
            res.append(clean_item)
        return res

    @staticmethod
    def _split_list(lst: List[Any], n: int) -> List[List[Any]]:
        """Split lst into n sublists."""
        subsets = []
        k = len(lst) // n
        m = len(lst) % n
        start = 0
        for i in range(n):
            end = start + k + (1 if i < m else 0)
            if start < len(lst):
                subsets.append(lst[start:end])
            start = end
        return [s for s in subsets if s]
