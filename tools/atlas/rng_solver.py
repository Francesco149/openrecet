#!/usr/bin/env python3
"""tools/atlas/rng_solver.py — RNG Callsite Map and Seed Solver (BA-07).

Implements the LCG mathematics and callsite mapping for Recettear's pseudo-random
number generator (FUN_005041f6 / DAT_006023a0 / src/rng.c):
1. Exact MSVC-compatible 32-bit linear congruential generator arithmetic:
     S_{n+1} = (S_n * 0x343fd + 0x269ec3) mod 2^32
     val15   = (S_{n+1} >> 16) & 0x7fff
2. Backward inversion using modular inverse:
     A_inv = 0xb9b33155  (since 0x343fd * 0xb9b33155 = 1 mod 2^32)
     S_{n-1} = ((S_n - 0x269ec3) * 0xb9b33155) mod 2^32
3. Arbitrary step jumping in O(log k) using matrix/doubling exponentiation.
4. Engine datetime seed derivation (FUN_0050bcff / DAT_006038d0).
5. Callsite registry associating return VAs with semantic consumers.
6. Seed solver finding initial seeds or draw offsets satisfying target downstream predicates.
"""
from __future__ import annotations

import datetime
from dataclasses import asdict, dataclass, field
from typing import Any, Callable, Dict, Iterator, List, Optional, Set, Tuple, Union

# ── LCG Mathematical Constants ───────────────────────────────────────────────

LCG_A: int = 0x343fd          # Multiplier: 214013
LCG_C: int = 0x269ec3         # Increment:  2531011
LCG_M: int = 0x100000000      # Modulus:    2^32 (4294967296)
LCG_MASK: int = 0xffffffff    # 32-bit bitmask

# Modular inverse of LCG_A modulo 2^32: (0x343fd * 0xb9b33155) % 2^32 == 1
LCG_A_INV: int = 0xb9b33155   # 3115495765


# ── Core LCG Step Operations ────────────────────────────────────────────────

def rng_step(seed: int) -> Tuple[int, int]:
    """Advance the RNG state by one step.

    Returns (next_seed, val15) where val15 is in [0, 32767].
    """
    next_seed = ((seed & LCG_MASK) * LCG_A + LCG_C) & LCG_MASK
    val15 = (next_seed >> 16) & 0x7fff
    return next_seed, val15


def rng_step_back(seed: int) -> int:
    """Step the RNG state backward by one step using the modular inverse."""
    diff = ((seed & LCG_MASK) - LCG_C) & LCG_MASK
    prev_seed = (diff * LCG_A_INV) & LCG_MASK
    return prev_seed


def rng_jump(seed: int, k: int) -> int:
    """Jump the RNG state forward (k > 0) or backward (k < 0) by k steps in O(log |k|)."""
    if k == 0:
        return seed & LCG_MASK

    if k < 0:
        # Negative jump: use inverse parameters
        # S_{n-k}: S_prev = a_inv * (S - c) = a_inv * S - a_inv * c
        a = LCG_A_INV
        c = (-LCG_A_INV * LCG_C) & LCG_MASK
        k = -k
    else:
        a = LCG_A
        c = LCG_C

    cur_a = a
    cur_c = c
    res_a = 1
    res_c = 0
    rem = k

    # Binary exponentiation on affine transformations: (A2, C2) o (A1, C1) = (A2*A1, A2*C1 + C2)
    while rem > 0:
        if rem & 1:
            res_a = (res_a * cur_a) & LCG_MASK
            res_c = (res_c * cur_a + cur_c) & LCG_MASK
        cur_c = (cur_c * cur_a + cur_c) & LCG_MASK
        cur_a = (cur_a * cur_a) & LCG_MASK
        rem >>= 1

    return (res_a * (seed & LCG_MASK) + res_c) & LCG_MASK


def rng_sequence(seed: int, count: int) -> List[Tuple[int, int]]:
    """Generate count consecutive (state, val15) pairs starting from seed."""
    seq: List[Tuple[int, int]] = []
    curr = seed & LCG_MASK
    for _ in range(count):
        curr, val = rng_step(curr)
        seq.append((curr, val))
    return seq


def rng_compute_seed(
    year: int,
    month: int,
    day: int,
    hour: int,
    minute: int,
    second: int,
    dst: int = 0,
) -> int:
    """Calculate the engine's time-derived seed matching FUN_0050bcff / src/rng.c."""
    doy = [365, -1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333]
    if month < 1 or month > 12:
        return -1
    yr_1900 = year - 1900
    if yr_1900 < 0x46 or yr_1900 > 0x8a:
        return -1

    adj_day = doy[month] + day
    if (yr_1900 & 3) == 0 and month > 2:
        adj_day += 1

    tz_offset = 0x7080        # 28800 seconds (PST)
    epoch_const = 0x7c558180  # MSVC runtime epoch constant
    dst_bias = -3600

    days = yr_1900 * 365 + adj_day + (year - 1901) // 4
    s = (((hour + days * 24) * 60 + minute) * 60 + tz_offset + epoch_const + second) & LCG_MASK
    if dst == 1:
        s = (s + dst_bias) & LCG_MASK
    return s


