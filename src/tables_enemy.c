/*
 * tables_enemy.c — `data/enemy.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #5 in
 * docs/decompiled/by-address/475270.c (L834..L1026). Identifies the
 * file via `s_data_enemy_txt_005cae2c` (size) and
 * `s_data_enemy_txt_005cae3c` (read); both interned copies hold the
 * same spelling "data/enemy.txt" (no path-mismatch quirk).
 *
 * Per-line shape (vendor file):
 *
 *     スライム緑     :15# 1# 25# 4# 0# 10# Slime Fluid#Worn Sword
 *
 * The 64 enemy records ship pre-baked in `.data` at `&DAT_005c23f0`
 * with their NAMES and a `flags` byte already populated; enemy.txt
 * supplies the runtime stats and drop-item references. The parser
 * therefore does not allocate or name records — it locates an
 * existing record by name and updates its fields in place.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - **Longest common prefix lookup.** The engine walks all 64
 *     records per line, tracks the longest `strlen(record->name)`
 *     that still matches the line's leading bytes, and writes into
 *     that record. So a line starting with "アーリマン緑" updates
 *     record `アーリマン緑` (12 bytes) rather than record `アーリマン`
 *     (10 bytes) even though both prefixes match — longest wins.
 *
 *   - **Sentinel `flags == 2` not present in vendor.** Engine break
 *     condition at L821 (`if (*(int *)(pcVar18 + 0x20) == 2) break`)
 *     never fires in shipping data; all 64 records are always walked.
 *     The port honours the sentinel for byte-for-byte fidelity, even
 *     though it's dead code.
 *
 *   - **Empty-name records skip the match.** Records 29, 31, 32, 33,
 *     56, 57, 58 ship with `name = " "` (single space, strlen 1).
 *     Lines whose first byte is ' ' are filtered earlier (L861), so
 *     the placeholder names never match a real data line.
 *
 *   - **Per-line drop reset.** Both drop slots are written -1 at the
 *     start of the per-line field loop (L925..L926) — so a line that
 *     omits the rare-drop column ends up with `drop_rare == -1`
 *     rather than the previous value.
 *
 *   - **State-machine field walker.** Outer loop iterates byte-by-byte
 *     up to ENEMY_LINE_CAP chars; `saw_delim` flag fires the per-
 *     field parse on the NEXT iteration after a delimiter (`:`, `,`,
 *     `;`, `#`). Field 0..5 are atoi'd directly from the cursor (no
 *     advance needed; atoi stops at the first non-digit). Fields 6/7
 *     copy bytes into a scratch buffer until \0/\r/\n/:/# or 32
 *     bytes, advancing the cursor past the field text — after which
 *     the outer loop's re-read of `*p` picks up the trailing
 *     delimiter and the cycle continues.
 *
 *   - **Float/runtime fields untouched.** The seven floats at
 *     +0x44..+0x5f (collision/sprite scaling, used by the runtime
 *     enemy spawner) are populated by OTHER engine code paths that
 *     aren't ported yet. enemy.txt does not write them; neither does
 *     the port.
 *
 *   - **Item-table dependency.** Drop-name resolution requires the
 *     item.txt table at `&DAT_095d381a` (stride 0x2cc, count at
 *     `_DAT_005c80ac`) to be populated. item.txt loads earlier in
 *     `tables_load_all` (block #3 vs #5) so the engine's lookup
 *     succeeds; the port's item.txt parser is not yet implemented,
 *     so drop_common / drop_rare resolve to -1. See `lookup_item_id`
 *     in this file for the future hook.
 *
 * Safety divergences (documented, not present in the engine):
 *
 *   - **No MessageBoxA on missing record.** Engine: if no record name
 *     matches the line's prefix, MessageBoxA pops up "{name} no_match".
 *     Port: silently skips the line.
 *
 *   - **No MessageBoxA on missing drop.** Engine: if a drop name
 *     doesn't appear in the item.txt table, MessageBoxA pops up
 *     "{drop} no_match". Port: stores -1.
 *
 *   - **Line buffer bounded.** Port collects up to 511 bytes per line
 *     into a stack buffer. The engine writes into a 0x25C-byte
 *     local + has no length cap on the inner read; the outer-line
 *     0x100 char cap on the field walker is what eventually bounds
 *     processing. Vendor lines are under 80 bytes.
 */

#include "tables_enemy.h"

#include <stdlib.h>
#include <string.h>

enemy_record_t g_enemy[ENEMY_COUNT];

