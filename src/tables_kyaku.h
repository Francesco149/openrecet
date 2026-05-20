/*
 * tables_kyaku.h — parser for `data/kyaku.txt` (block #4 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `kyaku.txt` (客 = "customer") defines the 50-slot customer/character
 * roster: each record carries a singular and plural name, a buying
 * profile (preferred attribute mask + preferred item categories +
 * budget range + suspicion / gullibility / haggle-rise stats), an
 * activity-time-of-day mask, and a per-character dialog-file path.
 *
 * Per-line shape (vendor file):
 *
 *     013:Woman#Women              ← header: id : singular [# plural]
 *     名前番号:13                  ← name-table index
 *     属性:0,4                     ← attr x,y
 *     好き属性:食品派手貴金…       ← preferred-attribute SJIS tokens
 *     好き種類:Medicines           ← preferred item category (English; up to 20 lines)
 *     好き種類:Clothes
 *     ...
 *     嫌い:                        ← dislikes (engine matches key but discards body)
 *     予算:3000-300000             ← budget range
 *     活動時間:夕夜                ← activity time SJIS tokens
 *     疑:                          ← suspicion
 *     ランダム:3                   ← random spread
 *     初回:120                     ← first-pass acceptance %
 *     騙:20                        ← gullibility (騙される度)
 *     上昇１:10                    ← haggle rise #1
 *     上昇２:10                    ← haggle rise #2
 *     file:kyaku/f3.txt            ← per-character dialog file
 *
 * Lines starting with `/`, `\r`, or `\n` are comments/blanks.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_KYAKU_H
#define OPENRECET_TABLES_KYAKU_H

#include <stddef.h>
#include <stdint.h>

/* Total customer slots. Engine span `&DAT_06a5ea90`..`&DAT_07b8a7e0`
 * at stride 0x2c670 = 50 records (the engine record holds per-customer
 * dialog buffers, which is why the stride is so large; the port only
 * carries the table-loader fields). */
#define KYAKU_COUNT 50

/* Per-record name buffer width (engine: 0x20 byte field at +0x04 for
 * singular, +0x24 for joint). The parser caps the header consume at
 * 32 chars total, so 33-byte buffers cover any vendor entry plus a
 * safety NUL. */
#define KYAKU_NAME_LEN 33

/* Per-record file_path width (engine: 0x100 byte field at +0x5044). */
#define KYAKU_FILE_LEN 257

/* Per-record preferred-category cap (engine: 0x14 == 20 entries at
 * +0x5158, with the count at +0x51a8 gated by `< 0x14`). Vendor
 * customer Alouette ships the most: 11 entries — comfortable margin. */
#define KYAKU_LIKE_KIND_MAX 20

/* Activity-time mask bit positions, mirroring the parser at
 * DAT_005cadf0..DAT_005cadfc (the 4 2-byte SJIS tokens that get bit-
 * OR'd into +0x51b4 when matched). Tokens not in this set (e.g.
 * `本人` / `試`) match nothing and leave the mask at 0. */
#define KYAKU_TIME_MORNING (1u << 0)  /* 朝 0x92 0xA9 */
#define KYAKU_TIME_NOON    (1u << 1)  /* 昼 0x92 0x8B */
#define KYAKU_TIME_EVENING (1u << 2)  /* 夕 0x97 0x5B */
#define KYAKU_TIME_NIGHT   (1u << 3)  /* 夜 0x96 0xE9 */

/*
 * One customer record. The engine's per-customer buffer is 0x2c670
 * bytes (most of which holds dialog text for that customer's file:
 * script); the table-loader at FUN_00475270 block #4 only writes the
 * fields listed below. Engine-relative offsets are noted for cross-
 * reference but the port does NOT enforce them via _Static_assert —
 * the engine's stride is hostile to a clean C struct, so we lift the
 * meaningful fields into a flat layout.
 *
 *   engine_off   field
 *   +0x0000      name_index        (`名前番号:N`)
 *   +0x0004      singular[0x20]    (header before '#')
 *   +0x0024      joint[0x20]       (header, plural overwrites singular at offset 0)
 *   +0x5044      file_path[0x100]  (`file:NNN`)
 *   +0x514c      active            (1 once the `NNN:` header is matched)
 *   +0x5150      attr_x            (`属性:X,Y` field 1)
 *   +0x5154      attr_y            (`属性:X,Y` field 2)
 *   +0x5158      like_kinds[0x14]  (`好き種類:` resolved category ids)
 *   +0x51a8      like_count        (entries in like_kinds; init=0, cap=0x14)
 *   +0x51ac      like_attr_mask    (`好き属性:` OR-of-tag-bits)
 *   +0x51b4      activity_time_mask(`活動時間:` OR-of-bits 1/2/4/8)
 *   +0x51b8      suspicion         (`疑:N`)
 *   +0x51bc      gullibility       (`騙:N`)
 *   +0x51c0      rise1             (`上昇１:N`)
 *   +0x51c4      rise2             (`上昇２:N`)
 *   +0x51c8      initial           (`初回:N`)
 *   +0x51cc      random            (`ランダム:N`)
 *   +0x51d0      budget_low        (`予算:L-H` low)
 *   +0x51d4      budget_high       (`予算:L-H` high)
 */
