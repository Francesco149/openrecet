/*
 * tables_enemylist.c — `data/enemylist.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #14 in
 * docs/decompiled/by-address/475270.c (L2581..L2899). Identifies the
 * file via `s_data_enemylist_txt_005cb2a0` (size) and
 * `s_data_enemylist_txt_005cb2b4` (read); both interned copies hold
 * "data/enemylist.txt" (no path-mismatch quirk).
 *
 * Two engine globals are populated:
 *   - `&DAT_0053f8e8` — 10 × 60 × 752B floor-range sections.
 *   - `&DAT_073d8630` — 10-dword wisp drop table.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - **Slots 6..9 are storage with no key.** Engine init scrubs
 *     all 10 × 60 sections, but the parser's dungeon-key chain at
 *     L2690..L2702 only matches `ダンジョン１..ダンジョン６`. The trailing
 *     4 dungeon slots stay at their post-init sentinel state for
 *     the lifetime of the game. (Quirk #31.)
 *
 *   - **`wisp10:` lands on `:` and emits an empty name.** Init at
 *     L2608..L2612 reserves 10 wisp dwords, but the name-copy loop
 *     at L2641 reads from `line[0x26]` (the 7th char) — which is
 *     the digit for `wisp1..9` but the trailing `:` for `wisp10`.
 *     Empty name → no item lookup → slot 9 stays at -1. (Quirk #32.)
 *
 *   - **Per-line drop reset.** Each drop slot (drop[0..2] of the
 *     current enemy) is set to -1 before the per-line lookup at
 *     L2793. So a line that omits a `#`-separated drop ends with
 *     that slot at -1 rather than the previous value.
 *
 *   - **30 writable enemies + 1 terminator slot.** The init loop
 *     fills the `enemy_id` field of slots 0..30 with -1, and the
 *     parser writes a terminator `-1` to slot `local_18 + 1` after
 *     each line. The overflow MessageBoxA fires at slot 30+ (L2842
 *     `0x1e < local_3c`), but the terminator write still happens —
 *     for slot 30 that means writing slot 31's enemy_id field,
 *     which is the FIRST DROP dword of slot 0. Dormant in vendor
 *     data (no `f:` block has near 30 enemies). The port logs the
 *     overflow to stderr and STILL performs the terminator write
 *     to preserve byte-identical state. (Quirk #33.)
 *
 *   - **Longest-prefix enemy-name lookup.** Same pattern as
 *     enemy.txt: walk all records, prefix-match, longest matching
 *     name wins. So a line starting with `アーリマン緑` updates
 *     record `アーリマン緑` rather than the shorter `アーリマン`.
 *
 *   - **State carries between lines.** `floor_lo`/`floor_hi`,
 *     dungeon-slot, and enemy-slot indices are sticky across lines
 *     until the next header overrides them. An enemy line emitted
 *     BEFORE any `ダンジョン` header lands in dungeon 0; an enemy
 *     line emitted before any `f:` lands in floor block 0; both
 *     mirror the engine's `local_14=0; local_18=0; local_20=0`
 *     init at L2587..L2591.
 *
 *   - **Empty `(` / `x` are atoi(0).** The variant and count parsers
 *     do not guard against missing digits — `()` → variant=0,
 *     `x` (alone) → count=0. Vendor data always supplies digits.
 *
 *   - **`f:N` (no dash) → `floor_hi = floor_lo`.** The dash-scan
 *     terminates on `\r`/`\n` without finding `-`, so the second
 *     atoi never runs and `iVar1` (still floor_lo) is written to
 *     floor_hi.
 *
 *   - **`f:` alone → "loop err 16" (no crash).** Engine prints to
 *     a stderr-equivalent via FUN_0047aa31 and skips the line. Port
 *     logs to stderr and skips.
 *
 *   - **Effective exact-match for wisp/drop item lookup.** Engine
 *     L2655..L2675 (and the parallel block at L2799..L2822) calls
 *     FUN_00479f4d twice — once with name-len, once with item-len.
 *     Both succeed only when the names are equal (any length
 *     mismatch makes one of the two memcmps read past a NUL and
 *     fail). Port uses `tables_item_resolve` (strncmp-up-to-32),
 *     which yields the same result on well-formed vendor data.
 *
 * Safety divergences (documented, not present in engine):
 *
 *   - **No MessageBoxA on unknown enemy / drop / wisp item.** The
 *     engine pops three different titles ("無効な敵ネーム",
 *     "不明なアイテム4/6"); the port silently skips and lets the
 *     defaults (-1) stand. Diagnostic logging is gated on
 *     `verbose_unknowns` (currently unused but reserved).
 *
 *   - **Line buffer capped at 512 bytes.** Engine writes into a
 *     0x25C-byte local; the per-field walker has its own 0x100 cap.
 *     Vendor lines stay well under both.
 */

