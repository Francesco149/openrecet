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
#include <string.h>

#include "storage.h"
#include "tables_buysell.h"
#include "tables_chara.h"
#include "tables_config.h"
#include "tables_enemy.h"
#include "tables_enemylist.h"
#include "tables_event.h"
#include "tables_gousei.h"
#include "tables_item.h"
#include "tables_kyaku.h"
#include "customer_dialogue.h"  /* per-kyaku fN.txt dialogue buffer (L1c) */
#include "scene_buy.h"      /* scene_buy_load_stage_files — cc08==4 standee names */
#include "tables_model.h"
#include "tables_news.h"
#include "tables_oder.h"
#include "tables_snews.h"
#include "tables_stage.h"
#include "tables_tuto.h"

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

/* Adapter that lets `tables_item_resolve` satisfy the
 * `(name, user) -> id` callback shape used by enemy.txt and gousei.txt.
 * `user` is the `item_state_t *` to probe. Engine reference: both
 * parsers inline the exact-name probe against the item.txt table; we
 * factor it out so the resolver lives in one place. */
static int32_t resolve_via_item_state(const char *name, void *user)
{
    const item_state_t *state = (const item_state_t *)user;
    return tables_item_resolve(state, name);
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

/* stage.idx — ported. Real parser in src/tables_stage.c. Defines the
 * 21 stage records (`stage:0-1`..`0-5` + `stage:1-1`..`1-16`); each is
 * a flat key:value bag for map geometry, camera, lighting, water,
 * weather, and fog/colour. See docs/formats/data-text.md for the
 * full key list and docs/findings/engine-quirks.md (#34..#36) for
 * the field-shared / fallback-ID / sunset-off-broken behaviour. */
static void load_stage_idx(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("idx/stage.idx", &buf);
    if (sz == 0) return;
    tables_parse_stage(buf, sz, &g_stage);
    int total_maps = 0, total_mapcameras = 0;
    int with_sunpos = 0, with_sunset = 0, with_moonpos = 0;
    for (int i = 0; i < g_stage.count; i++) {
        total_maps        += g_stage.records[i].map_count;
        total_mapcameras  += g_stage.records[i].mapcamera_count;
        if (g_stage.records[i].sunpos_mode == STAGE_SUN_SUNPOS) with_sunpos++;
        if (g_stage.records[i].sunpos_mode == STAGE_SUN_SUNSET) with_sunset++;
        if (g_stage.records[i].moonpos_set)                     with_moonpos++;
    }
    fprintf(stderr,
            "tables: idx/stage.idx — %zu bytes "
            "(stages=%d maps=%d mapcameras=%d "
            "sunpos=%d sunset=%d moonpos=%d)\n",
            sz, g_stage.count, total_maps, total_mapcameras,
            with_sunpos, with_sunset, with_moonpos);
    free(buf);
}
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
/* item.txt — ported. Real parser in src/tables_item.c. Master item
 * catalog: dispatcher fans lines into category headers (':' prefix,
 * via FUN_00491044) and item records (4-digit id prefix, via
 * FUN_004912de). Populates the per-category name globals at
 * &DAT_0963e5f8 / &DAT_0963c5f8 and the per-record table at
 * &DAT_095d37d0 (stride 0x2cc). Gating dependency for the deferred
 * resolvers in oder.txt / enemy.txt / gousei.txt — resolver wiring
 * lands in a follow-up commit. */
static void load_item_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/item.txt", &buf);
    if (sz == 0) return;
    tables_parse_item(buf, sz, &g_item);
    int max_id = 0, equippable = 0;
    int categories = 0;
    for (int i = 0; i < g_item.count; i++) {
        if (g_item.records[i].item_id > max_id)
            max_id = g_item.records[i].item_id;
        if (g_item.records[i].equip_class != 0) equippable++;
    }
    for (int c = 0; c < ITEM_CATEGORY_COUNT; c++) {
        if (g_item.categories[c].singular[0] != 0) categories++;
    }
    fprintf(stderr,
            "tables: data/item.txt — %zu bytes "
            "(items=%d max_id=%d equippable=%d cats=%d)\n",
            sz, g_item.count, max_id, equippable, categories);
    free(buf);
}
/* kyaku.txt — ported. Real parser in src/tables_kyaku.c. The
 * `好き種類:` lines resolve through `resolve_via_item_category` against
 * `g_item.categories[]` (populated by load_item_txt earlier in the
 * load order); resolver misses fall back to -1. */
