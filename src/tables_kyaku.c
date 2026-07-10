/*
 * tables_kyaku.c — `data/kyaku.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #4 in
 * docs/decompiled/by-address/475270.c (L469..L832). Both interned
 * paths spell "data/kyaku.txt" — no path-mismatch quirk like
 * config.idx.
 *
 * Per-record engine stride is 0x2c670 bytes (most of it dialog-buffer
 * scratch for the customer's `file:` script content). The table loader
 * only writes the 18-or-so meaningful fields itemised in
 * tables_kyaku.h; the dialog content is wired in by an unrelated
 * subsystem we haven't ported yet. The port lifts those fields into a
 * flat struct and discards the dialog tail.
 *
 * Engine quirks faithfully reproduced (cross-ref docs/findings/
 * engine-quirks.md):
 *
 *   - **`嫌い:` is matched but discarded.** The parser does a 5-byte
 *     prefix match against `嫌い:` (DAT_005caddc) and... that's it —
 *     no field write, no atoi, no string copy. The match's body is
 *     literally empty; the do-while just `break`s on mismatch and
 *     falls through to the next key on a hit. Cost: 5 char-compares
 *     per non-comment line. Almost certainly a dialled-back feature.
 *
 *   - **Header singular/joint write-position reset.** On `NNN:S#P`,
 *     singular[] and joint[] both receive the bytes of `S` (via
 *     `puVar14[iVar6+4]` and `puVar14[iVar6+0x24]`, incrementing iVar6
 *     together). At '#', iVar6 resets to 0 — so subsequent bytes of
 *     `P` *overwrite* joint[0..] starting from the beginning, leaving
 *     singular[] frozen at the pre-'#' contents. Net result: singular
 *     holds the pre-'#' name, joint holds the post-'#' name (with any
 *     leftover singular suffix if plural is shorter — vendor data
 *     never triggers that).
 *
 *   - **Singular NUL at off-by-five.** On EOL detect inside the header
 *     loop, the engine writes `puVar14[iVar17 + 5] = 0` — that's
 *     singular[iVar17 + 1], NOT singular[iVar6 + 1]. For lines without
 *     '#', iVar17 == iVar6, so the NUL lands one past singular's last
 *     content byte (correct). For lines with '#', iVar17 has been
 *     incrementing past the '#' while iVar6 reset to 0, so the NUL
 *     lands several bytes past singular's end. Harmless because the
 *     record was memset'd to zero at boot (BSS) and the trailing bytes
 *     past singular's content are already NUL.
 *
 *   - **Header gated by leading '0'.** The line dispatcher only tries
 *     the 50-iteration `%03d:` match if the line's first byte is '0'.
 *     Records 0..50 all have IDs ≤ 50 so leading digit is always '0',
 *     but if records went past 99 the parser would silently ignore
 *     them. Vendor file goes 000..020 plus a commented-out 012.
 *
 *   - **`属性:` and `予算:` unbounded delimiter scans.** Once the
 *     first numeric is parsed, the engine walks forward looking for
 *     `,` (attr) or `-` (budget) with NO upper bound — past the line
 *     buffer if the delimiter is missing. Vendor data always has both,
 *     but the port stops at NUL too for safety.
 *
 *   - **`好き属性:` ORs both attribute-tag and class-tag helpers.**
 *     Each 4-byte chunk is fed to both FUN_0049e9a7 (16-tag SJIS
 *     attribute table) and FUN_0049eb2a (English category-name table).
 *     The English helper requires `name[tag_len] == '\0'` which is
 *     never true for a 4-byte SJIS slice, so in practice it always
 *     returns 0 for vendor data — but the OR is faithfully kept.
 *
 *   - **`活動時間:` matches exactly 4 tokens.** The do-while body
 *     OR's one of 4 bit positions per 2-byte SJIS token (朝/昼/夕/夜),
 *     advancing the cursor 2 bytes per iter, with a hard cap of 4
 *     iterations. Tokens not in the set (e.g. `本人` for Recette,
 *     `試` for Tear) match nothing and leave the mask at 0.
 *
 *   - **`好き種類:` cap of 20 per record.** Engine guards with
 *     `if (DAT_06a63c38[...] < 0x14)`; the 21st like-kind triggers a
 *     MessageBoxA "好き種類登録数オーバー" and is dropped. Vendor
 *     customer Alouette tops out at 11. Port also caps at 20.
 *
 *   - **`好き種類:` lookup MessageBox on miss.** Engine pops
 *     MessageBoxA "不明なアイテム1" + the unmatched name when a
 *     `好き種類:` line names a category that doesn't exist in the
 *     item.txt-populated table. Port logs to stderr.
 *
 * Safety divergences (documented, not present in the engine):
 *
 *   - **Line buffer bounded.** Port collects up to 511 bytes per line
 *     into a stack buffer. The engine writes into a 0x25c-byte local;
 *     vendor lines are under 80 bytes.
 *
 *   - **`属性:`/`予算:` delimiter scan bounded.** Port stops at the
 *     line's NUL terminator on missing `,`/`-`. Engine would walk on.
 *
 *   - **No MessageBoxA.** Out-of-bounds like-kind overflow and
 *     unknown-category errors go to stderr instead of a modal popup.
 */