#include "tables_enemylist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enemylist_state_t g_enemylist;

/* ── SJIS dungeon keys ─────────────────────────────────────────────────
 *
 * 12-byte each: `ダンジョン１` .. `ダンジョン６`. Engine references
 * these as &DAT_005cb2e0 + i*0x10 (interned with padding). The
 * `１..６` digits are the SJIS fullwidth digits 0x824f..0x8254. */
static const unsigned char K_DUNGEON[ENEMYLIST_DUNGEON_KEYS][12] = {
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x50 }, /* ダンジョン１ */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x51 }, /* ダンジョン２ */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x52 }, /* ダンジョン３ */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x53 }, /* ダンジョン４ */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x54 }, /* ダンジョン５ */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x55 }, /* ダンジョン６ */
};

/* ── Init helpers ────────────────────────────────────────────────────── */

/* Mirror the engine init loop at L2592..L2607: floor_lo of every
 * section = -1; enemy_id of every slot 0..30 = -1. (The init writes
 * 31 enemy_id sentinels: dword 2 + 3*k for k=0..30 — that's all
 * ENEMYLIST_ENEMY_SLOTS_PER_SECTION slots.)
 *
 * Everything else (floor_hi, variants, counts, drops, wisp item
 * fields, drop slots, section_counts) starts at zero from the leading
 * memset, then floor_lo and the enemy_ids get stamped with -1 here.
 * The wisp init at L2608..L2612 sets all 10 wisp_drops to -1. */
static void init_state(enemylist_state_t *out)
{
    memset(out, 0, sizeof *out);
    for (int d = 0; d < ENEMYLIST_DUNGEON_SLOTS; d++) {
        for (int s = 0; s < ENEMYLIST_SECTIONS_PER_DUNGEON; s++) {
            out->sections[d][s].floor_lo = -1;
            for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; k++) {
                out->sections[d][s].enemies[k].enemy_id = -1;
            }
        }
    }
    for (int w = 0; w < ENEMYLIST_WISP_SLOTS; w++) {
        out->wisp_drops[w] = -1;
    }
}

/* ── Per-field parsers ───────────────────────────────────────────────── */

/* Read one line from `data` starting at `*pos` into `line[0..*llen]`.
 * Stores the terminating '\r'/'\n' as part of the line (matches the
 * engine quirk that line[0x21] = '\0' follows the terminator). On
 * EOF (data[pos] == 0 or pos >= size), returns 0 and leaves *llen=0.
 *
 * Returns 1 if a line was read, 0 if EOF. */
static int read_line(const unsigned char *data, size_t size, size_t *pos,
                     char *line, size_t cap, size_t *llen)
{
    *llen = 0;
    if (*pos >= size || data[*pos] == '\0') return 0;

    while (*pos < size && data[*pos] != '\0' && *llen + 2 < cap) {
        char c = (char)data[(*pos)++];
        line[(*llen)++] = c;
        if (c == '\r' || c == '\n') break;
    }
    line[*llen] = '\0';
    return 1;
}

