/* watch_write.c — see watch_write.h */
#include "watch_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "state_va.h"

static uint32_t s_va;
static uint32_t s_from, s_to = 0xffffffffu;
static volatile int32_t *s_wp;
static int32_t s_last;
static uint32_t s_label;
static int s_armed;

int watch_write_parse(const char *arg)
{
    char *end;
    s_va = (uint32_t)strtoul(arg, &end, 16);
    if (end == arg || s_va % 4) return -1;
    if (*end == ':') {
        s_from = (uint32_t)strtoul(end + 1, &end, 10);
        if (*end == ':') s_to = (uint32_t)strtoul(end + 1, &end, 10);
    }
    return 0;
}

#if defined(_WIN32) && defined(__i386__)
#include <windows.h>

#define WW_ARM_CODE 0xE0577701u   /* our own exception: the handler loads DR0 / DR7 */

static LONG CALLBACK ww_handler(PEXCEPTION_POINTERS ep)
{
    PEXCEPTION_RECORD er = ep->ExceptionRecord;
    PCONTEXT c = ep->ContextRecord;
    if (er->ExceptionCode == WW_ARM_CODE) {
        c->Dr0 = (DWORD)(uintptr_t)s_wp;
        c->Dr7 = (c->Dr7 & ~0x000f0003u) | 0x1u | (0x1u << 16) | (0x3u << 18);  /* L0, write, 4 bytes */
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && (c->Dr6 & 1u)) {
        const int32_t v = *s_wp;
        if (s_label >= s_from && s_label <= s_to && v != s_last)
            fprintf(stderr, "ww f%u eip=%08lx %08x->%08x\n", (unsigned)s_label,
                    (unsigned long)c->Eip, (unsigned)s_last, (unsigned)v);
        s_last = v;
        c->Dr6 &= ~0xfu;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void watch_write_tick(uint32_t label)
{
    s_label = label;
    if (s_armed || s_va == 0) return;
    s_wp = state_va_mirror_addr(s_va);
    if (!s_wp) return;                      /* not mapped yet (a work bank before load) */
    s_last = *s_wp;
    AddVectoredExceptionHandler(1, ww_handler);
    RaiseException(WW_ARM_CODE, 0, 0, NULL);
    s_armed = 1;
    fprintf(stderr, "ww armed va=%x port=%p from=%u to=%u\n", (unsigned)s_va, (void *)s_wp,
            (unsigned)s_from, (unsigned)s_to);
}
#else
void watch_write_tick(uint32_t label) { (void)label; }
#endif