#include "tables_kyaku.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

kyaku_state_t g_kyaku;

/* ── 4-byte SJIS attribute-tag table for FUN_0049e9a7 ──────────────────
 * Same 16 tags + bit assignments as oder.txt / item.txt; duplicated
 * here to keep the module's dependency surface narrow (oder/item also
 * duplicate it). Bit positions match those modules exactly. */
static const uint8_t KYAKU_ATTR_TAGS[16][4] = {
    /* 0x0001 */ { 0x95, 0x90, 0x8a, 0xed },  /* 武器  weapon                 */
    /* 0x0002 */ { 0x96, 0x68, 0x8b, 0xef },  /* 防具  armour                 */
    /* 0x0004 */ { 0x92, 0xb2, 0x93, 0x78 },  /* 調度  decor                  */
    /* 0x0008 */ { 0x95, 0x9e, 0x8f, 0xfc },  /* 服飾  clothing               */
    /* 0x0010 */ { 0x83, 0x41, 0x83, 0x4e },  /* アク  accessory              */
    /* 0x0020 */ { 0x8b, 0x4d, 0x8b, 0xe0 },  /* 貴金  precious metal         */
    /* 0x0040 */ { 0x8b, 0xe0, 0x91, 0xae },  /* 金属  metal                  */
    /* 0x0080 */ { 0x97, 0x5b, 0x94, 0xd1 },  /* 夕飯  dinner                 */
    /* 0x0100 */ { 0x8a, 0xc3, 0x82, 0xa2 },  /* 甘い  sweet                  */
    /* 0x0200 */ { 0x94, 0x68, 0x8e, 0xe8 },  /* 派手  fancy                  */
    /* 0x0400 */ { 0x92, 0x6e, 0x96, 0xa1 },  /* 地味  plain                  */
    /* 0x0800 */ { 0x92, 0xbf, 0x95, 0x69 },  /* 珍品  rare                   */
    /* 0x1000 */ { 0x96, 0x68, 0x8a, 0xa6 },  /* 防寒  cold-weather           */
    /* 0x2000 */ { 0x90, 0x48, 0x95, 0x69 },  /* 食品  food                   */
    /* 0x4000 */ { 0x90, 0xb9, 0x91, 0xae },  /* 聖属  holy                   */
    /* 0x8000 */ { 0x96, 0x82, 0x91, 0xae },  /* 魔属  sinister               */
};

static uint32_t kyaku_attr_4byte_mask(const char *p)
{
    uint32_t mask = 0;
    for (int i = 0; i < 16; i++) {
        if (memcmp(p, KYAKU_ATTR_TAGS[i], 4) == 0) {
            mask = 1u << i;
        }
    }
    return mask;
}

/* English category-name → broad-class bitmask table, same shape as
 * tables_item.c's ITEM_CLASS_TAGS. For vendor kyaku.txt data the
 * `好き属性:` line only contains SJIS 4-byte chunks, so the
 * `name[tag_len] == '\0'` guard never holds and this function always
 * returns 0. Kept for faithful engine reproduction. */