static int32_t resolve_via_item_category(const char *name, void *user)
{
    const item_state_t *state = (const item_state_t *)user;
    for (int c = 0; c < ITEM_CATEGORY_COUNT; c++) {
        if (state->categories[c].singular[0] == '\0') continue;
        size_t nlen = strlen(state->categories[c].singular);
        if (memcmp(name, state->categories[c].singular, nlen) == 0
            && (name[nlen] == '\0' || name[nlen] == '\r'
                || name[nlen] == '\n')) {
            return (int32_t)c;
        }
    }
    return -1;
}
static void load_kyaku_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/kyaku.txt", &buf);
    if (sz == 0) return;
    tables_parse_kyaku(buf, sz, &g_kyaku,
                       resolve_via_item_category, &g_item);
    int defined = 0, total_likes = 0, with_budget = 0;
    for (int i = 0; i < KYAKU_COUNT; i++) {
        if (g_kyaku.records[i].active) {
            defined++;
            total_likes += g_kyaku.records[i].like_count;
            if (g_kyaku.records[i].budget_high > 0) with_budget++;
        }
    }
    fprintf(stderr,
            "tables: data/kyaku.txt — %zu bytes "
            "(customers=%d like_kinds=%d with_budget=%d)\n",
            sz, defined, total_likes, with_budget);
    free(buf);
}
/* Per-customer dialogue scripts (kyaku/<name>.txt) — the dialogue half of
 * FUN_00475270's per-record loop (all.c:74568-74715).  For every parsed
 * customer with a `file:` script, read it via storage and parse the msgNN:
 * lines into the per-record dialogue buffer the live haggle picker
 * (customer_service.c::cs_pick_line = FUN_00460a1a) reads.  Runs AFTER
 * load_kyaku_txt (needs records[i].active + file_path). */
static void load_kyaku_dialogue(void)
{
    kyaku_dialogue_free_all();
    int loaded = 0, lines = 0;
    for (int i = 0; i < KYAKU_COUNT; i++) {
        if (!g_kyaku.records[i].active)        continue;
        const char *path = g_kyaku.records[i].file_path;
        if (path[0] == '\0')                   continue;

        unsigned char *buf;
        size_t sz = load_via_storage(path, &buf);
        if (sz == 0)                           continue;   /* logged by helper */

        kyaku_dialogue_t *dlg = calloc(1, sizeof(*dlg));
        if (dlg) {
            kyaku_dialogue_parse((const char *)buf, sz, dlg);
            kyaku_dialogue_set(i, dlg);
            loaded++;
            for (int t = 0; t < KYAKU_DLG_TYPES; t++)
                lines += dlg->count[t];
        }
        free(buf);
    }
    fprintf(stderr, "tables: kyaku dialogue — %d scripts, %d lines\n",
            loaded, lines);
}
/* enemy.txt — ported. Real parser in src/tables_enemy.c. The 64
 * records ship pre-baked in .data with their NAMES; enemy.txt only
 * supplies stats + drop refs, which are matched into records by
 * longest-prefix name lookup. Drop-name → item-id resolution is wired
 * through `resolve_via_item_state` against `g_item` (populated by
 * load_item_txt earlier in the load order). */
