/* P1 — haggle offer oracle: retail FUN_00460161 (offer UP) / FUN_004603cf (offer
 * DOWN) called directly vs the port's haggle_offer_up/down built -std=c11 AND
 * -std=gnu11 (the x87 float-semantics sweep, docs/FRONT.md).
 *
 * Retail inputs (IDA sub_460161 / sub_4603CF):
 *   round DAT_0730b584, offer 0730b574, work_price 0730b57c, floor 0730b580,
 *   accept_ref 0730b588, base DAT_005c6bc0, player_ask DAT_005c6bb8,
 *   customer idx DAT_0730b56c → kyaku record 0x6a5ea90 + idx*0x2c670:
 *   +0x51bc gullibility, +0x51c0 rise1, +0x51c4 rise2, +0x51c8 initial, +0x51cc random;
 *   price trend = FUN_004361b2(DAT_0730b5a4) → call sites inside the two functions
 *   are redirected to a stub returning the case's trend;
 *   tutorial = byte 0x450f406 + 188360*DAT_0438b1e0 (slot 0 used).
 *   RNG: DAT_006023a0 (port rng_seed/g_rng_seed).
 * Compared: every output global + the RNG state after the call. */
#include "retail_map.h"
#include <float.h>
#include "rng.h"
#include "customer_haggle.h"

RETAIL_HOLE_DEFINE
#define G32(va) (*(volatile int32_t *)(uintptr_t)(va))
#define G8(va)  (*(volatile uint8_t *)(uintptr_t)(va))

void haggle_offer_up_gnu11(haggle_state_t *, const haggle_customer_t *, int32_t, int32_t, int32_t, int);
void haggle_offer_down_gnu11(haggle_state_t *, const haggle_customer_t *, int32_t, int32_t *, int32_t, int, int32_t);

static int32_t g_trend;
static int32_t __cdecl trend_stub(int32_t item) { (void)item; return g_trend; }
static int patch_calls_in(uint32_t lo, uint32_t len, uint32_t target, void *hook)
{
    int n = 0;
    for (uint8_t *p = (uint8_t *)(uintptr_t)lo, *e = p + len - 5; p < e; p++) {
        if (*p != 0xE8) continue;
        uint32_t t = (uint32_t)(uintptr_t)p + 5 + *(int32_t *)(p + 1);
        if (t != target) continue;
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hook - ((uint32_t)(uintptr_t)p + 5));
        n++;
    }
    return n;
}