static const struct {
    const char *name;
    uint32_t    bits;
} KYAKU_CLASS_TAGS[] = {
    { "Swords",       0x00001 },
    { "Daggers",      0x00001 },
    { "Staves",       0x00001 },
    { "Bows",         0x00001 },
    { "Spears",       0x00001 },
    { "Gloves",       0x00001 },
    { "Claws",        0x00001 },
    { "Arm Parts",    0x00001 },
    { "Clothes",      0x00002 },
    { "Robes",        0x00002 },
    { "Breastplates", 0x00002 },
    { "Armor",        0x00002 },
    { "Shields",      0x00002 },
    { "Bracelets",    0x00002 },
    { "Helms",        0x00002 },
    { "Hats",         0x00002 },
    { "Flooring",     0x10000 },
    { "Wallpapers",   0x10000 },
    { "Counters",     0x10000 },
    { "Carpets",      0x10000 },
};

static uint32_t kyaku_class_bits(const char *category_name)
{
    uint32_t bits = 0;
    for (size_t i = 0;
         i < sizeof(KYAKU_CLASS_TAGS) / sizeof(*KYAKU_CLASS_TAGS); i++) {
        size_t tag_len = strlen(KYAKU_CLASS_TAGS[i].name);
        if (memcmp(category_name, KYAKU_CLASS_TAGS[i].name, tag_len) == 0
            && category_name[tag_len] == '\0') {
            bits |= KYAKU_CLASS_TAGS[i].bits;
        }
    }
    return bits;
}

/* ── 2-byte SJIS activity-time tokens ─────────────────────────────────
 * Bit positions match KYAKU_TIME_* in the header. Engine reference:
 * DAT_005cadf0/df4/df8/dfc (4 × 2-byte tokens). */
static const struct {
    uint8_t  bytes[2];
    uint32_t bit;
} KYAKU_TIME_TOKENS[4] = {
    { { 0x92, 0xa9 }, KYAKU_TIME_MORNING },  /* 朝 */
    { { 0x92, 0x8b }, KYAKU_TIME_NOON    },  /* 昼 */
    { { 0x97, 0x5b }, KYAKU_TIME_EVENING },  /* 夕 */
    { { 0x96, 0xe9 }, KYAKU_TIME_NIGHT   },  /* 夜 */
};

/* ── Per-key prefix match ──────────────────────────────────────────── */

/* Returns 1 if `line[0..n-1]` equals `key[0..n-1]` (n-byte memcmp).
 * Engine pattern: do-while comparing chars one at a time; we use
 * memcmp since the buffer is always NUL-terminated. */
static int match_prefix(const char *line, const char *key, size_t n)
{
    return memcmp(line, key, n) == 0 ? 1 : 0;
}

/* ── Header parse: "NNN:Singular#Plural" ──────────────────────────────
 * Returns the matched record id (0..49) or -1 if no header matched.
 * Writes singular/joint/active on hit. Mirrors L518..L552. */
static int parse_header(const char *line, kyaku_record_t records[KYAKU_COUNT])
{
    char id_key[5];
    for (int id = 0; id < KYAKU_COUNT; id++) {
        snprintf(id_key, sizeof id_key, "%03d:", id);
        if (memcmp(line, id_key, 4) != 0) continue;

        kyaku_record_t *r = &records[id];
        r->active = 1;

        /* Header body: copy line[4..] into singular[]/joint[] with the
         * iVar6-resets-at-'#' quirk. iVar17 = total chars consumed,
         * iVar6 = current write index, post_hash = 1 once '#' seen. */
        const char *p = line + 4;
        int iVar6 = 0, iVar17 = 0;
        int post_hash = 0;
        while (iVar17 < 0x20) {
            char c = *p;
            if (c == '#') {
                iVar6 = 0;
                post_hash = 1;
            } else {
                if (!post_hash && iVar6 < KYAKU_NAME_LEN - 1) {
                    r->singular[iVar6] = c;
                }
                if (iVar6 < KYAKU_NAME_LEN - 1) {
                    r->joint[iVar6] = c;
                }
                iVar6++;
                char nxt = p[1];
                if (nxt == '\0' || nxt == '\r' || nxt == '\n') {
                    /* Engine's puVar14[iVar17 + 5] = 0 — that's
                     * singular[iVar17 + 1]. Bound-check for the port. */
                    if (iVar17 + 1 < KYAKU_NAME_LEN) {
                        r->singular[iVar17 + 1] = '\0';
                    }
                    break;
                }
            }
            p++;
            iVar17++;
        }
        return id;
    }
    return -1;
}

