/*
 * save_io.c — see save_io.h.
 *
 * Engine source: FUN_004902fe @ 0x4902fe (682 bytes). The full
 * engine handles three size buckets (modern, ancient, fallback);
 * the user's saves land in the fallback bucket so that's the only
 * path implemented end-to-end. The two legacy paths log + fall back
 * to verbatim-copy.
 */

#include "save_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_bank.h"
#include "save_work.h"   /* the live working arena (commit source) */

int g_save_loaded_known_format = 0;

/* ─── arena scan ─────────────────────────────────────────────────────── */

/* Returns 1 if `(int32_t)item >> 6` lands in the engine's "Adventure 8
 * cleared" range. The engine literally iterates `0..7` and tests for
 * each `(item >> 6) == 0xd49 + i`, so the closed interval is
 * [0xd49, 0xd50]. */
static int adv8_range_hits(uint32_t item)
{
    int32_t shifted = (int32_t)item >> 6;
    return (shifted >= 0xd49 && shifted <= 0xd50);
}

void save_io_scan_for_title_menu(scene_title_save_t *out)
{
    memset(out, 0, sizeof *out);

    /* hidden_char_unlocked: shared header dword 6 (engine DAT_056e5788
     * — byte offset 0x18 from header base, i.e. dword index 6). The
     * engine reads it as a 1-byte test (`!= 0`); we read it as dword
     * for alignment safety, which is equivalent so long as the upper
     * 3 bytes stay zero (they do on every save we've seen). */
    uint32_t *header = (uint32_t *)save_arena_base();
    out->hidden_char_unlocked = (header[6] != 0) ? 1 : 0;

    /* Per-bank scan — engine FUN_0049a324 + FUN_0049a43d, fused.
     *
     *   has_any_score:        any bank[2] (int32) > 0
     *   has_any_adv_cleared:  any bank[2] > 0 AND bank[0xb759] == 3
     *   has_any_adv8_cleared: any item in the above bank's items list
     *                         (bank[6..6+ITEM_COUNT-1]) has (item >> 6)
     *                         in the [0xd49, 0xd50] range
     *
     * The engine drives this on init then caches into local_8 = uVar1
     * bitmask. We expose the three flags individually instead.
     */
    for (int b = 0; b < SAVE_BANK_COUNT; b++) {
        uint32_t *bank = save_bank_dwords_at(b);
        if (!bank) continue;

        int32_t score = (int32_t)bank[2];
        if (score <= 0) continue;

        out->has_any_score = 1;

        if (bank[0xb759] != 3) continue;

        out->has_any_adv_cleared = 1;

        /* Item count is ITEM_COUNT (bank dword 0xaec6 = engine DAT_0450f2b0),
         * NOT bank[0]. FUN_0049a324 scans `local_c = *piVar3` items where
         * piVar3 = bank + 0xaec6 and the item base piVar3 - 0xaec0 = bank + 6.
         * (bank[0] is a zero field — reading it here left the loop a no-op,
         * which silently never unlocked Survival once GAME_MODE==3.) */
        int32_t count = (int32_t)bank[SAVE_BANK_FIELD_ITEM_COUNT];
        if (count <= 0) continue;
        /* Cap to a sanity ceiling so a corrupt count doesn't iterate
         * past the bank end. The engine has no cap; we add one because
         * a misread of bank[0] would otherwise OOB through 18 MB of
         * the arena. Effective bank length from index 6 is
         * SAVE_BANK_STRIDE_DWORDS - 6. */
        if (count > (int32_t)(SAVE_BANK_STRIDE_DWORDS - 6)) {
            count = (int32_t)(SAVE_BANK_STRIDE_DWORDS - 6);
        }
        for (int i = 0; i < count; i++) {
            if (adv8_range_hits(bank[6 + i])) {
                out->has_any_adv8_cleared = 1;
                break;
            }
        }
    }
}

