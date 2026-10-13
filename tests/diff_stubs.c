/*
 * diff_stubs.c — no-op stubs for the host differential-test shared library
 * (tests/build/libengine_diff.so), NOT part of the exe build.
 *
 * audio_fade.c references audio_trace_emit_fade_start (defined in audio.c)
 * and call_trace_enter (defined in call_trace.c) from code paths the diff
 * harness never invokes (audio_fade_apply / the BGM tick). Both real TUs
 * pull in <windows.h> and can't join the host build, so the symbols are
 * stubbed here. This file lives under tests/ on purpose: src/Makefile globs
 * every src/*.c into openrecet.exe, so a stub placed in src/ would
 * duplicate-define the real symbols and break the exe link. Mirrors the same
 * trick in tools/state_diff/oracle.c.
 *
 * Linked only via tests/Makefile DIFF_SRCS.
 */

#include <stdint.h>

#include "tables_enemylist.h"   /* enemylist_state_t for the g_enemylist def */

void audio_trace_emit_fade_start(int channel, int slider, int32_t centibel)
{
    (void)channel; (void)slider; (void)centibel;
}

void call_trace_enter(uint32_t ghidra_va, const void *ret_addr, int stub)
{
    (void)ghidra_va; (void)ret_addr; (void)stub;
}

/* stage_gate.c (the E.4 Tier 1 target TU) references two engine globals
 * whose real definitions live in TUs we don't link into the host .so:
 *
 *   g_scene1_combat_stage_id  — scene1_combat_sm.c (heavy include web)
 *   g_enemylist               — tables_enemylist.c (parser + stdio)
 *
 * Only stage_gate_floor_is_checkpoint + stage_gate_boss_id_allowed are
 * exercised as diff targets; neither touches g_enemylist (only the
 * unported-as-a-target stage_gate_query walks it).  So a BSS-zero
 * standalone definition here satisfies the linker without dragging the
 * real TUs in.  g_scene1_combat_stage_id IS injected by the checkpoint
 * target — diff_entry.c writes it directly. */
int32_t g_scene1_combat_stage_id;
enemylist_state_t g_enemylist;
