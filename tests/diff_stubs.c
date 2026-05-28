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

void audio_trace_emit_fade_start(int channel, int slider, int32_t centibel)
{
    (void)channel; (void)slider; (void)centibel;
}

void call_trace_enter(uint32_t ghidra_va, const void *ret_addr, int stub)
{
    (void)ghidra_va; (void)ret_addr; (void)stub;
}