static void load_enemy_txt(void)
{
    tables_enemy_init(g_enemy);
    unsigned char *buf;
    size_t sz = load_via_storage("data/enemy.txt", &buf);
    if (sz == 0) return;
    tables_parse_enemy(buf, sz, g_enemy, resolve_via_item_state, &g_item);
    /* Parsed marker: at-least-one of {hp, at, md} non-zero. Covers
     * outlier vendor rows like `岩とマグロ:0#0#20#0#0#0` (only AT)
     * and `ゴーストＯ:20#25#0#16#20#10` (zero AT but non-zero HP/MD). */
    int parsed = 0, bosses = 0, drops_resolved = 0;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        if (g_enemy[i].hp != 0 || g_enemy[i].at != 0
            || g_enemy[i].md != 0) parsed++;
        if (g_enemy[i].flags == 1) bosses++;
        if (g_enemy[i].drop_common >= 0) drops_resolved++;
        if (g_enemy[i].drop_rare   >= 0) drops_resolved++;
    }
    fprintf(stderr,
            "tables: data/enemy.txt — %zu bytes "
            "(enemies=%d bosses=%d drops_resolved=%d)\n",
            sz, parsed, bosses, drops_resolved);
    free(buf);
}
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
/* event.txt — ported. Real parser in src/tables_event.c. Four-way
 * location dispatch (広場/市場/教会/酒場) into 100-slot record arrays;
 * each line is id-flag : 4 hex prereqs : weekday tags : loop_min :
 * day-range pairs. Pre-seeds record 0 of 広場 with a hard-coded default
 * event (id=0x0b, prereq[0]=0xa3) so 広場 starts with count=1. */
static void load_event_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/event.txt", &buf);
    if (sz == 0) return;
    tables_parse_event(buf, sz, &g_event);
    int with_prereqs = 0;
    for (int c = 0; c < EVENT_CATEGORY_COUNT; c++) {
        for (int i = 0; i < g_event.counts[c]; i++) {
            if (g_event.records[c][i].prereq[0] >= 0) with_prereqs++;
        }
    }
    fprintf(stderr,
            "tables: data/event.txt — %zu bytes "
            "(hiroba=%d ichiba=%d kyokai=%d sakaba=%d with_prereqs=%d)\n",
            sz,
            g_event.counts[EVENT_CAT_HIROBA],
            g_event.counts[EVENT_CAT_ICHIBA],
            g_event.counts[EVENT_CAT_KYOKAI],
            g_event.counts[EVENT_CAT_SAKABA],
            with_prereqs);
    free(buf);
}
/* news.txt — ported. Real parser in src/tables_news.c. Resolves the
 * `<name>` field on data rows through two adapters against `g_item`:
 *   `news_resolve_category` — prefix-match name vs categories[i].singular
 *   `news_resolve_item`     — prefix-match name vs records[j].singular,
 *                              returns item_id (not slot).
 * Both mirror the engine's `FUN_00479f4d(name, candidate, name_len)`
 * prefix-by-length compare; see tables_news.c quirk #28 for why this
 * differs from exact-match. */
static int32_t news_resolve_category(const char *name, size_t name_len,
                                     void *user)
{
    const item_state_t *state = (const item_state_t *)user;
    for (int c = 0; c < ITEM_CATEGORY_COUNT; c++) {
        const char *cand = state->categories[c].singular;
        if (cand[0] == '\0') continue;
        size_t clen = strlen(cand);
        if (clen >= name_len && memcmp(name, cand, name_len) == 0) {
            return (int32_t)c;
        }
    }
    return -1;
}
static int32_t news_resolve_item(const char *name, size_t name_len,
                                 void *user)
{
    const item_state_t *state = (const item_state_t *)user;
    for (int i = 0; i < state->count; i++) {
        const char *cand = state->records[i].singular;
        if (cand[0] == '\0') continue;
        size_t clen = strlen(cand);
        if (clen >= name_len && memcmp(name, cand, name_len) == 0) {
            return state->records[i].item_id;
        }
    }
    return -1;
}
static void load_news_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/news.txt", &buf);
    if (sz == 0) return;
    tables_parse_news(buf, sz, &g_news,
                      news_resolve_category, news_resolve_item, &g_item);
    int dash_rows = 0, special_rows = 0, attr_hits = 0, cat_hits = 0, item_hits = 0;
    for (int i = 0; i < g_news.count; i++) {
        const news_record_t *r = &g_news.records[i];
        if (r->category == NEWS_CATEGORY_DASH)            dash_rows++;
        else if (r->attr_mask == -1)                      special_rows++;
        else if (r->attr_mask != 0)                       attr_hits++;
        else if (r->category >= 0)                        cat_hits++;
        else if (r->item_id >= 0)                         item_hits++;
    }
    fprintf(stderr,
            "tables: data/news.txt — %zu bytes "
            "(news=%d dash=%d special=%d attr=%d category=%d item=%d)\n",
            sz, g_news.count, dash_rows, special_rows, attr_hits, cat_hits, item_hits);
    free(buf);
}
/* snews.txt — ported. Real parser in src/tables_snews.c. Two unrelated
 * globals: a 64-slot name table and a 10×30 grid of floor-range
 * sections (only 6×N reachable via the SJIS dungeon keys). */
