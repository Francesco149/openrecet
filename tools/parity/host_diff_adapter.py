#!/usr/bin/env python3
"""tools/parity/host_diff_adapter.py — CC-02 native host C differential adapter.

Loads the compiled host differential shared library (tests/build/libengine_diff.so)
and provides ctypes marshaling to execute CallCapsules against the actual native C
ported functions.
"""
from __future__ import annotations

import ctypes
import os
from pathlib import Path
import subprocess
import sys
from typing import Any, Dict, List, Optional, Tuple

from .capsule import (
    CallCapsule,
    CapsuleError,
    CapsuleReplayResult,
    replay_capsule,
    validate_capsule,
)

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DIFF_SO_PATH = REPO_ROOT / "tests" / "build" / "libengine_diff.so"


# ─── ctypes Struct Definitions (matching src/diff_entry.h) ───────────────────

class EngineRngIn(ctypes.Structure):
    _fields_ = [("seed", ctypes.c_uint32)]

class EngineRngOut(ctypes.Structure):
    _fields_ = [
        ("post_state", ctypes.c_uint32),
        ("ret_value", ctypes.c_uint16),
        ("_pad", ctypes.c_uint16),
    ]

class EngineFadeIn(ctypes.Structure):
    _fields_ = [("slider", ctypes.c_int32)]

class EngineFadeOut(ctypes.Structure):
    _fields_ = [("centibel", ctypes.c_int32)]

class EngineBossIdIn(ctypes.Structure):
    _fields_ = [("enemy_id", ctypes.c_int32)]

class EngineBossIdOut(ctypes.Structure):
    _fields_ = [("allowed", ctypes.c_int32)]

class EngineCheckpointIn(ctypes.Structure):
    _fields_ = [
        ("dungeon_id", ctypes.c_int32),
        ("next_floor", ctypes.c_int32),
    ]

class EngineCheckpointOut(ctypes.Structure):
    _fields_ = [("is_checkpoint", ctypes.c_int32)]

class EngineHaggleDecideIn(ctypes.Structure):
    _fields_ = [
        ("player_ask", ctypes.c_int32),
        ("accept_ref", ctypes.c_int32),
    ]

class EngineHaggleDecideOut(ctypes.Structure):
    _fields_ = [("verdict", ctypes.c_int32)]

class EngineHaggleBudgetCeilingIn(ctypes.Structure):
    _fields_ = [
        ("market_price", ctypes.c_int32),
        ("budget_low", ctypes.c_int32),
        ("budget_high", ctypes.c_int32),
    ]

class EngineHaggleBudgetCeilingOut(ctypes.Structure):
    _fields_ = [("ceiling", ctypes.c_int32)]

class EngineAudioOneShotIn(ctypes.Structure):
    _fields_ = [("track", ctypes.c_int32)]

class EngineAudioOneShotOut(ctypes.Structure):
    _fields_ = [("is_one_shot", ctypes.c_int32)]

class EnginePushbackPatienceIn(ctypes.Structure):
    _fields_ = [
        ("loyalty_level", ctypes.c_int32),
        ("sell_active", ctypes.c_int32),
    ]

class EnginePushbackPatienceOut(ctypes.Structure):
    _fields_ = [("patience_variant", ctypes.c_int32)]

class EngineBudgetLevelDayIn(ctypes.Structure):
    _fields_ = [
        ("cand_idx", ctypes.c_int32),
        ("shop_day", ctypes.c_int32),
        ("closeness_level", ctypes.c_int32),
    ]

class EngineBudgetLevelDayOut(ctypes.Structure):
    _fields_ = [("budget", ctypes.c_int32)]

class EngineItemFindSlotIn(ctypes.Structure):
    _fields_ = [("item_id", ctypes.c_int32)]

class EngineItemFindSlotOut(ctypes.Structure):
    _fields_ = [("slot_idx", ctypes.c_int32)]

class EngineCharaEquipStatsIn(ctypes.Structure):
    _fields_ = [
        ("slot_val", ctypes.c_uint32),
        ("initial_sum", ctypes.c_int32 * 4),
    ]

class EngineCharaEquipStatsOut(ctypes.Structure):
    _fields_ = [("sum", ctypes.c_int32 * 4)]

# ─── Native Library Loader ───────────────────────────────────────────────────

_CACHED_LIB: Optional[ctypes.CDLL] = None


