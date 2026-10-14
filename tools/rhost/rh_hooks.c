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
#include "actor_spawn.h"
#include "audio_mci.h"
#include "customer_haggle.h"
#include "customer_seller.h"
#include "floor_layout.h"
#include "rng.h"
#include "scene1_particles_tick.h"
#include "tables_item.h"

/* actor_spawn.c clears fields of the port's people table; rhost links no
 * scene1_particles_tick.c, so it owns the storage here. */
scene1_people_entry_t g_scene1_people[SCENE1_PEOPLE_COUNT];
/* customer_seller.c reads g_item; synced from retail's table (0x95d37d0) per call. */
item_state_t g_item;

extern volatile LONG g_live_threads;
typedef enum { OFF, SHADOW, REPLACE } mode_t_;
typedef struct {
    const char *name; uint32_t va; int is_state;
    void *hook;                    /* call-site target */
    mode_t_ mode; int sites;
    long calls, mism, skipped;
    char first[200];
} hook_t;

#define NH 9
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
#define H_OFFER_UP   4
#define H_OFFER_DOWN 5
#define H_FLOOR      6
#define H_ACTOR_RST  7
#define H_SELLER     8

static int32_t __cdecl hook_haggle(void)
{
    hook_t *h = &H[H_HAGGLE]; h->calls++;
    if (h->mode == REPLACE) return port_haggle_decide();
    int32_t r = ((int32_t (__cdecl *)(void))0x460672)();
    int32_t p = port_haggle_decide();
    rh_log("haggle decide f%u base=%d ask=%d ref=%d -> %d\n", g_frame, (int)G32(0x5c6bc0), (int)G32(0x5c6bb8), (int)G32(0x730b588), r);
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
/* ---- haggle offers (FUN_00460161 / FUN_004603cf): retail globals → port structs ---- */
#define W_ROUND 0x730b584u
#define W_OFFER 0x730b574u
#define W_WORK  0x730b57cu
#define W_FLOOR 0x730b580u
#define W_ACC   0x730b588u
#define W_BASE  0x5c6bc0u
#define W_ASK   0x5c6bb8u
#define W_IDX   0x730b56cu
static const uint32_t offer_words[] = { W_ROUND, W_OFFER, W_WORK, W_FLOOR, W_ACC, W_ASK, DAT_RNG };
#define N_OW ((int)(sizeof offer_words / sizeof offer_words[0]))
static void offer_hook(hook_t *h, int down)
{
    h->calls++;
    uint32_t va = down ? 0x4603cfu : 0x460161u;
    if (h->mode == REPLACE) { rh_fatal("%s: REPLACE not wired (shadow only)", h->name); }
    uint32_t pre[N_OW], ret_[N_OW];
    for (int i = 0; i < N_OW; i++) pre[i] = G32(offer_words[i]);
    /* inputs the port takes as parameters (read before retail mutates anything) */
    int32_t idx = (int32_t)G32(W_IDX);
    uint32_t rec = 0x6a5ea90u + (uint32_t)idx * 0x2c670u;
    haggle_customer_t c = { (int32_t)G32(rec + 0x51c8), (int32_t)G32(rec + 0x51cc), (int32_t)G32(rec + 0x51bc),
                            (int32_t)G32(rec + 0x51c0), (int32_t)G32(rec + 0x51c4), 0, 0 };
    int32_t trend = (int32_t)G32(W_ROUND) == 0 ? ((int32_t (__cdecl *)(int32_t))0x4361b2u)((int32_t)G32(0x730b5a4u)) : 0;
    int tut = *(volatile uint8_t *)(uintptr_t)(0x450f406u + 188360u * G32(0x438b1e0u)) != 0;
    ((void (__cdecl *)(void))(uintptr_t)va)();
    for (int i = 0; i < N_OW; i++) { ret_[i] = G32(offer_words[i]); G32(offer_words[i]) = pre[i]; }
    rh_log("haggle %s f%u idx=%d round=%u base=%d ask=%d trend=%d tut=%d -> offer=%d work=%d floor=%d acc=%d\n",
           down ? "offer_down" : "offer_up", g_frame, idx, pre[0], (int)G32(W_BASE), (int)pre[5], trend, tut,
           (int)ret_[1], (int)ret_[2], (int)ret_[3], (int)ret_[4]);
    haggle_state_t st = { (int32_t)pre[0], (int32_t)pre[1], (int32_t)pre[2], (int32_t)pre[3], (int32_t)pre[4] };
    int32_t ask = (int32_t)pre[5];
    rng_seed(pre[6]);
    if (!down) haggle_offer_up(&st, &c, (int32_t)G32(W_BASE), ask, trend, tut);
    else haggle_offer_down(&st, &c, (int32_t)G32(W_BASE), &ask, trend, tut, idx);
    uint32_t port[N_OW] = { (uint32_t)st.round, (uint32_t)st.offer, (uint32_t)st.work_price, (uint32_t)st.floor,
                            (uint32_t)st.accept_ref, (uint32_t)ask, g_rng_seed };
    if (memcmp(port, ret_, sizeof port))
        note(h, "idx=%d round=%u trend=%d tut=%d retail offer=%d work=%d floor=%d acc=%d rng=%08x | port offer=%d work=%d floor=%d acc=%d rng=%08x",
             idx, pre[0], trend, tut, ret_[1], ret_[2], ret_[3], ret_[4], ret_[6], port[1], port[2], port[3], port[4], port[6]);
    for (int i = 0; i < N_OW; i++) G32(offer_words[i]) = ret_[i];   /* retail stays authoritative */
}
static void __cdecl hook_offer_up(void)   { offer_hook(&H[H_OFFER_UP], 0); }
static void __cdecl hook_offer_down(void) { offer_hook(&H[H_OFFER_DOWN], 1); }

/* ---- floor layout (FUN_0047df4d): seed the port tables from retail, run retail, run the
 * port on the same inputs + LCG state, compare every table it writes ---- */
static int ret_43195d(void) { return ((int (__cdecl *)(void))0x43195du)(); }
static int ret_4319d6(void) { return ((int (__cdecl *)(void))0x4319d6u)(); }
typedef struct { const char *name; uint32_t va; void *port; size_t n; } region_t;
static const region_t floor_regions[] = {
    { "W",          0x5cc2e8u,  &g_floor_w,             4 },
    { "H",          0x5cc2ecu,  &g_floor_h,             4 },
    { "maze",       0x73e11bcu, g_floor_maze,           sizeof g_floor_maze },
    { "ncells",     0x73e1540u, &g_floor_ncells,        4 },
    { "list",       0x73e008cu, g_floor_list,           sizeof g_floor_list },
    { "grid",       0x73dfd08u, g_floor_grid,           sizeof g_floor_grid },
    { "rooms",      0x73e03acu, g_floor_rooms,          sizeof g_floor_rooms },
    { "origin",     0x438b1f8u, g_floor_origin,         8 },
    { "extra_n",    0x438b430u, &g_floor_extra_n,       4 },
    { "marks",      0x438b434u, g_floor_marks,          sizeof g_floor_marks },
    { "pick_pos",   0x438b200u, g_floor_pick_pos,       sizeof g_floor_pick_pos },
    { "pick_cell",  0x438b2f0u, g_floor_pick_cell,      sizeof g_floor_pick_cell },
    { "pick_type",  0x438b390u, g_floor_pick_type,      sizeof g_floor_pick_type },
    { "pick_flag",  0x438b3e0u, g_floor_pick_flag,      sizeof g_floor_pick_flag },
    { "rooms_total",0x438b4b0u, &g_floor_rooms_total,   4 },
    { "c148",       0x438c148u, &g_floor_c148,          4 },
    { "spawn",      0x438b1ecu, g_scene1_stage_player_default_pos, 12 },
    { "dbgtext",    0x6a47aacu, g_audio_mci_buffer,     AUDIO_MCI_BUFFER_SIZE },
};
#define N_FR ((int)(sizeof floor_regions / sizeof floor_regions[0]))
static void __cdecl hook_floor_layout(void)
{
    hook_t *h = &H[H_FLOOR]; h->calls++;
    if (h->mode == REPLACE) rh_fatal("%s: REPLACE not wired (shadow only)", h->name);
    const uint32_t rec = 0x44e3798u + 188360u * G32(0x438b1e0u), pal = G32(0x68dd2f0u);
    floor_layout_in_t in = { (int32_t)G32(0x438b4c8u), (int32_t)G32(0x438b4ccu), (int32_t)G32(0x438b4dcu),
                             pal && G32(pal) == 1, *(volatile uint8_t *)(uintptr_t)(rec + 0x2bce2u),
                             *(volatile uint8_t *)(uintptr_t)(rec + 0x2bcf8u), ret_43195d, ret_4319d6 };
    for (int i = 0; i < N_FR; i++) memcpy(floor_regions[i].port, (void *)(uintptr_t)floor_regions[i].va, floor_regions[i].n);
    const uint32_t seed0 = G32(DAT_RNG);
    ((void (__cdecl *)(void))0x47df4du)();
    rng_seed(seed0);
    floor_layout_generate(&in);
    if (g_rng_seed != G32(DAT_RNG)) note(h, "rng retail %08x port %08x", G32(DAT_RNG), g_rng_seed);
    char b[160]; size_t o = 0; b[0] = 0;
    for (int i = 0; i < N_FR; i++) {
        const region_t *r = &floor_regions[i];
        if (!memcmp(r->port, (void *)(uintptr_t)r->va, r->n)) continue;
        size_t k = 0;
        while (((uint8_t *)r->port)[k] == *(volatile uint8_t *)(uintptr_t)(r->va + k)) k++;
        o += snprintf(b + o, sizeof b - o, "%s+%zx ", r->name, k);
    }
    rh_log("floor_layout f%u cls=%d floor=%d -> W=%d H=%d ncells=%d start=(%d,%d) exit=(%d,%d) extra=%d rng=%08x\n",
           g_frame, in.stage_class, in.floor, (int)G32(0x5cc2e8u), (int)G32(0x5cc2ecu), (int)G32(0x73e1540u),
           (int)G32(0x438b434u), (int)G32(0x438b438u), (int)G32(0x438b43cu), (int)G32(0x438b440u), (int)G32(0x438b430u), G32(DAT_RNG));
    if (b[0]) note(h, "cls=%d floor=%d differs: %s", in.stage_class, in.floor, b);
}

/* ---- actor-table reset (FUN_0041f252): spawn order + counters + LCG ---- */
static void __cdecl hook_actor_reset(void)
{
    hook_t *h = &H[H_ACTOR_RST]; h->calls++;
    if (h->mode == REPLACE) rh_fatal("%s: REPLACE not wired (shadow only)", h->name);
    const uint32_t seed0 = G32(DAT_RNG);
    ((void (__cdecl *)(void))0x41f252u)();
    rng_seed(seed0);
    g_actor_spawn_7c8c58 = g_actor_spawn_done = g_actor_spawn_serial = -1;
    actor_spawn_reset();
    int bad = g_rng_seed != G32(DAT_RNG) || g_actor_spawn_7c8c58 != (int32_t)G32(0x7c8c58u) ||
              g_actor_spawn_done != (int32_t)G32(0x7c8c5cu) || g_actor_spawn_serial != (int32_t)G32(0x7c8c60u);
    for (int i = 0; i < ACTOR_SPAWN_ORDER_N; i++) bad |= g_actor_spawn_order[i] != (int32_t)G32(0x7c8c38u + 4u * i);
    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) {
        uint32_t e = 0x76bd54u + 0xba4u * (uint32_t)i;
        bad |= G32(e + 0x44u) != 0 || G32(e + 0x6ecu) != 0 || G32(e + 0x774u) != 0;
    }
    if (bad) note(h, "retail order %d%d%d%d%d%d%d%d rng %08x | port %d%d%d%d%d%d%d%d rng %08x",
                  G32(0x7c8c38u), G32(0x7c8c3cu), G32(0x7c8c40u), G32(0x7c8c44u), G32(0x7c8c48u), G32(0x7c8c4cu), G32(0x7c8c50u), G32(0x7c8c54u), G32(DAT_RNG),
                  g_actor_spawn_order[0], g_actor_spawn_order[1], g_actor_spawn_order[2], g_actor_spawn_order[3],
                  g_actor_spawn_order[4], g_actor_spawn_order[5], g_actor_spawn_order[6], g_actor_spawn_order[7], g_rng_seed);
}