static void load_snews_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/snews.txt", &buf);
    if (sz == 0) return;
    tables_parse_snews(buf, sz, &g_snews);
    /* Count populated names and sections for the boot trace. */
    int names = 0;
    for (int i = 0; i < SNEWS_NAME_COUNT; i++) {
        if (g_snews.names[i].active) names++;
    }
    int sections = 0;
    for (int d = 0; d < SNEWS_DUNGEON_KEY_COUNT; d++) {
        for (int s = 0; s < SNEWS_SECTION_COUNT; s++) {
            if (g_snews.sections[d][s].floor_start != -1) sections++;
        }
    }
    fprintf(stderr,
            "tables: data/snews.txt — %zu bytes "
            "(names=%d sections=%d)\n",
            sz, names, sections);
    free(buf);
}
/* gousei.txt — ported. Real parser in src/tables_gousei.c. Item-name
 * → item-id resolution is wired through `resolve_via_item_state`
 * against `g_item` (populated by load_item_txt earlier in the load
 * order); resolver misses fall back to -1. */
static void load_gousei_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/gousei.txt", &buf);
    if (sz == 0) return;
    tables_parse_gousei(buf, sz, &g_gousei,
                        resolve_via_item_state, &g_item);
    int max_rank = 0, outputs_resolved = 0, ingredients_resolved = 0;
    for (int i = 0; i < g_gousei.count; i++) {
        if (g_gousei.records[i].rank > max_rank) {
            max_rank = g_gousei.records[i].rank;
        }
        if (g_gousei.records[i].output_id >= 0) outputs_resolved++;
        for (int k = 0; k < GOUSEI_INGREDIENT_COUNT; k++) {
            if (g_gousei.records[i].ingredient_id[k] >= 0) {
                ingredients_resolved++;
            }
        }
    }
    fprintf(stderr,
            "tables: data/gousei.txt — %zu bytes "
            "(recipes=%d max_rank=%d outputs_resolved=%d "
            "ingredients_resolved=%d)\n",
            sz, g_gousei.count, max_rank,
            outputs_resolved, ingredients_resolved);
    free(buf);
}
/* enemylist.txt — ported. Real parser in src/tables_enemylist.c.
 * Two engine globals are populated: a 10×60 grid of 752-byte floor
 * sections and a 10-slot wisp drop table. Cross-table dependencies:
 *   - `g_enemy` (populated by load_enemy_txt) supplies the pre-baked
 *     enemy-name table for the per-line longest-prefix lookup.
 *   - `g_item` (populated by load_item_txt) supplies the item table
 *     for `wispN:` and per-enemy drop name → item id resolution.
 * Both load earlier in the dispatch order, so the resolver is wired
 * unconditionally here. */
