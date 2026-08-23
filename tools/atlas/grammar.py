#!/usr/bin/env python3
"""tools/atlas/grammar.py — Action Grammars for Behavior Atlas (BA-04).

Defines action grammars for scene domains (title menu, house dialogue, pause menu,
customer service) and compiles high-level semantic action invocations into
frame-accurate DirectInput button masks and input events.
"""
from __future__ import annotations

import json
from typing import Any, Dict, List, Optional, Tuple

from .model import ActionGrammar, CompletionCondition

# DirectInput button masks matching src/input.h and tools/frida/openrecet-agent.js
BTN_UP = 0x01
BTN_RIGHT = 0x02
BTN_DOWN = 0x04
BTN_LEFT = 0x08
BTN_A = 0x10       # Confirm / Talk / Accept
BTN_B = 0x20       # Cancel / Back
BTN_C = 0x40       # Menu / Special
BTN_D = 0x80       # Dash
BTN_ESC = 0x100    # Pause / Skip


class GrammarRegistry:
    """Registry of standard scene action grammars."""

    @staticmethod
    def get_title_menu_grammar() -> ActionGrammar:
        return ActionGrammar(
            scene="title_menu",
            description="Title screen menu navigation and option selection",
            preconditions=["anchor == 'TITLE_MENU' || anchor == 'TITLE_BOOT'"],
            actions={
                "navigate_down": {
                    "semantic_intent": "Move title menu cursor down by one slot",
                    "inputs": [{"frame": 0, "buttons": ["DOWN"], "mask": BTN_DOWN}],
                    "expected_completion": {"kind": "state_predicate", "predicate": "cursor_pos == old + 1"},
                },
                "navigate_up": {
                    "semantic_intent": "Move title menu cursor up by one slot",
                    "inputs": [{"frame": 0, "buttons": ["UP"], "mask": BTN_UP}],
                    "expected_completion": {"kind": "state_predicate", "predicate": "cursor_pos == old - 1"},
                },
                "select_option": {
                    "semantic_intent": "Confirm selected title menu option",
                    "inputs": [{"frame": 0, "buttons": ["A"], "mask": BTN_A}],
                    "expected_completion": {"kind": "anchor_reached", "anchor": "OPTIONS_MENU_READY"},
                },
                "dismiss_submenu": {
                    "semantic_intent": "Back out of active title submenu",
                    "inputs": [{"frame": 0, "buttons": ["B"], "mask": BTN_B}],
                    "expected_completion": {"kind": "state_predicate", "predicate": "submenu_state == 0"},
                },
            },
        )

    @staticmethod
    def get_house_pause_grammar() -> ActionGrammar:
        return ActionGrammar(
            scene="house_pause",
            description="In-house pause menu and save picker interactions",
            preconditions=["anchor == 'HOUSE_FREEROAM' || anchor == 'PAUSE_OPEN'"],
            actions={
                "open_pause": {
                    "semantic_intent": "Press ESC to open in-game pause menu",
                    "inputs": [{"frame": 0, "buttons": ["ESC"], "mask": BTN_ESC}],
                    "expected_completion": {"kind": "anchor_reached", "anchor": "PAUSE_OPEN"},
                },
                "navigate_to_save": {
                    "semantic_intent": "Navigate down 3 times to Save option",
                    "inputs": [
                        {"frame": 5, "buttons": ["DOWN"], "mask": BTN_DOWN},
                        {"frame": 10, "buttons": ["DOWN"], "mask": BTN_DOWN},
                        {"frame": 15, "buttons": ["DOWN"], "mask": BTN_DOWN},
                    ],
                    "expected_completion": {"kind": "state_predicate", "predicate": "pause_cursor == 3"},
                },
                "open_save_picker": {
                    "semantic_intent": "Open save slot picker",
                    "inputs": [{"frame": 0, "buttons": ["A"], "mask": BTN_A}],
                    "expected_completion": {"kind": "anchor_reached", "anchor": "SAVE_PICKER_READY"},
                },
                "commit_save_slot": {
                    "semantic_intent": "Confirm overwrite and commit save to selected slot",
                    "inputs": [
                        {"frame": 0, "buttons": ["A"], "mask": BTN_A},   # Select slot
                        {"frame": 15, "buttons": ["A"], "mask": BTN_A},  # Confirm 'Yes' overwrite
                    ],
                    "expected_completion": {"kind": "save_committed"},
                },
            },
        )

    @staticmethod
    def get_customer_service_grammar() -> ActionGrammar:
        return ActionGrammar(
            scene="customer_service",
            description="Customer transaction, haggling, and counter service interactions",
            preconditions=["anchor == 'CUSTOMER_SERVICE_ENTER' || cc08 == 4"],
            actions={
                "accept_offer": {
                    "semantic_intent": "Accept customer offer at current proposed price",
                    "inputs": [{"frame": 0, "buttons": ["A"], "mask": BTN_A}],
                    "expected_completion": {"kind": "state_predicate", "predicate": "cc08 == 0 || b534 == 0"},
                },
                "reject_offer": {
                    "semantic_intent": "Reject transaction or refuse to sell item",
                    "inputs": [{"frame": 0, "buttons": ["B"], "mask": BTN_B}],
                    "expected_completion": {"kind": "state_predicate", "predicate": "b534 == 0"},
                },
                "advance_dialogue": {
                    "semantic_intent": "Advance customer speech bubble text",
                    "inputs": [{"frame": 0, "buttons": ["A"], "mask": BTN_A}],
                    "expected_completion": {"kind": "dialogue_advance"},
                },
            },
        )

    @classmethod
    def get_all_default_grammars(cls) -> Dict[str, ActionGrammar]:
        return {
            "title_menu": cls.get_title_menu_grammar(),
            "house_pause": cls.get_house_pause_grammar(),
            "customer_service": cls.get_customer_service_grammar(),
        }


def compile_action_sequence(grammar: ActionGrammar, action_names: List[str]) -> List[Dict[str, Any]]:
    """Compile a list of semantic action names into a concatenated input stream."""
    compiled_inputs: List[Dict[str, Any]] = []
    current_frame_offset = 0

    for act_name in action_names:
        if act_name not in grammar.actions:
            raise ValueError(f"Unknown action '{act_name}' in grammar '{grammar.scene}'")
        act_def = grammar.actions[act_name]
        act_inputs = act_def.get("inputs", [])

        max_rel_frame = 0
        for inp in act_inputs:
            rel_frame = inp.get("frame", 0)
            max_rel_frame = max(max_rel_frame, rel_frame)
            ev = dict(inp)
            ev["frame"] = current_frame_offset + rel_frame
            compiled_inputs.append(ev)

        # Stagger by at least 15 frames between sequential actions
        current_frame_offset += max_rel_frame + 15

    return compiled_inputs