/* ── Field key dispatch ───────────────────────────────────────────────
 * Each function below mirrors one field-key block in the engine. They
 * all take the current record + the line buffer; on a key mismatch
 * they return without writing.                                      */

static void apply_name_index(const char *line, kyaku_record_t *r)
{
    /* 9-byte key 名前番号: at DAT_005cad4c, atoi from offset 9. */
    static const char K[10] = "\x96\xbc\x91\x4f\x94\xd4\x8d\x86:";
    if (!match_prefix(line, K, 9)) return;
    r->name_index = atoi(line + 9);
}

static void apply_attr(const char *line, kyaku_record_t *r)
{
    /* 5-byte key 属性: at DAT_005cad58, atoi field 1, scan ',' atoi 2. */
    static const char K[6] = "\x91\xae\x90\xab:";
    if (!match_prefix(line, K, 5)) return;
    const char *p = line + 5;
    r->attr_x = atoi(p);
    /* Engine: skip if line ended right after ':' (no value). */
    char c = *p;
    if (c == '\0' || c == '\r' || c == '\n') return;
    while (*p != ',' && *p != '\0') p++;
    if (*p == ',') r->attr_y = atoi(p + 1);
}

static void apply_budget(const char *line, kyaku_record_t *r)
{
    /* 5-byte key 予算: at DAT_005cad70, atoi field 1, scan '-' atoi 2.
     * Engine logs "kyaku.txt ログエラー1%s\n" when value is empty
     * (sprintf to a local that's then thrown away); port silently
     * leaves both fields zero. */
    static const char K[6] = "\x97\x5c\x8e\x5a:";
    if (!match_prefix(line, K, 5)) return;
    const char *p = line + 5;
    char c = *p;
    if (c == '\0' || c == '\r' || c == '\n') return;
    r->budget_low = atoi(p);
    while (*p != '-' && *p != '\0') p++;
    if (*p == '-') r->budget_high = atoi(p + 1);
}

static void apply_like_kind(const char *line, kyaku_record_t *r,
                            kyaku_resolve_fn resolve, void *user)
{
    /* 9-byte key 好き種類: at DAT_005cad94. Resolver lookup, append. */
    static const char K[10] = "\x8d\x44\x82\xab\x8e\xed\x97\xde:";
    if (!match_prefix(line, K, 9)) return;
    int32_t id = (resolve != NULL) ? resolve(line + 9, user) : -1;
    if (id < 0) {
        /* Engine: MessageBoxA "不明なアイテム1" + name. */
        fprintf(stderr,
                "tables_kyaku: unknown 好き種類 '%s' (record name_idx=%d)\n",
                line + 9, r->name_index);
        return;
    }
    if (r->like_count >= KYAKU_LIKE_KIND_MAX) {
        /* Engine: MessageBoxA "好き種類登録数オーバー" + line. */
        fprintf(stderr,
                "tables_kyaku: 好き種類 overflow (cap=%d) on '%s'\n",
                KYAKU_LIKE_KIND_MAX, line);
        return;
    }
    r->like_kinds[r->like_count++] = id;
}

static void apply_dislikes_noop(const char *line)
{
    /* 5-byte key 嫌い: at DAT_005caddc. Engine matches but does
     * nothing — see file-level quirks doc. */
    static const char K[6] = "\x8c\x99\x82\xa2:";
    (void)line;
    (void)K;
    /* Intentionally empty. */
}

static void apply_like_attr(const char *line, kyaku_record_t *r)
{
    /* 9-byte key 好き属性: at DAT_005cadc8. OR up to 10 4-byte chunks. */
    static const char K[10] = "\x8d\x44\x82\xab\x91\xae\x90\xab:";
    if (!match_prefix(line, K, 9)) return;
    const char *p = line + 9;
    r->like_attr_mask = 0;
    for (int i = 0; i < 10; i++) {
        char c = *p;
        if (c == '\0' || c == ',' || c == '\r' || c == '\n') break;
        r->like_attr_mask |= kyaku_attr_4byte_mask(p);
        r->like_attr_mask |= kyaku_class_bits(p);
        p += 4;
    }
}