/* ─── file probe ─────────────────────────────────────────────────────── */

static int read_whole_file(const char *path, uint8_t **out_buf, long *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    long size = ftell(fp);
    if (size <= 0) { fclose(fp); return 0; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }

    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { fclose(fp); return 0; }

    size_t got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    if (got != (size_t)size) {
        free(buf);
        return 0;
    }

    *out_buf  = buf;
    *out_size = size;
    return 1;
}

int save_io_try_load(const char *primary, const char *backup)
{
    const char *paths[2] = { primary, backup };

    for (int i = 0; i < 2; i++) {
        if (!paths[i]) continue;

        uint8_t *buf  = NULL;
        long     size = 0;
        if (!read_whole_file(paths[i], &buf, &size)) {
            /* fopen failed OR file unreadable — try the next path.
             * Engine behaviour: file missing also calls save_bank_init_all
             * + scratch-copy; we skip that because the caller already
             * invoked save_bank_init_all to seed the arena. */
            continue;
        }

        uint8_t *arena = save_arena_base();

        if ((uint32_t)size > SAVE_IO_MAX_SIZE) {
            fprintf(stderr,
                    "save_io: %s — size=%ld > arena max (%u); "
                    "discarding and re-init'ing arena\n",
                    paths[i], size, SAVE_IO_MAX_SIZE);
            save_bank_init_all();
            free(buf);
            return 1;
        }

        if ((uint32_t)size == SAVE_IO_MODERN_SIZE) {
            /* Engine's first bucket — JP-release per-bank-stride
             * 0xb7a5 dwords, sets DAT_095d3728 = 1. The detailed
             * parser is deferred (user's CF EN saves don't hit this
             * path). Fall back to verbatim copy for now; the per-bank
             * checksum revalidation in save_bank_init_all will reset
             * any banks the verbatim copy doesn't match. */
            fprintf(stderr,
                    "save_io: %s — legacy modern format (0x%x) "
                    "detected; verbatim-copy fallback (per-bank "
                    "parser not yet ported)\n",
                    paths[i], SAVE_IO_MODERN_SIZE);
            memcpy(arena, buf, (size_t)size);
            g_save_loaded_known_format = 1;
            save_bank_init_all();
        } else if ((uint32_t)size == SAVE_IO_ANCIENT_SIZE) {
            /* Engine's second bucket — ancient pre-release format.
             * Same deferral as above. */
            fprintf(stderr,
                    "save_io: %s — legacy ancient format (0x%x) "
                    "detected; verbatim-copy fallback (per-bank "
                    "parser not yet ported)\n",
                    paths[i], SAVE_IO_ANCIENT_SIZE);
            memcpy(arena, buf, (size_t)size);
            g_save_loaded_known_format = 1;
            save_bank_init_all();
        } else {
            /* Engine's third bucket — "any other size <= arena_bytes".
             * The CF EN Steam release writes saves that land exactly
             * here (size == SAVE_BANK_ARENA_BYTES = 0x011f7530). */
            memcpy(arena, buf, (size_t)size);
            /* g_save_loaded_known_format stays 0 — matches engine. */
            save_bank_init_all();
        }

        fprintf(stderr,
                "save_io: loaded %s (%ld bytes)\n",
                paths[i], size);
        free(buf);
        return 1;
    }

    /* Neither file was readable. The arena keeps its caller-provided
     * baseline (save_bank_init_all output). */
    return 0;
}

/* ─── disk write ─────────────────────────────────────────────────────── */

/* Optional write-redirect dir (set by save_io_set_write_dir). When non-empty,
 * every save_io_write_arena output path is rewritten to <dir>/<basename> — so a
 * trace replay's save writes land in a sandbox instead of the user's real
 * save.dat/_save.dat. The whole point: NEVER overwrite the real save during
 * replay (and capture what was written, for divergence verification). */
static char g_save_io_write_dir[512] = {0};