# ── Callsite Registry & Semantic Mapping ─────────────────────────────────────

@dataclass
class RNGCallsite:
    """Metadata describing a retail RNG consumption callsite."""
    va: int
    symbol: str
    consumer_type: str  # "dialogue_variant", "haggle_tolerance", "npc_motion", "particle_jitter", "item_drop", "spawn"
    description: str
    draw_count_formula: str
    typical_range: str
    downstream_predicate_hint: str

    def to_dict(self) -> Dict[str, Any]:
        return {
            "va": f"0x{self.va:08x}",
            "symbol": self.symbol,
            "consumer_type": self.consumer_type,
            "description": self.description,
            "draw_count_formula": self.draw_count_formula,
            "typical_range": self.typical_range,
            "downstream_predicate_hint": self.downstream_predicate_hint,
        }


class RNGCallsiteRegistry:
    """Authoritative registry of known Recettear engine RNG callsites."""

    _CALLSITES: Dict[int, RNGCallsite] = {
        0x00470184: RNGCallsite(
            va=0x00470184,
            symbol="FUN_0047be92_probe",
            consumer_type="tick_probe",
            description="Scheduler master tick probe / frame-level consumption mirror",
            draw_count_formula="Variable per frame",
            typical_range="0..32767",
            downstream_predicate_hint="Frame sync",
        ),
        0x00460a1a: RNGCallsite(
            va=0x00460a1a,
            symbol="cs_pick_line",
            consumer_type="dialogue_variant",
            description="Customer service greeting/reaction dialogue variant selection (rng % 2)",
            draw_count_formula="1 per customer greeting/reaction",
            typical_range="0..1 (variant index)",
            downstream_predicate_hint="val % 2 == 0 ('Capitalism, ho!') vs 1 ('How much should I?...')",
        ),
        0x00460672: RNGCallsite(
            va=0x00460672,
            symbol="cs_accept_eval",
            consumer_type="haggle_tolerance",
            description="Haggling price acceptance jitter & budget modifier",
            draw_count_formula="1..2 per proposed price check",
            typical_range="0.85..1.30 (price multiplier)",
            downstream_predicate_hint="price <= base * (min_mult + (val / 32768.0) * span)",
        ),
        0x00460f16: RNGCallsite(
            va=0x00460f16,
            symbol="cs_pushback_line",
            consumer_type="dialogue_variant",
            description="Customer counter-offer rejection and pushback speech bubble line",
            draw_count_formula="1 per pushback",
            typical_range="0..2 (pushback line index)",
            downstream_predicate_hint="val % 3 == line_index",
        ),
        0x0046f621: RNGCallsite(
            va=0x0046f621,
            symbol="scene1_bg_npc_warmup",
            consumer_type="npc_motion",
            description="Outside shop window pedestrian NPC spawn position and velocity warmup",
            draw_count_formula="2 per window NPC spawn",
            typical_range="X in [-400, 400], speed in [1.0, 3.0]",
            downstream_predicate_hint="spawn_x = (val1 / 32768.0) * 800 - 400",
        ),
        0x0047019f: RNGCallsite(
            va=0x0047019f,
            symbol="scene1_customer_npc_pump",
            consumer_type="npc_motion",
            description="In-shop browsing chibi NPC movement step and shelf retargeting",
            draw_count_formula="1 per 30 frames or retarget event",
            typical_range="target_shelf in [0, shelf_count - 1]",
            downstream_predicate_hint="shelf_idx = val % num_shelves",
        ),
        0x0048a833: RNGCallsite(
            va=0x0048a833,
            symbol="companion_sparkle",
            consumer_type="particle_jitter",
            description="Tear wing-glow sparkle animation jitter (db054 % 4 == 0)",
            draw_count_formula="1 per 4 simulation ticks",
            typical_range="angle in [0, 2*pi]",
            downstream_predicate_hint="sparkle_angle = (val / 32768.0) * 6.28318",
        ),
        0x00451790: RNGCallsite(
            va=0x00451790,
            symbol="particle_randomization",
            consumer_type="particle_jitter",
            description="Pre-reseed cold boot particle table initialization",
            draw_count_formula="128 consecutive draws at boot step 2",
            typical_range="initial coordinate offsets",
            downstream_predicate_hint="Deterministic under seed=1",
        ),
        0x0048960d: RNGCallsite(
            va=0x0048960d,
            symbol="shop_display_grid_rebuild",
            consumer_type="layout",
            description="Display shelf layout item placement orientation",
            draw_count_formula="1 per placed display item",
            typical_range="rotation index [0, 3]",
            downstream_predicate_hint="rotation = val % 4",
        ),
        0x00476320: RNGCallsite(
            va=0x00476320,
            symbol="dungeon_spawn_enemy",
            consumer_type="spawn",
            description="Dungeon floor enemy mob group selection and positioning",
            draw_count_formula="3 per spawned monster",
            typical_range="enemy_id in table, pos (x, z)",
            downstream_predicate_hint="mob_id = mob_table[val % len(mob_table)]",
        ),
        0x00477810: RNGCallsite(
            va=0x00477810,
            symbol="dungeon_drop_calc",
            consumer_type="item_drop",
            description="Defeated monster loot drop roll",
            draw_count_formula="1 per kill",
            typical_range="roll in [0, 99]",
            downstream_predicate_hint="is_rare_drop = (val % 100) < drop_rate",
        ),
    }

    @classmethod
    def get_by_va(cls, va: int) -> Optional[RNGCallsite]:
        return cls._CALLSITES.get(va)

    @classmethod
    def list_all(cls) -> List[RNGCallsite]:
        return list(cls._CALLSITES.values())

    @classmethod
    def find_by_consumer_type(cls, ctype: str) -> List[RNGCallsite]:
        return [cs for cs in cls._CALLSITES.values() if cs.consumer_type == ctype]


