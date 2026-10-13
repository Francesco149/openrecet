/* rh_hooks.c — hook table: retail function VA → OFF | SHADOW | REPLACE (port code).
 *
 *   --hook shadow:haggle_decide,replace:rand,shadow:sim_a
 *
 * Kinds of registry entry:
 *   RET   pure-ish function: returns a value, reads globals, may write a few listed
 *         state words. SHADOW = run retail, roll back the words, run the port,
 *         compare return + words, keep retail's. REPLACE = run only the port.
 *   STATE void function with arbitrary side effects: SHADOW = rh_shadow() full-state
 *         compare (snapshot .data + heap arena); port NULL ⇒ retail vs retail
 *         (determinism self-check). Skipped while a retail worker thread is live.
 * Every hook is a direct-call-site rewrite (redirect_calls); retail stays callable
 * at its VA. A port function must never call its own retail VA (lint TODO). */
#include "rhost.h"
#include "customer_haggle.h"

extern volatile LONG g_live_threads;
typedef enum { OFF, SHADOW, REPLACE } mode_t_;
typedef struct {
    const char *name; uint32_t va; int is_state;
    void *hook;                    /* call-site target */
    mode_t_ mode; int sites;
    long calls, mism, skipped;
    char first[200];
} hook_t;

#define NH 4
static hook_t H[NH];
static void note(hook_t *h, const char *fmt, ...)
{
    h->mism++;
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (!h->first[0]) snprintf(h->first, sizeof h->first, "f%u %s", g_frame, b);
    if (h->mism <= 20) rh_log("  DIVERGE %s f%u: %s\n", h->name, g_frame, b);
}

/* ---- port wrappers in retail calling convention ---- */
static int32_t port_haggle_decide(void) { return haggle_decide((int32_t)G32(0x5c6bb8), (int32_t)G32(0x730b588)); }
static uint32_t port_rand(void)
{
    uint32_t s = G32(DAT_RNG) * 0x343fdu + 0x269ec3u;
    G32(DAT_RNG) = s;
    return (s >> 16) & 0x7fffu;
}

#define H_HAGGLE 0
#define H_RAND   1
#define H_SIM_A  2
#define H_SIM_B  3

static int32_t __cdecl hook_haggle(void)
{
    hook_t *h = &H[H_HAGGLE]; h->calls++;
    if (h->mode == REPLACE) return port_haggle_decide();
    int32_t r = ((int32_t (__cdecl *)(void))0x460672)();
    int32_t p = port_haggle_decide();
    if (p != r) note(h, "ref=%d ask=%d retail=%d port=%d", (int)G32(0x730b588), (int)G32(0x5c6bb8), r, p);
    return r;
}
static uint32_t __cdecl hook_rand(void)
{
    hook_t *h = &H[H_RAND]; h->calls++;
    if (h->mode == REPLACE) return port_rand();
    uint32_t s0 = G32(DAT_RNG);
    uint32_t r = ((uint32_t (__cdecl *)(void))VA_RAND)(), s1 = G32(DAT_RNG);
    G32(DAT_RNG) = s0;
    uint32_t p = port_rand(), s2 = G32(DAT_RNG);
    if (p != r || s1 != s2) note(h, "seed=%08x retail=%u/%08x port=%u/%08x", s0, r, s1, p, s2);
    G32(DAT_RNG) = s1;
    return r;
}
static void state_hook(hook_t *h)
{
    h->calls++;
    if (g_live_threads) { h->skipped++; ((void (__cdecl *)(void))(uintptr_t)h->va)(); return; }
    char why[256];
    int r = rh_shadow(h->va, h->name, NULL, why, sizeof why);
    if (r > 0) note(h, "%s", why); else if (r < 0) h->skipped++;
}
static void __cdecl hook_sim_a(void) { state_hook(&H[H_SIM_A]); }
static void __cdecl hook_sim_b(void) { state_hook(&H[H_SIM_B]); }

static hook_t H[NH] = {
    [H_HAGGLE] = { "haggle_decide", 0x460672u, 0, (void *)hook_haggle },
    [H_RAND]   = { "rand",          VA_RAND,   0, (void *)hook_rand },
    [H_SIM_A]  = { "sim_a",         0x4536cbu, 1, (void *)hook_sim_a },   /* retail-vs-retail self-check */
    [H_SIM_B]  = { "sim_b",         0x49966au, 1, (void *)hook_sim_b },
};

void rh_hooks_install(void)
{
    if (!O.hooks) return;
    char spec[1024]; snprintf(spec, sizeof spec, "%s", O.hooks);
    for (char *tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
        char *c = strchr(tok, ':');
        if (!c) rh_fatal("bad --hook item '%s' (want mode:name)", tok);
        *c = 0;
        mode_t_ m = !strcmp(tok, "shadow") ? SHADOW : !strcmp(tok, "replace") ? REPLACE : !strcmp(tok, "off") ? OFF : (mode_t_)-1;
        if ((int)m < 0) rh_fatal("bad hook mode '%s'", tok);
        hook_t *h = NULL;
        for (int i = 0; i < NH; i++) if (!strcmp(H[i].name, c + 1)) h = &H[i];
        if (!h) rh_fatal("unknown hook '%s'", c + 1);
        if (h->is_state && m == REPLACE) rh_fatal("%s: STATE hooks have no port body yet (shadow only)", h->name);
        h->mode = m;
        if (m != OFF) h->sites = redirect_calls(h->va, h->hook);
        rh_log("hook %s %s @%08x: %d call sites\n", tok, h->name, h->va, h->sites);
    }
}

void rh_hooks_report(void)
{
    for (int i = 0; i < NH; i++) {
        hook_t *h = &H[i];
        if (h->mode == OFF) continue;
        rh_log("hook-result %s %s calls=%ld diverge=%ld skipped=%ld%s%s\n", h->mode == SHADOW ? "shadow" : "replace",
               h->name, h->calls, h->mism, h->skipped, h->first[0] ? " first: " : "", h->first);
    }
}