/* ---- seller customer (FUN_00461792): the b5xx words + queue entry + LCG ---- */
static int32_t __cdecl hook_seller(void)
{
    hook_t *h = &H[H_SELLER]; h->calls++;
    if (h->mode == REPLACE) rh_fatal("%s: REPLACE not wired (shadow only)", h->name);
    const uint32_t rec = 0x44e3798u + 188360u * G32(0x438b1e0u);
    const uint32_t ea = 0x730aca0u + 24u * G32(0x730b1a8u + 4u * G32(0x730b318u));
    seller_state_t st = { (int32_t)G32(0x730b56cu), (int32_t)G32(0x730b570u), (int32_t)G32(0x730b5a4u),
                          (int32_t)G32(0x730b5a8u), (int32_t)G32(0x730b5acu) };
    int32_t entry[6];
    memcpy(entry, (void *)(uintptr_t)ea, sizeof entry);
    memcpy(g_item.records, (void *)(uintptr_t)0x95d37d0u, sizeof g_item.records);
    g_item.count = (int32_t)G32(0x5c80acu);
    const int32_t owned = ((int32_t (__cdecl *)(void))0x491b16u)();
    const uint32_t seed0 = G32(DAT_RNG);
    const int32_t r = ((int32_t (__cdecl *)(void))0x461792u)();
    rng_seed(seed0);
    const int p = seller_select((const uint8_t *)(uintptr_t)rec, owned, entry, &st);
    const int32_t ret_[7] = { r, (int32_t)G32(0x730b56cu), (int32_t)G32(0x730b570u), (int32_t)G32(0x730b5a4u),
                              (int32_t)G32(0x730b5a8u), (int32_t)G32(0x730b5acu), (int32_t)G32(ea) };
    const int32_t port[7] = { p, st.b56c, st.b570, st.b5a4, st.b5a8, st.b5ac, entry[0] };
    rh_log("seller f%u k=%d idx=%d owned=%d -> r=%d b5a4=%x(id %d) b5ac=%d rng=%08x\n", g_frame, ret_[1], ret_[2],
           owned, r, ret_[3], ret_[3] >> 6, ret_[5], G32(DAT_RNG));
    if (memcmp(ret_, port, sizeof port) || g_rng_seed != G32(DAT_RNG))
        note(h, "retail r=%d k=%d idx=%d b5a4=%x b5a8=%d b5ac=%d q=%d rng=%08x | port r=%d k=%d idx=%d b5a4=%x b5a8=%d b5ac=%d q=%d rng=%08x",
             ret_[0], ret_[1], ret_[2], ret_[3], ret_[4], ret_[5], ret_[6], G32(DAT_RNG),
             port[0], port[1], port[2], port[3], port[4], port[5], port[6], g_rng_seed);
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
    [H_OFFER_UP]   = { "haggle_offer_up",   0x460161u, 0, (void *)hook_offer_up },
    [H_OFFER_DOWN] = { "haggle_offer_down", 0x4603cfu, 0, (void *)hook_offer_down },
    [H_FLOOR]      = { "floor_layout",      0x47df4du, 0, (void *)hook_floor_layout },
    [H_ACTOR_RST]  = { "actor_spawn_reset", 0x41f252u, 0, (void *)hook_actor_reset },
    [H_SELLER]     = { "seller_select",     0x461792u, 0, (void *)hook_seller },
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
        if (m != OFF && h->va == VA_RAND) h->sites += redirect_calls(0x471084u, h->hook);   /* j__rand thunk callers */
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