/* ── Pre-baked names + boss flags ─────────────────────────────────────
 *
 * Extracted from vendor/unpacked/recettear.unpacked.exe at file offset
 * 0x1c0bf0 (engine address `&DAT_005c23f0`), records 0..63 × 0x68
 * stride. Names are SJIS bytes; we keep them as octal/hex escapes so
 * the source file stays pure ASCII regardless of editor settings.
 *
 * `flags` is the int32 at +0x20 of each record. `0` = normal, `1` =
 * boss-class. Records 29/31/32/33/56/57/58 ship as " " placeholders.
 */
struct enemy_pre_baked {
    const char *name_bytes;  /* NUL-terminated; embeds raw SJIS bytes */
    int         name_len;    /* explicit length (in case of embedded NULs) */
    int32_t     flags;
};

static const struct enemy_pre_baked k_pre_baked[ENEMY_COUNT] = {
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x97\xce",                                  10, 0 }, /*  0 スライム緑 */
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x90\xd4",                                  10, 0 }, /*  1 スライム赤 */
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x90\xc2",                                  10, 0 }, /*  2 スライム青 */
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x89\xa9",                                  10, 0 }, /*  3 スライム黄 */
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x83\x73\x83\x93\x83\x4e",                  14, 0 }, /*  4 スライムピンク */
    { "\x83\x58\x83\x89\x83\x43\x83\x80\x83\x81\x83\x5e\x83\x8b",                  14, 0 }, /*  5 スライムメタル */
    { "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93",                                  10, 0 }, /*  6 アーリマン */
    { "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93\x97\xce",                          12, 0 }, /*  7 アーリマン緑 */
    { "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93\x90\xc2",                          12, 0 }, /*  8 アーリマン青 */
    { "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93\x90\xd4",                          12, 0 }, /*  9 アーリマン赤 */
    { "\x83\x4c\x83\x89\x81\x5b\x83\x72\x81\x5b",                                  10, 0 }, /* 10 キラービー */
    { "\x83\x4c\x83\x89\x81\x5b\x83\x72\x81\x5b\x97\xce",                          12, 0 }, /* 11 キラービー緑 */
    { "\x83\x4c\x83\x89\x81\x5b\x83\x72\x81\x5b\x90\xd4",                          12, 0 }, /* 12 キラービー赤 */
    { "\x83\x4c\x83\x6d\x83\x52",                                                   6, 0 }, /* 13 キノコ */
    { "\x83\x4c\x83\x6d\x83\x52\x8e\x87",                                           8, 0 }, /* 14 キノコ紫 */
    { "\x82\xa9\x82\xda\x82\xbf\x82\xe1",                                           8, 0 }, /* 15 かぼちゃ */
    { "\x83\x52\x83\x7b\x83\x8b\x83\x68",                                           8, 0 }, /* 16 コボルド */
    { "\x83\x8d\x81\x5b\x83\x70\x81\x5b\x97\xce",                                  10, 0 }, /* 17 ローパー緑 */
    { "\x83\x8d\x81\x5b\x83\x70\x81\x5b\x90\xd4",                                  10, 0 }, /* 18 ローパー赤 */
    { "\x83\x8d\x81\x5b\x83\x70\x81\x5b\x90\xc2",                                  10, 0 }, /* 19 ローパー青 */
    { "\x83\x8d\x81\x5b\x83\x70\x81\x5b\x89\xa9",                                  10, 0 }, /* 20 ローパー黄 */
    { "\x83\x8d\x81\x5b\x83\x70\x81\x5b\x83\x73\x83\x93\x83\x4e",                  14, 0 }, /* 21 ローパーピンク */
    { "\x93\x65",                                                                   2, 0 }, /* 22 兎 */
    { "\x83\x4e\x83\x89\x83\x45\x83\x93\x83\x58\x83\x89\x83\x43\x83\x80",          16, 0 }, /* 23 クラウンスライム */
    { "\x82\xcb\x82\xb8\x82\xdd\x83\x6f\x81\x5b\x83\x8b",                          12, 1 }, /* 24 ねずみバール (boss) */
    { "\x83\x56\x83\x83\x83\x8b\x83\x80",                                           8, 0 }, /* 25 シャルム */
    { "\x8a\xe2\x82\xc6\x83\x7d\x83\x4f\x83\x8d",                                  10, 0 }, /* 26 岩とマグロ */
    { "\x83\x65\x83\x42\x83\x47\x81\x5b\x83\x8b",                                  10, 0 }, /* 27 ティエール */
    { "\x83\x4f\x83\x8a\x83\x74",                                                   6, 0 }, /* 28 グリフ */
    { "\x20",                                                                       1, 0 }, /* 29 (placeholder) */
    { "\x83\x41\x83\x8b\x83\x47\x83\x62\x83\x67",                                  10, 0 }, /* 30 アルエット */
    { "\x20",                                                                       1, 0 }, /* 31 (placeholder) */
    { "\x20",                                                                       1, 0 }, /* 32 (placeholder) */
    { "\x20",                                                                       1, 0 }, /* 33 (placeholder) */
    { "\x83\x45\x83\x42\x83\x58\x83\x76",                                           8, 0 }, /* 34 ウィスプ */
    { "\x83\x82\x83\x41\x83\x43\x8b\xe0",                                           8, 0 }, /* 35 モアイ金 */
    { "\x83\x82\x83\x41\x83\x43\x8b\xe2",                                           8, 0 }, /* 36 モアイ銀 */
    { "\x83\x7b\x83\x80\x90\xd4",                                                   6, 0 }, /* 37 ボム赤 */
    { "\x83\x7b\x83\x80\x90\xc2",                                                   6, 0 }, /* 38 ボム青 */
    { "\x83\x7b\x83\x80\x97\xce",                                                   6, 0 }, /* 39 ボム緑 */
    { "\x83\x7b\x83\x80\x89\xa9",                                                   6, 0 }, /* 40 ボム黄 */
    { "\x83\x41\x83\x8b\x83\x7d",                                                   6, 0 }, /* 41 アルマ */
    { "\x83\x71\x83\x68\x83\x89",                                                   6, 0 }, /* 42 ヒドラ */
    { "\x90\x65\x95\x83",                                                           4, 0 }, /* 43 親父 */
    { "\x83\x75\x83\x8b\x81\x5b\x83\x69\x83\x43\x83\x67",                          12, 0 }, /* 44 ブルーナイト */
    { "\x83\x4f\x83\x8c\x81\x5b\x83\x69\x83\x43\x83\x67",                          12, 0 }, /* 45 グレーナイト */
    { "\x83\x53\x81\x5b\x83\x8b\x83\x68\x83\x69\x83\x43\x83\x67",                  14, 0 }, /* 46 ゴールドナイト */
    { "\x83\x4f\x83\x8a\x81\x5b\x83\x93\x83\x69\x83\x43\x83\x67",                  14, 0 }, /* 47 グリーンナイト */
    { "\x83\x8c\x83\x62\x83\x68\x83\x69\x83\x43\x83\x67",                          12, 0 }, /* 48 レッドナイト */
    { "\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x69\x83\x43\x83\x67",                  14, 0 }, /* 49 ブラックナイト */
    { "\x83\x58\x83\x50\x83\x8b\x83\x67\x83\x93",                                  10, 0 }, /* 50 スケルトン */
    { "\x83\x58\x83\x50\x83\x8b\x83\x67\x83\x93\x8b\xe0",                          12, 0 }, /* 51 スケルトン金 */
    { "\x83\x53\x81\x5b\x83\x58\x83\x67\x82\x6e",                                  10, 0 }, /* 52 ゴーストＯ */
    { "\x83\x53\x81\x5b\x83\x58\x83\x67\x82\x6d",                                  10, 0 }, /* 53 ゴーストＮ */
    { "\x83\x7b\x83\x58\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93",                  14, 0 }, /* 54 ボスアーリマン */
    { "\x82\xa9\x82\xda\x82\xbf\x82\xe1\x83\x53\x81\x5b\x83\x58\x83\x67",          16, 0 }, /* 55 かぼちゃゴースト */
    { "\x20",                                                                       1, 0 }, /* 56 (placeholder) */
    { "\x20",                                                                       1, 0 }, /* 57 (placeholder) */
    { "\x20",                                                                       1, 0 }, /* 58 (placeholder) */
    { "\x82\xcb\x82\xb8\x82\xdd\x83\x6e\x83\x8a\x83\x5a\x83\x93",                  14, 1 }, /* 59 ねずみハリセン (boss) */
    { "\x82\xcb\x82\xb8\x82\xdd\x83\x7d\x83\x4f\x83\x8d",                          12, 1 }, /* 60 ねずみマグロ (boss) */
    { "\x83\x53\x81\x5b\x83\x8c\x83\x80",                                           8, 1 }, /* 61 ゴーレム (boss) */
    { "\x83\x53\x81\x5b\x83\x8c\x83\x80\x89\x45",                                  10, 1 }, /* 62 ゴーレム右 (boss) */
    { "\x83\x53\x81\x5b\x83\x8c\x83\x80\x8d\xb6",                                  10, 1 }, /* 63 ゴーレム左 (boss) */
};