static uint32_t xs = 0x9e3779b9u;
static uint32_t xr(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static int32_t pick(int32_t lo, int32_t hi) { return lo + (int32_t)(xr() % (uint32_t)(hi - lo + 1)); }

typedef struct { int32_t round, offer, work, floor_, acc, base, ask, idx, trend, tut, seed; haggle_customer_t c; } case_t;
typedef struct { int32_t round, offer, work, floor_, acc, ask; uint32_t rng; } out_t;

static void make_case(case_t *k)
{
    k->round = pick(0, 4);
    k->base = (xr() & 3) ? pick(1, 3000) : pick(3000, 2000000);
    k->work = k->round ? pick(1, k->base * 3 + 10) : 0;
    k->offer = k->round ? pick(0, k->base * 3 + 10) : 0;
    k->floor_ = pick(0, 1000); k->acc = pick(0, 1000);
    k->ask = pick(0, k->base * 3 + 10);
    k->idx = (xr() % 8 == 0) ? 18 : pick(0, 30);
    k->trend = pick(-3, 2);
    k->tut = (xr() % 6) == 0;
    k->seed = (int32_t)xr();
    k->c.gullibility = pick(0, 20); k->c.rise1 = pick(0, 60); k->c.rise2 = pick(0, 60);
    k->c.initial = pick(10, 150); k->c.random = pick(0, 15);
    k->c.budget_low = k->c.budget_high = 0;
}

static void retail_run(const case_t *k, int down, out_t *o)
{
    uint32_t rec = 0x6a5ea90u + (uint32_t)k->idx * 0x2c670u;
    G32(rec + 0x51bc) = k->c.gullibility; G32(rec + 0x51c0) = k->c.rise1; G32(rec + 0x51c4) = k->c.rise2;
    G32(rec + 0x51c8) = k->c.initial; G32(rec + 0x51cc) = k->c.random;
    G32(0x730b56c) = k->idx; G32(0x730b584) = k->round; G32(0x730b574) = k->offer; G32(0x730b57c) = k->work;
    G32(0x730b580) = k->floor_; G32(0x730b588) = k->acc; G32(0x5c6bc0) = k->base; G32(0x5c6bb8) = k->ask;
    G32(0x438b1e0) = 0; G8(0x450f406) = (uint8_t)k->tut;
    G32(0x6023a0) = k->seed; g_trend = k->trend;
    retail_call0(down ? 0x4603cfu : 0x460161u);
    o->round = G32(0x730b584); o->offer = G32(0x730b574); o->work = G32(0x730b57c); o->floor_ = G32(0x730b580);
    o->acc = G32(0x730b588); o->ask = G32(0x5c6bb8); o->rng = (uint32_t)G32(0x6023a0);
}
static void port_run(const case_t *k, int down, int gnu, out_t *o)
{
    haggle_state_t st = { k->round, k->offer, k->work, k->floor_, k->acc };
    int32_t ask = k->ask;
    rng_seed((uint32_t)k->seed);
    if (!down) (gnu ? haggle_offer_up_gnu11 : haggle_offer_up)(&st, &k->c, k->base, ask, k->trend, k->tut);
    else (gnu ? haggle_offer_down_gnu11 : haggle_offer_down)(&st, &k->c, k->base, &ask, k->trend, k->tut, k->idx);
    o->round = st.round; o->offer = st.offer; o->work = st.work_price; o->floor_ = st.floor; o->acc = st.accept_ref;
    o->ask = ask; o->rng = g_rng_seed;
}
static int same(const out_t *a, const out_t *b) { return !memcmp(a, b, sizeof *a); }

int main(int argc, char **argv)
{
    retail_image_t img;
    const char *exe = argc > 1 ? argv[1] : "recettear.unpacked.exe";
    long n_per = argc > 2 ? atol(argv[2]) : 300000;
    if (retail_map(exe, &img, 1)) return 1;
    int pu = patch_calls_in(0x460161, 622, 0x4361b2, trend_stub), pd = patch_calls_in(0x4603cf, 675, 0x4361b2, trend_stub);
    printf("trend call sites patched: up=%d down=%d\n", pu, pd);
    unsigned int cw_modes[] = {_PC_53, _PC_24, _PC_64};
    const char *cw_names[] = {"PC53", "PC24", "PC64"};
    for (int m = 0; m < 3; m++) {
        unsigned int old; _controlfp_s(&old, cw_modes[m], _MCW_PC);
        for (int down = 0; down < 2; down++) {
            long n = 0, mis[2] = {0, 0}, by_round[2][5] = {{0}};
            for (long i = 0; i < n_per; i++) {
                case_t k; make_case(&k);
                if (down && k.idx == 18) k.ask = pick(0, 400000);   /* ×5 path: keep ask*5 in range */
                out_t r, p[2];
                retail_run(&k, down, &r);
                for (int g = 0; g < 2; g++) {
                    port_run(&k, down, g, &p[g]);
                    if (!same(&r, &p[g])) {
                        mis[g]++; by_round[g][k.round]++;
                        if (mis[g] <= 3 && m == 0)
                            printf("  [%s %s %s] round=%d base=%d work=%d offer=%d ask=%d idx=%d trend=%d tut=%d: retail offer=%d work=%d floor=%d acc=%d rng=%08x | port offer=%d work=%d floor=%d acc=%d rng=%08x\n",
                                   cw_names[m], down ? "down" : "up", g ? "gnu11" : "c11", k.round, k.base, k.work, k.offer, k.ask, k.idx, k.trend, k.tut,
                                   r.offer, r.work, r.floor_, r.acc, r.rng, p[g].offer, p[g].work, p[g].floor_, p[g].acc, p[g].rng);
                    }
                }
                n++;
            }
            printf("offer_%-4s [%s]: c11 %ld/%ld match (by round %ld/%ld/%ld/%ld/%ld mism) | gnu11 %ld/%ld match (by round %ld/%ld/%ld/%ld/%ld mism)\n",
                   down ? "down" : "up", cw_names[m], n - mis[0], n, by_round[0][0], by_round[0][1], by_round[0][2], by_round[0][3], by_round[0][4],
                   n - mis[1], n, by_round[1][0], by_round[1][1], by_round[1][2], by_round[1][3], by_round[1][4]);
        }
        _controlfp_s(&old, _PC_53, _MCW_PC);
    }
    return 0;
}