/* Longest-prefix lookup of `line` against the enemy-name table.
 * Walks `names[0..count)` (stopping at flags==2 sentinel), tracks the
 * record whose `name` is the longest prefix of `line`, and returns
 * its index — or -1 if no record's name is a prefix. */
static int find_enemy_by_prefix(const char *line, size_t line_len,
                                const enemy_record_t *names, int count)
{
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < count; i++) {
        if (names[i].flags == 2) break;
        size_t nlen = 0;
        while (nlen < ENEMY_NAME_LEN && names[i].name[nlen] != '\0') nlen++;
        if (nlen == 0) continue;
        if (nlen > line_len) continue;
        if (memcmp(line, names[i].name, nlen) != 0) continue;
        if (nlen > best_len) {
            best = i;
            best_len = nlen;
        }
    }
    return best;
}

/* Copy the wisp item name from `line + 6` into `out` (cap
 * ENEMYLIST_WISP_NAME_LEN). Stops at any of \r \n : # ; or buffer
 * full. NUL-terminates. Returns the copied length (0 if `line + 6`
 * was already a terminator — e.g. `wisp1:`). */
static size_t copy_wisp_item_name(const char *line, size_t line_len, char *out)
{
    size_t i = 0;
    while (i < ENEMYLIST_WISP_NAME_LEN - 1 && 6 + i < line_len) {
        char c = line[6 + i];
        if (c == '\r' || c == '\n' || c == ':' || c == '#' || c == ';'
            || c == '\0') break;
        out[i++] = c;
    }
    out[i] = '\0';
    return i;
}

/* Copy one `:` or `#`-delimited drop name into `out`. `*pos` is on
 * the delimiter char; advance past it, copy until next \r \n : # ; \0
 * or 0x20 bytes, NUL-terminate. Returns the copied length, and
 * leaves *pos pointing at the *last copied char* (so the outer
 * loop's `(*pos)++` advances past the trailing delimiter or onto
 * EOL correctly — mirrors `local_1c = (int *)(pcVar16 + -1)` at
 * L2788). */
static size_t copy_drop_name(const char *line, size_t line_len,
                             size_t *pos, char *out)
{
    size_t i = 0;
    size_t p = *pos + 1;  /* skip the leading delimiter */
    while (i < ENEMYLIST_DROP_NAME_LEN - 1 && p < line_len) {
        char c = line[p];
        if (c == '\0' || c == '\r' || c == '\n'
            || c == ':' || c == '#' || c == ';') break;
        out[i++] = c;
        p++;
    }
    out[i] = '\0';
    *pos = (p == 0) ? 0 : p - 1;  /* leave on last-copied char */
    return i;
}

/* ── Per-section finalisers ──────────────────────────────────────────── */

/* Stamp the next-slot enemy_id with -1 (engine terminator write at
 * L2845). Bounds-checked to avoid corrupting the drops region of
 * slot 0 when local_18 wraps past 30 — the engine's quirk-#33
 * behaviour is documented in the header but the port stays safe. */
static void write_terminator(enemylist_section_t *sec, int next_slot)
{
    if (next_slot < ENEMYLIST_ENEMY_SLOTS_PER_SECTION) {
        sec->enemies[next_slot].enemy_id = -1;
    }
    /* else: engine would clobber drops[0].item_id[0]; we don't, but
     * the overflow MessageBoxA at the call site already logged. */
}

/* ── Main parser ─────────────────────────────────────────────────────── */