typedef struct {
    int32_t  active;                /* 1 if a `NNN:` header was matched */
    int32_t  name_index;            /* 名前番号 — index into name table */
    char     singular[KYAKU_NAME_LEN];
    char     joint[KYAKU_NAME_LEN];
    char     file_path[KYAKU_FILE_LEN];
    int32_t  attr_x;
    int32_t  attr_y;
    int32_t  like_kinds[KYAKU_LIKE_KIND_MAX];
    int32_t  like_count;
    uint32_t like_attr_mask;        /* 好き属性 — FUN_0049e9a7|FUN_0049eb2a OR'd */
    uint32_t activity_time_mask;    /* 活動時間 — KYAKU_TIME_* bits */
    int32_t  suspicion;             /* 疑 */
    int32_t  gullibility;           /* 騙 — "easily deceived" */
    int32_t  rise1;                 /* 上昇１ */
    int32_t  rise2;                 /* 上昇２ */
    int32_t  initial;               /* 初回 — first-pass acceptance % */
    int32_t  random;                /* ランダム */
    int32_t  budget_low;
    int32_t  budget_high;
} kyaku_record_t;

typedef struct {
    kyaku_record_t records[KYAKU_COUNT];
} kyaku_state_t;

/* Engine-global state, populated from src/tables.c. */
extern kyaku_state_t g_kyaku;

/*
 * Category-name → category-id resolver callback. The engine looks up
 * `好き種類:` lines against the per-category name table at
 * `&DAT_0963e5f8` (populated by item.txt's `:Category#(tag)` headers,
 * stride 0x20 bytes, 256-slot span). Each populated slot's NUL-
 * terminated name is exact-matched against the line tail; the slot
 * index of the first match is stored.
 *
 * `name` is a NUL-terminated string from the `好き種類:` value. `user`
 * is the opaque value passed through from `tables_parse_kyaku`.
 *
 * Returns the resolved category id (>= 0) on success, or -1 on miss.
 * When `tables_parse_kyaku` is invoked with a NULL resolver, every
 * `好き種類:` line resolves to -1 — the convention used by unit tests
 * that don't bind item.txt state.
 */
typedef int32_t (*kyaku_resolve_fn)(const char *name, void *user);

/*
 * Parse a kyaku.txt buffer into `*out`. Mirrors FUN_00475270 block #4
 * (L469..L832 of docs/decompiled/by-address/475270.c).
 *
 * Pre-conditions: `*out` need not be initialised; this function memsets
 * it before parsing. After return:
 *   - For each customer header line `NNN:Singular#Plural`, the matching
 *     `records[N]` has `active = 1`, `singular[]` filled from the part
 *     before '#', and `joint[]` filled from the whole line with the '#'
 *     write-position reset (so joint ends up holding the plural, with
 *     any leftover suffix from singular intact).
 *   - Per-key lines append into the *current* record (set by the most
 *     recent NNN: header). Lines before any header silently no-op
 *     (engine's `local_14 < 0` branch).
 *
 * Line dispatch (engine, in match order):
 *   '/', '\r', '\n'    — comment / blank → skipped
 *   '0' first byte     — try `NNN:` header for N in 0..49
 *   `名前番号:`        — atoi into name_index
 *   `属性:`            — atoi field1, atoi after-',' field2
 *   `予算:`            — atoi field1, atoi after-'-' field2
 *   `好き種類:`        — resolver lookup, append to like_kinds[]
 *   `嫌い:`            — orphan match (engine matches the key, falls
 *                         through without storing — see quirks doc)
 *   `好き属性:`        — OR up to 10 4-byte tokens through both
 *                         attribute helpers
 *   `file:`            — copy up to 0x100 chars into file_path
 *   `活動時間:`        — OR bits 1/2/4/8 for 朝/昼/夕/夜 (max 4 tokens)
 *   `疑:`              — atoi → suspicion
 *   `騙:`              — atoi → gullibility
 *   `上昇１:`          — atoi → rise1
 *   `上昇２:`          — atoi → rise2
 *   `初回:`            — atoi → initial
 *   `ランダム:`        — atoi → random
 *
 * `resolve` may be NULL — in that case every `好き種類:` line records
 * a like-kind id of -1.
 *
 * `user` is passed through to every `resolve(name, user)` call.
 */
void tables_parse_kyaku(const unsigned char *data, size_t size,
                        kyaku_state_t *out,
                        kyaku_resolve_fn resolve, void *user);

#endif /* OPENRECET_TABLES_KYAKU_H */