static void load_enemylist_txt(void)
{
    unsigned char *buf;
    size_t sz = load_via_storage("data/enemylist.txt", &buf);
    if (sz == 0) return;
    tables_parse_enemylist(buf, sz, &g_enemylist,
                           g_enemy, ENEMY_COUNT,
                           resolve_via_item_state, &g_item);

    int total_sections = 0;
    int total_enemies  = 0;
    int total_drops    = 0;
    int drops_resolved = 0;
    int wisps          = 0;
    int wisps_resolved = 0;
    for (int d = 0; d < ENEMYLIST_DUNGEON_SLOTS; d++) {
        total_sections += g_enemylist.section_counts[d];
        for (int s = 0; s < g_enemylist.section_counts[d]; s++) {
            const enemylist_section_t *sec = &g_enemylist.sections[d][s];
            for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; k++) {
                if (sec->enemies[k].enemy_id < 0) continue;
                total_enemies++;
                for (int j = 0; j < ENEMYLIST_DROPS_PER_ENEMY; j++) {
                    int32_t id = sec->drops[k].item_id[j];
                    if (id != -1) {
                        total_drops++;
                        if (id >= 0) drops_resolved++;
                    }
                }
            }
        }
    }
    for (int w = 0; w < ENEMYLIST_WISP_SLOTS; w++) {
        if (g_enemylist.wisp_drops[w] != -1) {
            wisps++;
            if (g_enemylist.wisp_drops[w] >= 0) wisps_resolved++;
        }
    }

    fprintf(stderr,
            "tables: data/enemylist.txt — %zu bytes "
            "(sections=%d enemies=%d drops=%d resolved=%d "
            "wisps=%d wisp_resolved=%d)\n",
            sz, total_sections, total_enemies,
            total_drops, drops_resolved,
            wisps, wisps_resolved);
    free(buf);
}

/* tuto1.txt / tuto2.txt / tuto3.txt — ported. Real parser in
 * src/tables_tuto.c. The engine hard-codes the loop to three files
 * (local_38 == 3 → return) using format string "data/tuto%d.txt" at
 * 0x005cb38c — no early-exit on miss. We mirror that fixed-count
 * approach. */
static void load_tuto_loop(void)
{
    char path[64];
    int total = 0;
    int overflow = 0;
    for (int i = 1; i <= TUTO_FILE_COUNT; i++) {
        snprintf(path, sizeof path, "data/tuto%d.txt", i);
        unsigned char *buf;
        size_t sz = load_via_storage(path, &buf);
        if (sz == 0) continue;

        int n = tables_parse_tuto(i - 1, buf, sz, g_tuto);
        total += n;
        if (n > TUTO_PARSER_STRIDE) overflow++;
        fprintf(stderr, "tables: %s — %zu bytes (records=%d%s)\n",
                path, sz, n,
                n > TUTO_PARSER_STRIDE ? " ⚠ overflows 200-slot region" : "");
        free(buf);
    }
    if (overflow > 0) {
        fprintf(stderr,
                "tables: tuto overflow — %d/%d files exceed the 200-record "
                "per-file region (would collide into the next file)\n",
                overflow, TUTO_FILE_COUNT);
    }
    (void)total;
}

/* Per-stage character-sprite NAME driver (FUN_00475270 block #4): for each
 * defined customer, read its `file:` data file via storage and parse the
 * `grpNN:` standee lines into the scene_buy name table (the cc08==4 character
 * art).  Sets g_scene_buy_valid for every defined customer (engine: the flag is
 * raised by the kyaku.txt parse).  The pure parse lives in scene_buy.c. */
static void load_stage_files(void)
{
    for (int rec = 0; rec < KYAKU_COUNT && rec < SCENE_BUY_PAGE_COUNT; rec++) {
        const kyaku_record_t *kr = &g_kyaku.records[rec];
        if (!kr->active) continue;
        g_scene_buy_valid[rec] = 1;
        if (kr->file_path[0] == '\0') continue;

        unsigned char *buf = NULL;
        size_t got = load_via_storage(kr->file_path, &buf);
        if (got == 0) continue;            /* missing file → no standees (no-op) */
        scene_buy_parse_stage_buffer(rec, (const char *)buf, got);
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
    load_kyaku_dialogue();   /* per-kyaku fN.txt dialogue (after kyaku.txt) */
    load_enemy_txt();
    load_chara_txt();
    /* Per-stage character-sprite NAME parse (FUN_00475270 block #4): after
     * kyaku.txt + chara.txt, before buysell.txt — populates the cc08==4
     * customer-service standee name table from each customer's `file:` data. */
    load_stage_files();
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