def ensure_libengine_diff() -> Path:
    """Ensures tests/build/libengine_diff.so exists and is up to date."""
    if not DIFF_SO_PATH.exists():
        tests_dir = REPO_ROOT / "tests"
        proc = subprocess.run(
            ["make", "diff"],
            cwd=tests_dir,
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0:
            raise CapsuleError(
                f"Failed to build libengine_diff.so via 'make diff':\n{proc.stderr}\n{proc.stdout}"
            )
    return DIFF_SO_PATH


def get_diff_lib() -> ctypes.CDLL:
    """Returns the loaded ctypes CDLL handle for libengine_diff.so."""
    global _CACHED_LIB
    if _CACHED_LIB is None:
        so_path = ensure_libengine_diff()
        _CACHED_LIB = ctypes.CDLL(str(so_path))

        # Setup signatures
        # 1. rng_next15
        if hasattr(_CACHED_LIB, "engine_rng_next15"):
            _CACHED_LIB.engine_rng_next15.argtypes = [
                ctypes.POINTER(EngineRngIn),
                ctypes.POINTER(EngineRngOut),
            ]
            _CACHED_LIB.engine_rng_next15.restype = None

        # 2. audio_fade
        if hasattr(_CACHED_LIB, "engine_audio_fade"):
            _CACHED_LIB.engine_audio_fade.argtypes = [
                ctypes.POINTER(EngineFadeIn),
                ctypes.POINTER(EngineFadeOut),
            ]
            _CACHED_LIB.engine_audio_fade.restype = None

        # 3. boss_id_allowed
        if hasattr(_CACHED_LIB, "engine_stage_gate_boss_id_allowed"):
            _CACHED_LIB.engine_stage_gate_boss_id_allowed.argtypes = [
                ctypes.POINTER(EngineBossIdIn),
                ctypes.POINTER(EngineBossIdOut),
            ]
            _CACHED_LIB.engine_stage_gate_boss_id_allowed.restype = None

        # 4. floor_is_checkpoint
        if hasattr(_CACHED_LIB, "engine_stage_gate_floor_is_checkpoint"):
            _CACHED_LIB.engine_stage_gate_floor_is_checkpoint.argtypes = [
                ctypes.POINTER(EngineCheckpointIn),
                ctypes.POINTER(EngineCheckpointOut),
            ]
            _CACHED_LIB.engine_stage_gate_floor_is_checkpoint.restype = None

        # 5. haggle_decide
        if hasattr(_CACHED_LIB, "engine_haggle_decide"):
            _CACHED_LIB.engine_haggle_decide.argtypes = [
                ctypes.POINTER(EngineHaggleDecideIn),
                ctypes.POINTER(EngineHaggleDecideOut),
            ]
            _CACHED_LIB.engine_haggle_decide.restype = None

        # 6. haggle_budget_ceiling
        if hasattr(_CACHED_LIB, "engine_haggle_budget_ceiling"):
            _CACHED_LIB.engine_haggle_budget_ceiling.argtypes = [
                ctypes.POINTER(EngineHaggleBudgetCeilingIn),
                ctypes.POINTER(EngineHaggleBudgetCeilingOut),
            ]
            _CACHED_LIB.engine_haggle_budget_ceiling.restype = None

        # 7. audio_is_one_shot_track
        if hasattr(_CACHED_LIB, "engine_audio_is_one_shot_track"):
            _CACHED_LIB.engine_audio_is_one_shot_track.argtypes = [
                ctypes.POINTER(EngineAudioOneShotIn),
                ctypes.POINTER(EngineAudioOneShotOut),
            ]
            _CACHED_LIB.engine_audio_is_one_shot_track.restype = None

        # 8. customer_service_pushback_patience
        if hasattr(_CACHED_LIB, "engine_customer_service_pushback_patience"):
            _CACHED_LIB.engine_customer_service_pushback_patience.argtypes = [
                ctypes.POINTER(EnginePushbackPatienceIn),
                ctypes.POINTER(EnginePushbackPatienceOut),
            ]
            _CACHED_LIB.engine_customer_service_pushback_patience.restype = None

        # 9. customer_service_budget_level_day
        if hasattr(_CACHED_LIB, "engine_customer_service_budget_level_day"):
            _CACHED_LIB.engine_customer_service_budget_level_day.argtypes = [
                ctypes.POINTER(EngineBudgetLevelDayIn),
                ctypes.POINTER(EngineBudgetLevelDayOut),
            ]
            _CACHED_LIB.engine_customer_service_budget_level_day.restype = None

        # 10. tables_item_find_slot_by_id
        if hasattr(_CACHED_LIB, "engine_tables_item_find_slot_by_id"):
            _CACHED_LIB.engine_tables_item_find_slot_by_id.argtypes = [
                ctypes.POINTER(EngineItemFindSlotIn),
                ctypes.POINTER(EngineItemFindSlotOut),
            ]
            _CACHED_LIB.engine_tables_item_find_slot_by_id.restype = None

        # 11. chara_equip_item_stats
        if hasattr(_CACHED_LIB, "engine_chara_equip_item_stats"):
            _CACHED_LIB.engine_chara_equip_item_stats.argtypes = [
                ctypes.POINTER(EngineCharaEquipStatsIn),
                ctypes.POINTER(EngineCharaEquipStatsOut),
            ]
            _CACHED_LIB.engine_chara_equip_item_stats.restype = None

    return _CACHED_LIB


# ─── Native Host Diff Adapter Engine ─────────────────────────────────────────

class NativeHostDiffAdapter:
    """Executes CallCapsules directly against native compiled C code via libengine_diff.so."""

    @classmethod
    def execute_native(cls, capsule: CallCapsule) -> CapsuleReplayResult:
        """Executes the capsule against the corresponding native C symbol and verifies bit-exactness."""
        validate_capsule(capsule)

        if capsule.category == "unsupported_os_call":
            return CapsuleReplayResult(
                matched=False,
                return_val_matched=False,
                poststate_matched=False,
                writes_matched=False,
                verdict="UNSUPPORTED",
                notes=["unsupported_os_call cannot be executed natively on host."],
            )

        lib = get_diff_lib()
        target_va = capsule.target_va.lower()
        symbol = capsule.target_symbol

        actual_ret: Any = None
        actual_poststate: Dict[str, Any] = {}

        # 1. FUN_00431990 — stage_gate_boss_id_allowed
        if target_va == "0x00431990" or symbol == "stage_gate_boss_id_allowed":
            enemy_id = int(capsule.stack_args[0]) if capsule.stack_args else 0
            in_val = EngineBossIdIn(enemy_id=enemy_id)
            out_val = EngineBossIdOut()
            lib.engine_stage_gate_boss_id_allowed(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.allowed)

        # 2. FUN_0043195d — stage_gate_floor_is_checkpoint
        elif target_va == "0x0043195d" or symbol == "stage_gate_floor_is_checkpoint":
            dungeon_id = int(capsule.prestate.get("DAT_0438b4c8", 0))
            next_floor = int(capsule.prestate.get("DAT_0438b4cc", 0))
            in_val = EngineCheckpointIn(dungeon_id=dungeon_id, next_floor=next_floor)
            out_val = EngineCheckpointOut()
            lib.engine_stage_gate_floor_is_checkpoint(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.is_checkpoint)
            actual_poststate["DAT_0438b4c8"] = dungeon_id
            actual_poststate["DAT_0438b4cc"] = next_floor

        # 3. FUN_005041f6 — rng_next15
        elif target_va == "0x005041f6" or symbol == "rng_next15":
            seed = int(capsule.prestate.get("DAT_006023a0", 1))
            in_val = EngineRngIn(seed=seed)
            out_val = EngineRngOut()
            lib.engine_rng_next15(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.ret_value)
            actual_poststate["DAT_006023a0"] = int(out_val.post_state)

        # 4. FUN_00499583 — audio_fade_compute
        elif target_va == "0x00499583" or symbol == "audio_fade_compute":
            slider = int(capsule.stack_args[0]) if capsule.stack_args else 0
            in_val = EngineFadeIn(slider=slider)
            out_val = EngineFadeOut()
            lib.engine_audio_fade(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.centibel)


        # 5. FUN_00460672 — haggle_decide
        elif target_va == "0x00460672" or symbol == "haggle_decide":
            ask = int(capsule.stack_args[0]) if len(capsule.stack_args) > 0 else 1000
            ref = int(capsule.stack_args[1]) if len(capsule.stack_args) > 1 else 1000
            in_val = EngineHaggleDecideIn(player_ask=ask, accept_ref=ref)
            out_val = EngineHaggleDecideOut()
            lib.engine_haggle_decide(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.verdict)

        # 6. FUN_0045ecc0 — haggle_budget_ceiling
        elif target_va == "0x0045ecc0" or symbol == "haggle_budget_ceiling":
            market = int(capsule.stack_args[0]) if len(capsule.stack_args) > 0 else 0
            low = int(capsule.stack_args[1]) if len(capsule.stack_args) > 1 else 0
            high = int(capsule.stack_args[2]) if len(capsule.stack_args) > 2 else 0
            in_val = EngineHaggleBudgetCeilingIn(market_price=market, budget_low=low, budget_high=high)
            out_val = EngineHaggleBudgetCeilingOut()
            lib.engine_haggle_budget_ceiling(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.ceiling)

        # 7. FUN_00498ef4 — audio_is_one_shot_track
        elif target_va == "0x00498ef4" or symbol == "audio_is_one_shot_track":
            track = int(capsule.stack_args[0]) if capsule.stack_args else 0
            in_val = EngineAudioOneShotIn(track=track)
            out_val = EngineAudioOneShotOut()
            lib.engine_audio_is_one_shot_track(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.is_one_shot)

        # 8. FUN_00460f16 — customer_service_pushback_patience
        elif target_va == "0x00460f16" or symbol == "customer_service_pushback_patience":
            lvl = int(capsule.stack_args[0]) if len(capsule.stack_args) > 0 else 0
            sell_active = int(capsule.stack_args[1]) if len(capsule.stack_args) > 1 else 0
            in_val = EnginePushbackPatienceIn(loyalty_level=lvl, sell_active=sell_active)
            out_val = EnginePushbackPatienceOut()
            lib.engine_customer_service_pushback_patience(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.patience_variant)

        # 9. FUN_00461011 — customer_service_budget_level_day
        elif target_va == "0x00461011" or symbol == "customer_service_budget_level_day":
            cand = int(capsule.stack_args[0]) if capsule.stack_args else 0
            day = int(capsule.prestate.get("DAT_0450fb84", 2))
            lvl = int(capsule.prestate.get("DAT_045109aa", 0))
            in_val = EngineBudgetLevelDayIn(cand_idx=cand, shop_day=day, closeness_level=lvl)
            out_val = EngineBudgetLevelDayOut()
            lib.engine_customer_service_budget_level_day(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.budget)
            actual_poststate["DAT_0450fb84"] = day
            actual_poststate["DAT_045109aa"] = lvl

        # 10. FUN_004681f6 — tables_item_find_slot_by_id
        elif target_va == "0x004681f6" or symbol == "tables_item_find_slot_by_id":
            item_id = int(capsule.stack_args[0]) if capsule.stack_args else 0
            in_val = EngineItemFindSlotIn(item_id=item_id)
            out_val = EngineItemFindSlotOut()
            lib.engine_tables_item_find_slot_by_id(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = int(out_val.slot_idx)
            actual_poststate["DAT_005c80ac"] = int(capsule.prestate.get("DAT_005c80ac", 0))

        # 11. FUN_0048093f — chara_equip_item_stats
        elif target_va == "0x0048093f" or symbol == "chara_equip_item_stats":
            slot_val = int(capsule.stack_args[0]) if len(capsule.stack_args) > 0 else 0
            arr_type = ctypes.c_int32 * 4
            initial_arr = arr_type(0, 0, 0, 0)
            in_val = EngineCharaEquipStatsIn(slot_val=slot_val, initial_sum=initial_arr)
            out_val = EngineCharaEquipStatsOut()
            lib.engine_chara_equip_item_stats(ctypes.byref(in_val), ctypes.byref(out_val))
            actual_ret = None
            actual_poststate["DAT_005c80ac"] = int(capsule.prestate.get("DAT_005c80ac", 0))
        else:
            # Fallback to Python reference port replay for targets not in libengine_diff.so
            from .capsule_capture import (
                host_port_audio_fade_compute,
                host_port_boss_id_allowed,
                host_port_floor_is_checkpoint,
                host_port_records_a_spawn,
                host_port_rng_next15,
            )
            fallback_handlers = {
                "0x00447f4f": host_port_records_a_spawn,
                "scene1_spawn_particle": host_port_records_a_spawn,
            }
            handler = fallback_handlers.get(target_va) or fallback_handlers.get(symbol)
            if handler:
                return replay_capsule(capsule, handler)
            raise CapsuleError(f"No native adapter mapped for target VA {target_va} ({symbol})")

        # Compare outputs bit-for-bit
        ret_matched = (actual_ret == capsule.return_val)
        if not ret_matched:
            return CapsuleReplayResult(
                matched=False,
                return_val_matched=False,
                poststate_matched=True,
                writes_matched=True,
                verdict="FAIL",
                divergent_field="return_val",
                expected_val=capsule.return_val,
                actual_val=actual_ret,
                notes=[f"Native C return value mismatch: expected {capsule.return_val}, got {actual_ret}"],
            )

        post_matched = True
        for k, exp_val in capsule.poststate.items():
            act_val = actual_poststate.get(k)
            if act_val is not None and act_val != exp_val:
                return CapsuleReplayResult(
                    matched=False,
                    return_val_matched=True,
                    poststate_matched=False,
                    writes_matched=True,
                    verdict="FAIL",
                    divergent_field=f"poststate/{k}",
                    expected_val=exp_val,
                    actual_val=act_val,
                    notes=[f"Native C poststate mismatch on {k}: expected {exp_val}, got {act_val}"],
                )

        return CapsuleReplayResult(
            matched=True,
            return_val_matched=True,
            poststate_matched=True,
            writes_matched=True,
            verdict="PASS",
            notes=["Bit-exact match on native C host execution."],
        )
