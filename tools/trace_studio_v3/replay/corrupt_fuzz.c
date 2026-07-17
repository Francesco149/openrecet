/* GX-05 acceptance — the v3 container READER fails SAFELY on truncated/corrupt input
 * (roadmap parity-evidence §9 GX-05: "truncated/corrupt containers fail safely").
 *
 * Reuses the AUTHORITATIVE replay_core.c step() (the one the viewer + pixel producer
 * run) with do_res=0, do_calls=0: EVERY device call in step() is gated on do_res/
 * do_calls, so this is a PURE PARSE — no D3D device is created or touched, so the
 * fuzzer runs headless. Over crafted truncated / corrupt-length / integer-overflow /
 * unknown-opcode record streams (plus a deterministic random fuzz), it asserts the
 * cursor NEVER escapes the buffer [buf, buf+len] and every walk TERMINATES — i.e. no
 * out-of-bounds read, no hang. This is the reader half of GX-05; gx05_fixture.exe is
 * the dedup (byte-compare) half.
 *
 * The container HEADER (magic/version/DEV_PARAMS) is parsed separately in
 * orv3_replay_open with cu() (already EOF-clamped) + explicit magic/DEV_PARAMS checks,
 * so the OOB risk lives entirely in the record walk — which is what this exercises.
 *
 * Build: `nix develop --command make corrupt_fuzz.exe` in this dir. Run:
 * `corrupt_fuzz.exe` — exit 0 = every case failed safely; exit 1 = a cursor escaped or
 * a walk hung (the bug GX-05 closes). */
#include "replay_core.c"   /* pulls in step(), cu(), cspan(), ORV3_* opcodes, OrV3Replay */
#include <assert.h>

/* a tiny growable u32/bytes writer for building record streams */
typedef struct { uint8_t *b; size_t n, cap; } W;
static void wput(W *w, const void *p, size_t k) {
    if (w->n + k > w->cap) { w->cap = w->cap ? w->cap : 256; while (w->cap < w->n + k) w->cap *= 2;
                             w->b = (uint8_t *)realloc(w->b, w->cap); }
    memcpy(w->b + w->n, p, k); w->n += k;
}
static void wu(W *w, uint32_t v) { wput(w, &v, 4); }

static int g_fail;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "  FAIL: %s\n", msg); g_fail = 1; } \
                              else printf("  ok  %s\n", msg); } while (0)

/* Walk a record buffer with the REAL step() and assert cursor-safety + termination.
 * Returns NULL if the walk is safe (terminated with the cursor always in-bounds), else
 * a reason string. do_res=do_calls=0 ⇒ no D3D call is issued (dev stays untouched). */
static const char *walk_safely(const uint8_t *buf, size_t len) {
    OrV3Replay R; memset(&R, 0, sizeof R);   /* dev=NULL; never dereferenced when do_res=do_calls=0 */
    Cur c = { buf, buf + len };
    int nd = 0;
    for (long i = 0; i < 2000000L; i++) {
        const uint8_t *before = c.p;
        uint32_t op = step(&c, &R, /*do_res*/0, /*do_calls*/0, /*lo*/0, /*hi*/-1, &nd);
        if (c.p < buf || c.p > c.end) return "cursor escaped buffer";   /* the OOB the hardening forbids */
        if (op == ORV3_EOF || op == 0xfffffffeu) return NULL;           /* terminated safely (EOF / unknown op) */
        if (c.p == before) return "no progress (would hang)";
    }
    return "did not terminate";
}