void tables_enemy_init(enemy_record_t records[ENEMY_COUNT])
{
    memset(records, 0, sizeof(enemy_record_t) * ENEMY_COUNT);
    for (int i = 0; i < ENEMY_COUNT; i++) {
        int len = k_pre_baked[i].name_len;
        if (len > ENEMY_NAME_LEN) len = ENEMY_NAME_LEN;
        memcpy(records[i].name, k_pre_baked[i].name_bytes, (size_t)len);
        /* Trailing bytes already zero from memset; serves as the NUL
         * terminator for strlen/strcmp use. */
        records[i].flags = k_pre_baked[i].flags;
    }
}

/* Future hook: when item.txt lands, this becomes a lookup against the
 * item-name table at `&DAT_095d381a`. Until then it unconditionally
 * returns -1, mirroring "name not found" without the engine's
 * MessageBoxA. Engine reference: L975..L1008 of FUN_00475270. */
static int32_t lookup_item_id(const char *name)
{
    (void)name;
    return -1;
}

/* Longest-common-prefix record finder. Returns the index of the
 * record whose `name` is the longest prefix of `line`, or -1 if no
 * record's name matches. Empty-name records (strlen 0) are skipped.
 * Records with `flags == 2` terminate the scan (engine sentinel;
 * never used in shipping data). Mirrors L867..L918. */
