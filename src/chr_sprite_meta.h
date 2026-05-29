/*
 * chr_sprite_meta.{c,h} — Cchr.2a: character sprite-sheet metadata.
 *
 * The HOUSE character sprites (Recette / Tear / NPCs) are drawn by the
 * leaf renderer FUN_0045a56f (Cchr.2b, not yet ported), which is a
 * sprite-sheet → multi-quad billboard emitter.  Before it can draw it
 * needs two static data sources that the engine loads at boot:
 *
 *   1. DAT_0438abe0 — the `bmp/chr/formdata.bin` blob: a packed
 *      cell→atlas indirection table (per-sprite-sheet frame geometry).
 *      Loaded raw by the tail of FUN_004341fe (storage init); our
 *      storage.c port stops before this tail, so it was UNLOADED.
 *      Mirrored here as g_chr_formdata (chr_formdata_load()).
 *
 *   2. DAT_0438cea8[] — the per-character descriptor array (68 entries,
 *      stride 0x5058 bytes).  Built by FUN_00479f78 by parsing one
 *      `.idx` text file per character (idx/recette.idx .. idx/mint.idx).
 *      Mirrored here as g_chr_desc (chr_meta_parse_idx / chr_meta_load).
 *
 * This module ports points (1) and (2) — the *data* layer.  The leaf
 * renderer + the actor walkers (FUN_00456f56 / FUN_0045672a) that drive
 * it are later chips (Cchr.2b..2e).  See
 * docs/findings/scene1-char-sprite-render.md for the full ladder.
 *
 * --------------------------------------------------------------------
 * Per-character descriptor block (engine DAT_0438ce88 + idx*0x5058):
 *
 *   +0x00  char[0x20]  sheet name   (parsed from idx line 0, "%s")
 *   +0x20  char[0x20]  idx path     (sprintf "idx/<name>.idx" by loader)
 *   +0x40  int32       hdr0         (idx line 1, field 0)
 *   +0x44  int32       hdr1         (idx line 1, field 1)
 *   +0x48  int32       sheet_w      (idx line 2, field 0) = DAT_0438ced0
 *   +0x4c  int32       hdr3         (idx line 2, field 1)
 *   +0x50  int32       scale_x100   (idx line 4)          = DAT_0438ced8
 *   +0x54  int32       y_origin     (idx line 3)          = DAT_0438cedc
 *   +0x58  int32[]     frame LUT    (DAT_0438cee0): per-animation blocks
 *                                   of 0x100 dwords; each frame = up to 6
 *                                   dwords, packed; 0x3ff×6 = HALT/hold,
 *                                   0xffffffff = end-of-animation.
 *
 * The leaf renderer's anchor DAT_0438cea8 = block + 0x20 (so its
 * `(&DAT_0438ced0)[char*0x1416]` reads our +0x48; ced8 → +0x50;
 * cedc → +0x54; cee0 → +0x58).
 * --------------------------------------------------------------------
 */
#ifndef OPENRECET_CHR_SPRITE_META_H
#define OPENRECET_CHR_SPRITE_META_H

#include <stddef.h>
#include <stdint.h>

/* 68 character descriptors (engine PTR list 0x5c80c4 .. 0x5c81d4;
 * parse loop terminus &DAT_044e2608 = &DAT_0438cea8 + 68*0x5058). */
#define CHR_META_NUM_CHARS    68
#define CHR_META_BLOCK_BYTES  0x5058            /* per-char stride */
#define CHR_META_BLOCK_DW     0x1416            /* same, in dwords  */

/* Byte offsets within a per-char block (block base = DAT_0438ce88 +). */
#define CHR_META_OFF_NAME     0x00              /* char[0x20] */
#define CHR_META_OFF_PATH     0x20              /* char[0x20] */
#define CHR_META_OFF_HDR0     0x40
#define CHR_META_OFF_HDR1     0x44
#define CHR_META_OFF_SHEET_W  0x48              /* DAT_0438ced0 */
#define CHR_META_OFF_HDR3     0x4c
#define CHR_META_OFF_SCALE    0x50              /* DAT_0438ced8 (×100) */
#define CHR_META_OFF_YORIGIN  0x54              /* DAT_0438cedc */
#define CHR_META_OFF_LUT      0x58              /* DAT_0438cee0 */

/* frame-LUT structure (relative to CHR_META_OFF_LUT, in dwords) */
#define CHR_META_ANIM_STRIDE_DW   0x100         /* per-animation block */
#define CHR_META_FRAME_DW         6             /* dwords per frame    */
#define CHR_META_HALT             0x3ff         /* hold/loop marker    */
#define CHR_META_ANIM_END         0xffffffffu   /* end-of-animation    */

/* The descriptor array (engine DAT_0438ce88; CHR_META_NUM_CHARS blocks).
 * NULL until chr_meta_alloc() succeeds. */
extern uint8_t *g_chr_desc;

/* The chr/formdata.bin blob (engine DAT_0438abe0) + its byte size. */
extern uint8_t *g_chr_formdata;
extern size_t   g_chr_formdata_size;

/* Allocate (and zero) the descriptor array.  Returns 1 on success, 0 on
 * OOM.  Idempotent (no-op if already allocated). */
int chr_meta_alloc(void);

/* Free the descriptor array + formdata blob. */
void chr_meta_shutdown(void);

/* Parse one `.idx` file's text into descriptor block `char_idx`.
 * `text` must be NUL-terminated.  Mirrors the per-file body of
 * FUN_00479f78.  Requires chr_meta_alloc() to have succeeded and
 * 0 <= char_idx < CHR_META_NUM_CHARS. */
void chr_meta_parse_idx(int char_idx, const char *text);

/* Pointer to descriptor block `char_idx` (NULL if unallocated / OOR). */
uint8_t *chr_meta_block(int char_idx);

/* Named field accessors (read the parsed descriptor). */
const char *chr_meta_name(int char_idx);
int32_t     chr_meta_sheet_w(int char_idx);    /* DAT_0438ced0 */
int32_t     chr_meta_scale_x100(int char_idx); /* DAT_0438ced8 */
int32_t     chr_meta_y_origin(int char_idx);   /* DAT_0438cedc */

/* Frame-LUT dword accessor: block.LUT[anim*0x100 + frame*6 + field]. */
int32_t     chr_meta_lut(int char_idx, int anim, int frame, int field);

/* --- loaders that touch storage (not exercised by the host suite) --- */

/* Load bmp/chr/formdata.bin into g_chr_formdata (FUN_004341fe tail).
 * Returns 1 on success, 0 on miss/OOM. */
int chr_formdata_load(void);

/* Full descriptor build: for each of the 68 chars, storage_read its
 * idx file and parse it (FUN_00479f78).  Returns the number of idx
 * files successfully parsed.  The 68-entry name list is sourced from
 * chr_meta_idx_names() (TODO: populate from engine PTR list). */
int chr_meta_load(void);

/* The engine PTR list of 68 idx filenames (idx/recette.idx ..).
 * Returns a pointer to a NULL-terminated array, or NULL if the list
 * has not been transcribed yet (Cchr.2a follow-up). */
const char *const *chr_meta_idx_names(void);

#endif /* OPENRECET_CHR_SPRITE_META_H */
