/*
 * tables.c — gameplay-table loader skeleton.
 *
 * Mirrors FUN_00475270 ("init indexfile ok"). Each load_X stub exercises
 * the storage subsystem (size + read) for one of the 14 fixed asset
 * files but does no parsing yet — that lands one commit per file in the
 * Phase B work tracked by docs/PROGRESS.md.
 *
 * Each stub logs `tables: <path> — <N> bytes (parser stub)` to stderr
 * on success, or `tables: <path> — missing` on a storage lookup miss.
 * This makes a partial port observable in the boot trace, and gives the
 * test harness an early signal if a fixture file goes missing under
 * vendor/original/.
 *
 * Engine behavior on missing files is to MessageBoxA and continue, so
 * we likewise continue past failures here — the game will run
 * (possibly badly) with empty tables.
 */

#include "tables.h"

#include <stdio.h>
#include <stdlib.h>

#include "storage.h"
#include "tables_buysell.h"
#include "tables_chara.h"
#include "tables_config.h"
#include "tables_model.h"
#include "tables_oder.h"

/* Load one file via storage_read and report the size. Returns the
 * size, or 0 on miss / OOM. Output buffer is owned by the caller and
 * must be free()'d when no longer needed.                              */
static size_t load_via_storage(const char *path, unsigned char **out_buf)
{
    *out_buf = NULL;

    size_t sz = storage_get_size(path);
    if (sz == 0) {
        fprintf(stderr, "tables: %s — missing\n", path);
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc(sz);
    if (!buf) {
        fprintf(stderr, "tables: %s — OOM (%zu bytes)\n", path, sz);
        return 0;
    }

    size_t got = storage_read(path, buf);
    if (got == 0) {
        fprintf(stderr, "tables: %s — read failed\n", path);
        free(buf);
        return 0;
    }

    *out_buf = buf;
    return got;
}

/* Each loader stub does: storage size+read, log, free. The real
 * parsers will replace the printf with the per-file parse loop. */
#define DEFINE_STUB(fnname, path_literal)                                 \
    static void fnname(void)                                              \
    {                                                                     \
        unsigned char *buf;                                               \
        size_t sz = load_via_storage(path_literal, &buf);                 \
        if (sz != 0) {                                                    \
            fprintf(stderr, "tables: %s — %zu bytes (parser stub)\n",     \
                    path_literal, sz);                                    \
            free(buf);                                                    \
        }                                                                 \
    }

DEFINE_STUB(load_stage_idx,     "idx/stage.idx")
/* config.idx — ported. Real parser in src/tables_config.c. The engine
 * has two interned copies of the path; the get_size site uses bare
 * "config.idx" (storage miss → malloc(10) silently undersized!) and
 * the read site uses "data/config.idx". We use the read-side spelling
 * for both, sidestepping the 940-byte overrun. See
 * docs/findings/tables-loader.md. */
static void load_config_idx(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/config.idx", &buf);
    if (sz == 0) return;
    tables_parse_config(buf, sz, &g_config);
    fprintf(stderr,
            "tables: data/config.idx — %zu bytes "
            "(kanjioff=%d edgewi=%d edgedel=%d effectmode=%d font=%s)\n",
            sz, g_config.kanjioff, g_config.edgewi, g_config.edgedel,
            g_config.effectmode,
            g_config.font_set ? g_config.font_name : "(default)");
    free(buf);
}
DEFINE_STUB(load_item_txt,      "data/item.txt")
DEFINE_STUB(load_kyaku_txt,     "data/kyaku.txt")
DEFINE_STUB(load_enemy_txt,     "data/enemy.txt")
/* chara.txt — ported. Real parser in src/tables_chara.c. Two parser
 * sub-blocks share the same 8 records: "000:".."007:" populates base
 * stats (10 CSV fields, 7 ints + 3 floats); "100:".."107:" populates
 * the level-100 endpoints (6 CSV ints, permuted into the upper half
 * of the record). Engine's parse loop iterates 10× per sub-block —
 * a 2-record overrun bug that would clobber the adjacent g_models
 * globals; port caps matching at CHARA_COUNT. */
static void load_chara_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/chara.txt", &buf);
    if (sz == 0) return;
    tables_parse_chara(buf, sz, g_chara);
    /* Default level_threshold is 1 (engine init); a parsed record
     * stores (file_field1 - 1). Every vendor adventurer has file_lv
     * ∈ {1, 8, 10, 15, 20, 30} → stored ∈ {0, 7, 9, 14, 19, 29},
     * none of which equal 1, so this is a clean parse marker. */
    int defined = 0, lv100 = 0;
    for (int i = 0; i < CHARA_COUNT; i++) {
        if (g_chara[i].level_threshold != 1) defined++;
        if (g_chara[i].hp_lv100 != 0) lv100++;
    }
    fprintf(stderr,
            "tables: data/chara.txt — %zu bytes "
            "(adventurers=%d lv100=%d)\n",
            sz, defined, lv100);
    free(buf);
}
/* buysell.txt — ported. Real parser in src/tables_buysell.c. */
static void load_buysell_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/buysell.txt", &buf);
    if (sz == 0) return;
    tables_parse_buysell(buf, sz, &g_buysell);
    fprintf(stderr,
            "tables: data/buysell.txt — %zu bytes "
            "(debug=%d kyaku=%d kind=%d)\n",
            sz, g_buysell.debug_mode,
            g_buysell.kyaku_number, g_buysell.kind);
    free(buf);
}
/* oder.txt — ported. Real parser in src/tables_oder.c. The engine's
 * fallback name-table lookup (DAT_0963e5f8, populated by item.txt) is
 * intentionally skipped here — see docs/formats/data-text.md. */