static int find_record_by_name(const enemy_record_t records[ENEMY_COUNT],
                               const char *line)
{
    int best_idx = -1;
    size_t best_len = 0;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        if (records[i].flags == 2) break;
        size_t nlen = strlen(records[i].name);
        if (nlen == 0) continue;
        if (memcmp(line, records[i].name, nlen) != 0) continue;
        if (nlen > best_len) {
            best_len = nlen;
            best_idx = i;
        }
    }
    return best_idx;
}

/* Apply one parsed data line to its matched record. `line` is
 * NUL-terminated, `line[0..name_len)` is the record's name (already
 * matched by caller). Walks the line char-by-char, mirroring the
 * engine state machine at L931..L1024. */
static void apply_line(enemy_record_t *rec, const char *line)
{
    rec->drop_common = -1;
    rec->drop_rare   = -1;

    const char *p = line;
    int field_idx = 0;
    int saw_delim = 0;
    int chars = 0;

    while (chars < ENEMY_LINE_CAP) {
        char c = *p;
        if (c == '\0' || c == '\r' || c == '\n') break;

        if (saw_delim) {
            saw_delim = 0;
            switch (field_idx) {
            case 0: rec->hp         = atoi(p); break;
            case 1: rec->exp_reward = atoi(p); break;
            case 2: rec->at         = atoi(p); break;
            case 3: rec->df         = atoi(p); break;
            case 4: rec->ma         = atoi(p); break;
            case 5: rec->md         = atoi(p); break;
            case 6:
            case 7: {
                char drop_name[ENEMY_DROP_NAME_LEN];
                int k = 0;
                while (k < ENEMY_DROP_NAME_LEN - 1) {
                    char cc = *p;
                    if (cc == '\0' || cc == '\r' || cc == '\n'
                        || cc == ':'  || cc == '#') break;
                    drop_name[k++] = cc;
                    p++;
                }
                drop_name[k] = '\0';
                int32_t id = lookup_item_id(drop_name);
                if (field_idx == 6) rec->drop_common = id;
                else                rec->drop_rare   = id;
                break;
            }
            default:
                /* Engine has no case >= 8; falls through silently. */
                break;
            }
            field_idx++;
        }

        /* Re-read in case case 6/7 advanced the cursor onto a
         * delimiter. */
        c = *p;
        if (c == '#' || c == ':' || c == ',' || c == ';') saw_delim = 1;

        chars++;
        p++;
    }
}

void tables_parse_enemy(const unsigned char *data, size_t size,
                        enemy_record_t records[ENEMY_COUNT])
{
    /* Line buffer matches the engine's stack-local layout: the engine
     * reads into local_27c[0x20..], which can hold ~0x25c bytes of
     * line text. Port uses 512 bytes (vendor lines fit under 80). */
    char line[512];
    size_t pos = 0;

    while (pos < size) {
        if (data[pos] == '\0') break;

        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && data[pos] != '\r'
               && data[pos] != '\n'
               && llen + 1 < sizeof line) {
            line[llen++] = (char)data[pos++];
        }
        line[llen] = '\0';

        if (pos < size && (data[pos] == '\r' || data[pos] == '\n')) pos++;

        /* Skip blanks, comments, indented continuations. */
        if (llen == 0) continue;
        if (line[0] == '/' || line[0] == ' ') continue;

        int idx = find_record_by_name(records, line);
        if (idx < 0) {
            /* Engine: MessageBoxA "{name} no_match" via L920. Port
             * silently skips. */
            continue;
        }

        apply_line(&records[idx], line);
    }
}