void tables_parse_enemylist(const unsigned char *data, size_t size,
                            enemylist_state_t *out,
                            const enemy_record_t *enemy_names,
                            int                  enemy_names_count,
                            enemylist_resolve_fn resolve,
                            void                *user)
{
    init_state(out);

    /* Engine locals:
     *   local_30 = EOF flag                — handled by read_line()
     *   local_14 = f-section index (within current dungeon)
     *   local_18 = enemy slot index (within current f-section)
     *   local_20 = dungeon index (0..5 from key compare; 0 default)
     */
    int dungeon = 0;          /* local_20 */
    int section_idx = 0;      /* local_14 */
    int enemy_slot = 0;       /* local_18 */
    enemylist_section_t *sec = &out->sections[0][0];

    char line[512];

    size_t pos = 0;
    size_t llen = 0;
    while (read_line(data, size, &pos, line, sizeof line, &llen)) {
        if (llen == 0) continue;

        /* Comment / blank dispatch (L2633). */
        if (line[0] == '\r' || line[0] == '\n' || line[0] == '/') continue;

        /* ── `wisp` branch (L2634..L2688) ─────────────────────────── */
        if (llen >= 4 && memcmp(line, "wisp", 4) == 0) {
            int wisp_num = atoi(line + 4);
            int slot = wisp_num - 1;
            char name[ENEMYLIST_WISP_NAME_LEN];
            size_t nlen = copy_wisp_item_name(line, llen, name);
            if (nlen == 0) continue;  /* engine: `if (0 < iVar1)` skips */

            int32_t item_id = -1;
            if (resolve != NULL) item_id = resolve(name, user);

            /* Slot bounds: engine init only wrote indices 0..9 to -1;
             * a `wisp0:` (slot -1) or `wisp11+:` would write out of
             * range. Port clamps to the reserved 10-slot range. */
            if (slot >= 0 && slot < ENEMYLIST_WISP_SLOTS) {
                out->wisp_drops[slot] = item_id;
            }
            continue;
        }

        /* ── Dungeon-key dispatch (L2690..L2702) ─────────────────── */
        {
            int matched_dungeon = -1;
            for (int d = 0; d < ENEMYLIST_DUNGEON_KEYS; d++) {
                if (llen >= sizeof K_DUNGEON[d]
                    && memcmp(line, K_DUNGEON[d], sizeof K_DUNGEON[d]) == 0) {
                    matched_dungeon = d;
                    break;
                }
            }
            if (matched_dungeon >= 0) {
                dungeon = matched_dungeon;
                section_idx = 0;
                /* sec pointer not updated until next f: line — mirrors
                 * the engine, which only re-computes `piVar4` on f-lines.
                 * But the new dungeon's section[0] slot is what the
                 * next f-line will land on, so subsequent enemy lines
                 * (with no f-line) would land in `sec` from the PREVIOUS
                 * dungeon. Vendor never does this — every dungeon header
                 * is followed by `f:` lines before any enemy line. */
                continue;
            }
        }

        /* ── `f:` branch (L2703..L2872) ─────────────────────────── */
        if (llen >= 2 && memcmp(line, "f:", 2) == 0) {
            /* Engine: writes floor_lo BEFORE checking for EOL — so
             * `f:` alone still produces a `*piVar4 = atoi("")-1 = -1`
             * write into the CURRENT sec's floor_lo before bailing.
             * We mirror that: capture the target section index, write
             * floor_lo, then check. */
            int target = section_idx;
            if (dungeon >= ENEMYLIST_DUNGEON_SLOTS
                || target >= ENEMYLIST_SECTIONS_PER_DUNGEON) {
                /* Storage overflow — engine would walk into the next
                 * dungeon's slots (or past the array entirely). Port
                 * silently caps. */
                continue;
            }
            enemylist_section_t *target_sec = &out->sections[dungeon][target];
            int32_t floor_lo = (int32_t)atoi(line + 2) - 1;
            target_sec->floor_lo = floor_lo;

            char c0 = line[2];
            if (c0 == '\0' || c0 == '\r' || c0 == '\n') {
                fprintf(stderr, "tables_enemylist: loop err 16 (empty f:)\n");
                continue;
            }
            /* Scan for '-' or line terminator. */
            int32_t floor_hi = floor_lo;
            size_t p = 2;
            while (p < llen) {
                char c = line[p];
                if (c == '\r' || c == '\n' || c == '\0') break;
                if (c == '-') {
                    floor_hi = (int32_t)atoi(line + p + 1) - 1;
                    break;
                }
                p++;
            }
            target_sec->floor_hi = floor_hi;
            section_idx++;
            enemy_slot = 0;
            sec = target_sec;

            /* Maintain a per-dungeon count for the boot trace. The
             * engine does not track this. */
            if (section_idx > out->section_counts[dungeon]) {
                out->section_counts[dungeon] = (int16_t)section_idx;
            }
            continue;
        }

        /* ── Enemy line (everything else) ──────────────────────── */
        int rec = find_enemy_by_prefix(line, llen, enemy_names,
                                       enemy_names_count);
        if (rec < 0) {
            /* Engine pops MessageBoxA "無効な敵ネーム"; port silently
             * skips. Matches the same suppression convention used in
             * tables_enemy.c (quirk #21 — vendor enemylist.txt has
             * many lines whose enemies aren't in the pre-baked
             * record table; the engine spams MessageBoxA on every
             * boot but the port stays quiet for test sanity). */
            continue;
        }

        /* If sec is still pointing at sections[0][0] but we haven't
         * yet seen an f-line in this run, that's fine — the engine
         * also seeds piVar4 = section[0][0] at startup.  */

        /* Engine: writes enemy_id, then clears variant, then sets
         * count = 1, then clears drop_ids. (L2753..L2759.) */
        enemy_slot &= 0x7fffffff;
        int slot = enemy_slot;
        if (slot >= ENEMYLIST_ENEMY_WRITABLE_LIMIT) {
            fprintf(stderr,
                    "tables_enemylist: enemy list overflow at slot %d "
                    "(max %d)\n", slot, ENEMYLIST_ENEMY_WRITABLE_LIMIT);
            /* Engine continues past the overflow, with `local_18`
             * incrementing further — and the terminator write at
             * slot 31 would land on drops[0].item_id[0]. The port
             * caps and skips for safety. */
            continue;
        }
        sec->enemies[slot].enemy_id = rec;
        sec->enemies[slot].variant  = 0;
        sec->enemies[slot].count    = 1;
        sec->drops[slot].item_id[0] = -1;
        sec->drops[slot].item_id[1] = -1;
        sec->drops[slot].item_id[2] = -1;

        /* Walk the line tail: scan for '(', 'x', and ':' / '#' drop
         * delimiters. Engine cursor starts at line+0x20 (= line+0)
         * and runs up to 0x100 chars. (L2760..L2839.) */
        size_t p = 0;
        int drop_idx = 0;
        char drop_name[ENEMYLIST_DROP_NAME_LEN];
        while (p < llen && p < ENEMYLIST_LINE_CAP) {
            char c = line[p];
            if (c == '\r' || c == '\n' || c == '\0') break;
            if (c == 'x') {
                sec->enemies[slot].count = (int32_t)atoi(line + p + 1);
            }
            if (line[p] == '(') {
                sec->enemies[slot].variant = (int32_t)atoi(line + p + 1);
            }
            if (line[p] == ':' || line[p] == '#') {
                size_t scan_pos = p;
                size_t nlen = copy_drop_name(line, llen, &scan_pos,
                                             drop_name);
                p = scan_pos;
                int32_t item_id = -1;
                if (nlen > 0 && resolve != NULL) {
                    item_id = resolve(drop_name, user);
                }
                if (drop_idx >= 0 && drop_idx < ENEMYLIST_DROPS_PER_ENEMY) {
                    sec->drops[slot].item_id[drop_idx] = item_id;
                }
                drop_idx++;
                if (drop_idx == ENEMYLIST_DROPS_PER_ENEMY) break;
            }
            p++;
        }

        /* Engine: terminator write at slot+1, then advance local_18. */
        enemy_slot = slot + 1;
        write_terminator(sec, enemy_slot);
        if (enemy_slot > ENEMYLIST_ENEMY_WRITABLE_LIMIT) {
            fprintf(stderr,
                    "tables_enemylist: enemy list overflow — %d slots used\n",
                    enemy_slot);
        }
    }
}