void save_io_set_write_dir(const char *dir)
{
    if (!dir || !dir[0]) {
        g_save_io_write_dir[0] = '\0';
        return;
    }
    /* Strip a trailing slash so we can append "/<name>" uniformly. */
    size_t n = strlen(dir);
    while (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) n--;
    if (n >= sizeof g_save_io_write_dir) n = sizeof g_save_io_write_dir - 1;
    memcpy(g_save_io_write_dir, dir, n);
    g_save_io_write_dir[n] = '\0';
}

/* Last path component of `path` (after the final '/' or '\\'), or `path`. */
static const char *path_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static int write_arena_to(const char *path)
{
    if (!path) return 0;

    /* Redirect into the sandbox dir when one is set (replay write-protection). */
    char redirected[640];
    if (g_save_io_write_dir[0]) {
        snprintf(redirected, sizeof redirected, "%s/%s",
                 g_save_io_write_dir, path_basename(path));
        path = redirected;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    size_t wrote = fwrite(save_arena_base(), 1, SAVE_BANK_ARENA_BYTES, fp);
    fclose(fp);
    return (wrote == SAVE_BANK_ARENA_BYTES) ? 1 : 0;
}

/* Optional write-notify (set by save_io_set_write_notify). Fires once per
 * save_io_write_arena AFTER the write — the recorder uses it to capture each
 * save the game makes during a recording (req: multiple saves per trace, each to
 * its own file) for replay/divergence verification. NULL = no notify. */
static void (*g_save_io_write_notify)(void *user) = NULL;
static void  *g_save_io_write_notify_user         = NULL;

void save_io_set_write_notify(void (*fn)(void *user), void *user)
{
    g_save_io_write_notify      = fn;
    g_save_io_write_notify_user = user;
}

int save_io_write_arena(const char *primary, const char *backup)
{
    /* Engine FUN_004905a8 writes BOTH files unconditionally — no
     * atomic temp+rename, just back-to-back fopen("wb"). We match. */
    int ok_primary = write_arena_to(primary);
    int ok_backup  = write_arena_to(backup);

    if (g_save_io_write_notify) {
        g_save_io_write_notify(g_save_io_write_notify_user);
    }

    if (ok_primary || ok_backup) {
        fprintf(stderr,
                "save_io: wrote arena (%u bytes) → %s%s%s\n",
                (unsigned)SAVE_BANK_ARENA_BYTES,
                ok_primary ? (primary ? primary : "?") : "",
                (ok_primary && ok_backup) ? " + " : "",
                ok_backup ? (backup ? backup : "?") : "");
    } else {
        fprintf(stderr,
                "save_io: arena write failed — no files written "
                "(primary=%s backup=%s)\n",
                primary ? primary : "(null)",
                backup  ? backup  : "(null)");
    }
    return ok_primary || ok_backup;
}

int save_io_commit_slot(int slot)
{
    /* FUN_004905a8 head (param_1 != -1): merge the live working bank (the
     * active stage, DAT_0438b1e0) into save bank `slot`, then re-stamp that
     * bank's checksum. The engine copies 0xb7f2 dwords working→slot and sums
     * the first 0xb7f0 into [0xb7f1]; save_bank_stamp_checksum does the sum,
     * so we just memcpy the whole bank first (the magic at 0xb7f0 carries
     * over with the copy, exactly as the engine's full-bank loop does). */
    if (slot >= 0) {
        const uint32_t *src = save_work_dwords_at(save_work_active_slot());
        uint32_t       *dst = save_bank_dwords_at(slot);
        if (src && dst) {
            memcpy(dst, src, (size_t)SAVE_BANK_STRIDE_DWORDS * 4);
            save_bank_stamp_checksum(slot);
        }
    }

    /* FUN_004905a8 tail: write save.dat + _save.dat (the write-dir sandbox
     * redirect keeps replays off the user's real save). */
    return save_io_write_arena("save.dat", "_save.dat");
}