# ── Seed Solver & Goal Predicate Solver ──────────────────────────────────────

@dataclass
class SeedSolution:
    """Result of an RNG seed solution query."""
    initial_seed: int
    steps_advanced: int
    resulting_seed: int
    matching_values: List[int]
    provenance: Optional[Dict[str, Any]] = None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "initial_seed": self.initial_seed,
            "steps_advanced": self.steps_advanced,
            "resulting_seed": self.resulting_seed,
            "matching_values": self.matching_values,
            "provenance": self.provenance,
        }


class RNGSeedSolver:
    """Solves initial seeds or step offsets satisfying downstream gameplay predicates."""

    @staticmethod
    def solve_for_sequence_predicate(
        predicate_fn: Callable[[List[int]], bool],
        draw_count: int,
        start_seed: int = 1,
        max_search_steps: int = 50000,
    ) -> Optional[SeedSolution]:
        """Finds the first advance offset from start_seed where next draw_count values satisfy predicate."""
        curr_seed = start_seed & LCG_MASK

        for step in range(max_search_steps):
            # Probe forward draw_count values
            probe_seed = curr_seed
            values = []
            for _ in range(draw_count):
                probe_seed, val = rng_step(probe_seed)
                values.append(val)

            if predicate_fn(values):
                return SeedSolution(
                    initial_seed=start_seed,
                    steps_advanced=step,
                    resulting_seed=curr_seed,
                    matching_values=values,
                    provenance={"search_steps": step},
                )

            curr_seed, _ = rng_step(curr_seed)

        return None

    @staticmethod
    def solve_for_seed_candidates(
        predicate_fn: Callable[[List[int]], bool],
        draw_count: int,
        seed_candidates: List[int],
    ) -> List[SeedSolution]:
        """Filters a list of candidate seeds (e.g. from timestamp sweeps) matching predicate."""
        solutions = []
        for seed in seed_candidates:
            probe_seed = seed
            values = []
            for _ in range(draw_count):
                probe_seed, val = rng_step(probe_seed)
                values.append(val)

            if predicate_fn(values):
                solutions.append(SeedSolution(
                    initial_seed=seed,
                    steps_advanced=0,
                    resulting_seed=seed,
                    matching_values=values,
                ))
        return solutions

    @staticmethod
    def generate_datetime_seed_space(
        year: int = 2026,
        month: int = 8,
        days: Optional[List[int]] = None,
        hours: Optional[List[int]] = None,
    ) -> List[Tuple[int, datetime.datetime]]:
        """Generates realistic engine datetime seed space for the given date constraints."""
        target_days = days or list(range(1, 32))
        target_hours = hours or list(range(0, 24))
        space: List[Tuple[int, datetime.datetime]] = []

        for d in target_days:
            for h in target_hours:
                for m in range(0, 60, 5):  # Sample every 5 minutes
                    try:
                        dt = datetime.datetime(year, month, d, h, m, 0)
                        seed = rng_compute_seed(year, month, d, h, m, 0, dst=0)
                        if seed != -1:
                            space.append((seed, dt))
                    except ValueError:
                        continue

        return space