static void load_oder_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/oder.txt", &buf);
    if (sz == 0) return;
    tables_parse_oder(buf, sz, &g_oder);
    int max_lv = 0;
    for (int i = 0; i < g_oder.count; i++) {
        int lv = g_oder.entries[i].level_minus_1 + 1;
        if (lv > max_lv) max_lv = lv;
    }
    fprintf(stderr,
            "tables: data/oder.txt — %zu bytes "
            "(orders=%d max_lv=%d)\n",
            sz, g_oder.count, max_lv);
    free(buf);
}
/* model.txt — ported. Real parser in src/tables_model.c. */
static void load_model_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/model.txt", &buf);
    if (sz == 0) return;
    tables_parse_model(buf, sz, g_models);
    int defined = 0, max_points = 0;
    for (int i = 0; i < MODEL_DEF_COUNT; i++) {
        if (g_models[i].count > 0) {
            defined++;
            if ((int)g_models[i].count > max_points)
                max_points = (int)g_models[i].count;
        }
    }
    fprintf(stderr,
            "tables: data/model.txt — %zu bytes "
            "(models=%d max_points=%d)\n",
            sz, defined, max_points);
    free(buf);
}
DEFINE_STUB(load_event_txt,     "data/event.txt")
DEFINE_STUB(load_news_txt,      "data/news.txt")
DEFINE_STUB(load_snews_txt,     "data/snews.txt")
DEFINE_STUB(load_gousei_txt,    "data/gousei.txt")
DEFINE_STUB(load_enemylist_txt, "data/enemylist.txt")

/* Tutorial files: the engine loops over data/tuto_%d.txt starting at
 * index 1 and stops at the first missing file (we picked 1..32 as a
 * generous cap; vendor/original ships tuto_1..tuto_3). When a parser
 * lands here it'll need the index too — for now just log presence. */
static void load_tuto_loop(void)
{
    char path[64];
    for (int i = 1; i <= 32; i++) {
        /* Engine format string at 0x005cb38c is "data/tuto%d.txt"
         * (no underscore) — vendor ships tuto1.txt, tuto2.txt, tuto3.txt. */
        snprintf(path, sizeof path, "data/tuto%d.txt", i);
        unsigned char *buf;
        size_t sz = load_via_storage(path, &buf);
        if (sz == 0) {
            /* First miss ends the loop — matches the engine pattern of
             * stopping when storage_get_size returns 0. */
            break;
        }
        fprintf(stderr, "tables: %s — %zu bytes (parser stub)\n", path, sz);
        free(buf);
    }
}

void tables_load_all(void)
{
    /* Order matches FUN_00475270 exactly. Some later loaders may
     * depend on globals populated by earlier ones (not yet confirmed
     * per-file — flagged for verification during Phase B). */
    load_stage_idx();
    load_config_idx();
    load_item_txt();
    load_kyaku_txt();
    load_enemy_txt();
    load_chara_txt();
    load_buysell_txt();
    load_oder_txt();
    load_model_txt();
    load_event_txt();
    load_news_txt();
    load_snews_txt();
    load_gousei_txt();
    load_enemylist_txt();
    load_tuto_loop();
}