int main(void) {
    printf("corrupt_fuzz — the v3 reader must fail safely on bad containers:\n");

    /* POSITIVE control: a valid Clear+Present frame walks to Present then EOF, safely. */
    { W w = {0}; wu(&w, ORV3_Clear); wu(&w, 0); wu(&w, 1); wu(&w, 0xff000000u); wu(&w, 0); wu(&w, 0);
      wu(&w, ORV3_Present); wu(&w, 0);
      CHECK(walk_safely(w.b, w.n) == NULL, "positive control: a valid frame walks safely"); free(w.b); }

    /* truncated mid-record: an opcode with no payload. */
    { W w = {0}; wu(&w, ORV3_RES_VB);
      CHECK(walk_safely(w.b, w.n) == NULL, "truncated RES_VB (opcode, no payload)"); free(w.b); }

    /* corrupt huge byte-length. */
    { W w = {0}; wu(&w, ORV3_RES_VB); wu(&w, 0); wu(&w, 12); wu(&w, 0); wu(&w, 0xffffffffu); wu(&w, 0xdeadu);
      CHECK(walk_safely(w.b, w.n) == NULL, "corrupt RES_VB dl=0xffffffff"); free(w.b); }
    { W w = {0}; wu(&w, ORV3_RES_IB); wu(&w, 0); wu(&w, 12); wu(&w, 101); wu(&w, 0x40000000u); wu(&w, 0);
      CHECK(walk_safely(w.b, w.n) == NULL, "corrupt RES_IB dl=0x40000000"); free(w.b); }

    /* count INTEGER-OVERFLOW: count*16 wraps to a small product on 32-bit size_t — the
     * cspan_n division-domain guard must still reject it. */
    { W w = {0}; wu(&w, ORV3_Clear); wu(&w, 0x10000000u); for (int i = 0; i < 6; i++) wu(&w, 0);
      CHECK(walk_safely(w.b, w.n) == NULL, "Clear count=0x10000000 (count*16 overflow guard)"); free(w.b); }
    { W w = {0}; wu(&w, ORV3_Clear); wu(&w, 0xffffffffu); for (int i = 0; i < 6; i++) wu(&w, 0);
      CHECK(walk_safely(w.b, w.n) == NULL, "Clear count=0xffffffff"); free(w.b); }
    { W w = {0}; wu(&w, ORV3_CopyRects); wu(&w, 1); wu(&w, 0); wu(&w, 1); wu(&w, 0); wu(&w, 0xffffffffu);
      CHECK(walk_safely(w.b, w.n) == NULL, "CopyRects count=0xffffffff"); free(w.b); }

    /* unknown opcode terminates the walk (not a silent misread). */
    { W w = {0}; wu(&w, 0x12345678u);
      CHECK(walk_safely(w.b, w.n) == NULL, "unknown opcode terminates"); free(w.b); }

    /* UP-draw inline data with a corrupt length. */
    { W w = {0}; wu(&w, ORV3_DrawPrimitiveUP); wu(&w, D3DPT_TRIANGLELIST); wu(&w, 1); wu(&w, 24); wu(&w, 0xffffffffu);
      CHECK(walk_safely(w.b, w.n) == NULL, "DrawPrimitiveUP dl=0xffffffff"); free(w.b); }
    { W w = {0}; wu(&w, ORV3_DrawIndexedPrimitiveUP); wu(&w, D3DPT_TRIANGLELIST); wu(&w, 0); wu(&w, 3);
      wu(&w, 1); wu(&w, 101); wu(&w, 0xffffffffu);
      CHECK(walk_safely(w.b, w.n) == NULL, "DrawIndexedPrimitiveUP il=0xffffffff"); free(w.b); }

    /* fixed-size payloads (matrix 64B / material 68B) truncated. */
    { W w = {0}; wu(&w, ORV3_SetTransform); wu(&w, 256); wu(&w, 0);   /* only 4 of 64 matrix bytes */
      CHECK(walk_safely(w.b, w.n) == NULL, "SetTransform truncated 64B matrix"); free(w.b); }
    { W w = {0}; wu(&w, ORV3_SetMaterial); wu(&w, 0);                 /* only 4 of 68 material bytes */
      CHECK(walk_safely(w.b, w.n) == NULL, "SetMaterial truncated 68B material"); free(w.b); }

    /* RES_TEX with a huge level COUNT and a huge per-level datalen. */
    { W w = {0}; wu(&w, ORV3_RES_TEX); wu(&w, 0); wu(&w, 0xffffffffu);       /* levels huge */
      wu(&w, 4); wu(&w, 4); wu(&w, 21); wu(&w, 16); wu(&w, 0xffffffffu);     /* one level, huge ld */
      CHECK(walk_safely(w.b, w.n) == NULL, "RES_TEX huge levels + ld"); free(w.b); }

    /* DETERMINISTIC fuzz (LCG, no time/rand seed ⇒ reproducible): random opcodes with
     * random payloads, TRUNCATED at a random offset. Every walk must stay safe. */
    uint32_t s = 0x9e3779b9u;
    int fuzz_ok = 1;
    for (int t = 0; t < 40000 && fuzz_ok; t++) {
        W w = {0};
        int nrec = 2 + (int)((s >> 3) % 12u);
        for (int r = 0; r < nrec; r++) {
            s = s * 1664525u + 1013904223u;
            wu(&w, 2u + (s % 30u));                    /* opcodes span RES..CopyRects + a little junk */
            int npad = (int)((s >> 7) % 10u);
            for (int k = 0; k < npad; k++) { s = s * 1664525u + 1013904223u; wu(&w, s); }
        }
        s = s * 1664525u + 1013904223u;
        size_t len = w.n ? (size_t)(s % (uint32_t)(w.n + 1)) : 0;   /* truncate anywhere */
        const char *why = walk_safely(w.b, len);
        if (why) { fprintf(stderr, "  FAIL: fuzz t=%d len=%zu: %s\n", t, len, why); g_fail = 1; fuzz_ok = 0; }
        free(w.b);
    }
    CHECK(fuzz_ok, "40000 deterministic fuzz inputs all walked safely");

    if (g_fail) { fprintf(stderr, "corrupt_fuzz: FAILED\n"); return 1; }
    printf("corrupt_fuzz: OK (reader fails safely on truncated/corrupt containers)\n");
    return 0;
}