static void apply_file_path(const char *line, kyaku_record_t *r)
{
    /* 5-byte ASCII key "file:" at DAT_005cadd4. Copy up to 0x100 bytes
     * (engine cap iVar1 != 0x100). */
    if (!match_prefix(line, "file:", 5)) return;
    const char *p = line + 5;
    int k = 0;
    while (k < KYAKU_FILE_LEN - 1) {
        char c = p[k];
        if (c == '\0' || c == '\r' || c == '\n') break;
        r->file_path[k] = c;
        k++;
    }
    r->file_path[k] = '\0';
}

static void apply_activity_time(const char *line, kyaku_record_t *r)
{
    /* 9-byte key 活動時間: at DAT_005cade4. Up to 4 2-byte SJIS tokens,
     * each contributing one bit. Unknown tokens silently skipped. */
    static const char K[10] = "\x8a\x88\x93\xae\x8e\x9e\x8a\xd4:";
    if (!match_prefix(line, K, 9)) return;
    r->activity_time_mask = 0;
    const char *p = line + 9;
    for (int i = 0; i < 4; i++) {
        char c = *p;
        if (c == '\0' || c == '\r' || c == '\n') break;
        for (int t = 0; t < 4; t++) {
            if (memcmp(p, KYAKU_TIME_TOKENS[t].bytes, 2) == 0) {
                r->activity_time_mask |= KYAKU_TIME_TOKENS[t].bit;
            }
        }
        p += 2;
    }
}

static void apply_atoi_field(const char *line, const char *key, size_t klen,
                             int32_t *out)
{
    if (!match_prefix(line, key, klen)) return;
    *out = atoi(line + klen);
}

/* ── Outer parser ─────────────────────────────────────────────────── */

void tables_parse_kyaku(const unsigned char *data, size_t size,
                        kyaku_state_t *out,
                        kyaku_resolve_fn resolve, void *user)
{
    memset(out, 0, sizeof *out);

    /* Line buffer matches the engine's stack-local layout: the engine
     * reads into local_27c[0x20..0x27c] (≈0x25c bytes). Port uses 512
     * bytes; vendor lines fit well under that. */
    char line[512];
    size_t pos = 0;
    int current = -1;  /* engine local_14 — current record id (or -1) */

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

        /* Comments / blanks (engine: L511 — first byte \r/\n/'/'). */
        if (llen == 0) continue;
        if (line[0] == '/' || line[0] == '\r' || line[0] == '\n') continue;

        /* Header probe: only fires if line starts with '0' (engine's
         * 1-byte gate at L513-516). */
        if (line[0] == '0') {
            int id = parse_header(line, out->records);
            if (id >= 0) current = id;
            /* Fall through: engine continues to field-key matchers
             * after header probe. None of the keys will match the
             * `NNN:...` shape, so the fall-through is harmless. */
        }

        if (current < 0) {
            /* Engine: sprintf to local_67c (then discarded). Mirror as
             * a stderr trace once at the first orphan line. */
            continue;
        }

        kyaku_record_t *r = &out->records[current];

        /* Field-key dispatch — engine order (which is also the
         * compiled order). The 嫌い: orphan match is preserved as a
         * no-op to keep the dispatch chain faithful. */
        apply_name_index(line, r);
        apply_attr(line, r);
        apply_budget(line, r);
        apply_like_kind(line, r, resolve, user);
        apply_dislikes_noop(line);
        apply_like_attr(line, r);
        apply_file_path(line, r);
        apply_activity_time(line, r);
        apply_atoi_field(line, "\x8b\x5e:",                3, &r->suspicion);    /* 疑: */
        apply_atoi_field(line, "\xe9\x78:",                3, &r->gullibility);  /* 騙: */
        apply_atoi_field(line, "\x8f\xe3\x8f\xb8\x82\x50:", 7, &r->rise1);       /* 上昇１: */
        apply_atoi_field(line, "\x8f\xe3\x8f\xb8\x82\x51:", 7, &r->rise2);       /* 上昇２: */
        apply_atoi_field(line, "\x8f\x89\x89\xf1:",        5, &r->initial);      /* 初回: */
        apply_atoi_field(line, "\x83\x89\x83\x93\x83\x5f\x83\x80:", 9,
                         &r->random);                                            /* ランダム: */
    }
}
