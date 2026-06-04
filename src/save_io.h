/*
 * save_io.h — port of FUN_004902fe (boot-time save-load probe).
 *
 * The engine's loader tries save.dat first, then _save.dat as backup.
 * It compares the file size against three buckets:
 *
 *   - == 0x011efce0 (18,807,008): the "modern" JP-release format —
 *                                 stride 0xb7a5 dwords per bank, with
 *                                 magic + checksum word at the end of
 *                                 each bank. Sets DAT_095d3728 = 1.
 *
 *   - == 0x00f30ae0 (15,927,008): the "ancient" pre-release format —
 *                                 only stores a header per bank (6
 *                                 dwords each) plus a wide common
 *                                 region; the rest of the bank reads
 *                                 as 0xffffffff. Sets DAT_095d3728 = 1.
 *
 *   - any other size <= ARENA_BYTES: copies the file verbatim into the
 *                                 arena, then runs save_bank_init_all
 *                                 to re-validate each bank's checksum
 *                                 and reset any that don't match. Does
 *                                 NOT set DAT_095d3728.
 *
 *   - size > ARENA_BYTES: ignored; calls save_bank_init_all (fresh).
 *
 * The user's saves on the Carpe Fulgur English Steam release happen
 * to be exactly ARENA_BYTES (18,838,832) — they fall in the third
 * (verbatim-copy) bucket. The two legacy formats are stubbed in this
 * port (logged + verbatim-copy fallback) since the user's data won't
 * hit them; they're paper trail for a future port if vintage saves
 * surface.
 *
 * DAT_095d3728 is the engine's "skip checksum revalidation" flag (NOT
 * a "save exists" flag — see save_bank.c line 92807). The title menu
 * unlocks (CONTINUE_ANY / NEW_HAS_SAVE / CONT_HAS_SAVE) gate on the
 * actual bank contents via `save_io_scan_for_title_menu`, not on this
 * flag.
 *
 * Pure-C — uses libc fopen/fread/fclose/malloc/free. Unit-testable
 * under host gcc.
 */

#ifndef OPENRECET_SAVE_IO_H
#define OPENRECET_SAVE_IO_H

#include <stdint.h>

#include "scene_title.h"  /* for scene_title_save_t */

/* Engine literal sizes from FUN_004902fe — kept here for cross-
 * reference even though only the third bucket is actually implemented
 * today. */
#define SAVE_IO_MODERN_SIZE    0x011efce0u   /* 18,807,008 — JP release */
#define SAVE_IO_ANCIENT_SIZE   0x00f30ae0u   /* 15,927,008 — pre-release */
#define SAVE_IO_MAX_SIZE       0x011f7530u   /* SAVE_BANK_ARENA_BYTES */

/* Engine `DAT_095d3728`. Set when the loader recognised a known size
 * bucket (modern or ancient). Used by save_bank_init_all as a "skip
 * full per-bank checksum revalidation" optimisation flag. */
extern int g_save_loaded_known_format;

/* Boot-time save-load probe. Tries `primary` first, then `backup`
 * if `primary` fails to open. On success, copies disk bytes into the
 * arena and re-runs save_bank_init_all() to validate every bank.
 *
 * Returns 1 if either file was opened (regardless of whether contents
 * passed validation), 0 if neither file exists.
 *
 * NOTE: the caller must have invoked save_bank_init_all() already so
 * the arena has a baseline state. If no save file is found, the
 * existing arena state is kept untouched. */
int save_io_try_load(const char *primary, const char *backup);

/* Scan the in-memory save arena and populate the title-menu save
 * struct (consumed by scene_title_menu_init). Engine equivalents:
 *
 *   - has_any_adv_cleared:  FUN_0049a324 — any bank's dword at offset
 *                           0x244c has value 3 (cleared Adventure 2).
 *   - has_any_adv8_cleared: FUN_0049a324 — any bank's dword at offset
 *                           0x244c, when (>> 6), is in [0xd49..0xd50]
 *                           (cleared Adventure 8 range).
 *   - has_any_score:        FUN_0049a43d — any bank's score dword > 0.
 *   - hidden_char_unlocked: shared header byte DAT_056e5788 != 0.
 *
 * Safe to call on an uninitialised arena; output is all-zero. */
void save_io_scan_for_title_menu(scene_title_save_t *out);

/* Simplified port of FUN_004905a8(-1) — writes the in-memory arena
 * to disk. The engine's full FUN_004905a8 takes a `param_1` slot
 * index; when != -1 it first copies a "working bank" scratch
 * (DAT_044e3798 + active_slot * STRIDE) into the named bank and
 * re-stamps that bank's checksum. We don't have a working-bank
 * scratch yet (no gameplay state to sync), so the bank-merge branch
 * is intentionally omitted — pass param_1 = -1 to the engine and
 * you get this function's behaviour.
 *
 * Writes BOTH primary and backup paths unconditionally (engine
 * quirk — there's no atomic temp+rename). On failure to open
 * either file, the function silently skips that one and returns
 * 0; on success of either, returns 1. NULL paths are skipped.
 *
 * Pure-C. Uses libc fopen/fwrite/fclose. */
int save_io_write_arena(const char *primary, const char *backup);

/* Redirect ALL subsequent save_io_write_arena output into `dir` (writes go to
 * <dir>/<basename> instead of the cwd's real save.dat/_save.dat). Pass NULL or
 * "" to clear. Used by the TAS harness so replaying a trace NEVER overwrites the
 * user's real save — writes land in a per-run sandbox (which also captures them
 * for later divergence verification). Pure-C; the dir is copied internally. */
void save_io_set_write_dir(const char *dir);

#endif /* OPENRECET_SAVE_IO_H */
