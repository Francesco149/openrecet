/*
 * tables_tuto.h — parser for `data/tuto1.txt` .. `tuto3.txt`
 * (block #15 of FUN_00475270 / `tables_load_all`).
 *
 * The three tutorial scripts are line-based CSV with a small opcode
 * vocabulary (`CHR0`/`CHR1` dialogue lines, `GOTO` jumps, `PRID`/`PRIA`
 * price-window cues, four Japanese keywords for branch nodes, etc.).
 * Each non-blank non-comment line becomes one record in
 * `g_tuto[TUTO_RECORD_COUNT]`.
 *
 * Major engine quirk: the **parser writes** at slot
 * `(file_idx * 50 + record_idx)` but the **consumer** (FUN_00461c00 et
 * al.) reads at slot `(file_idx * 200 + record_idx)`. The two strides
 * disagree by a factor of 4, so the parser only ever fills file-0's
 * 200-slot region (and the first few records of files 1/2 by accident).
 * Vendor data overflows the parser's 50-record cap on every file. See
 * `docs/findings/engine-quirks.md` for the full story; we faithfully
 * reproduce the parser side and let the consumer port deal with the
 * read side.
 *
 * Pure C, no Win32 surface — compiles under host gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_TUTO_H
#define OPENRECET_TABLES_TUTO_H

#include <stddef.h>

/* Engine parser stride per file (`local_c * 0x32` in FUN_00475270). */
#define TUTO_PARSER_STRIDE     50

/* Engine consumer stride per file (`DAT_005c6bb0 * 0xe740 / 0x128`
 * in FUN_00461c00, line 59759 of all.c). */
#define TUTO_CONSUMER_STRIDE   200

/* Three tuto files in load order: tuto1, tuto2, tuto3. */
#define TUTO_FILE_COUNT        3

/* Array size = 3 × 200 to match the consumer's view (vendor data
 * actually causes the parser to write up to slot 159 today — plenty
 * inside this region). */
#define TUTO_RECORD_COUNT      (TUTO_FILE_COUNT * TUTO_CONSUMER_STRIDE)

/* Per-record text buffer size (engine copies up to 0x100 bytes). */
#define TUTO_TEXT_SIZE         256

/* Engine opcode values from the dispatch chain at L2977-3067 of
 * docs/decompiled/by-address/475270.c. Note opcode 7 is unused. */
enum {
    TUTO_OP_CHR0        = 0,      /* "CHR0",  1 int + text                */
    TUTO_OP_CHR1        = 1,      /* "CHR1",  1 int + text                */
    TUTO_OP_TAGD        = 2,      /* "TAGD",  no args (target-window show)*/
    TUTO_OP_PRID        = 3,      /* "PRID",  no args (price-window show) */
    TUTO_OP_PRIA        = 4,      /* "PRIA",  no args (price-input wait)  */
    TUTO_OP_BUN0        = 5,      /* "BUN0",  7 ints                      */
    TUTO_OP_GOTO        = 6,      /* "GOTO",  7 ints (target id in [0])   */
    /* 7: gap — no token maps here.                                       */
    TUTO_OP_TAGN        = 8,      /* "TAGN",  no args (target-window hide)*/
    TUTO_OP_TOUT        = 9,      /* "TOUT",  no args (NPC exits)         */
    TUTO_OP_ITEM        = 10,     /* "アイテム", no args (item window)     */
    TUTO_OP_SWORD       = 11,     /* "剣選択", 7 ints                      */
    TUTO_OP_PRICE       = 12,     /* "値段" or "高く", 7 ints              */
    TUTO_OP_DISCOUNT    = 13,     /* "値引",  7 ints                       */
    TUTO_OP_MARKUP      = 14,     /* "値上",  7 ints                       */
    TUTO_OP_SET_INITIAL = 20,     /* "初期金額決定", no args                */
    TUTO_OP_SENTINEL    = -1,     /* end-of-records marker / id == -1 line */
};

/*
 * Per-record layout — exactly 0x128 bytes to match the engine's
 * `&DAT_005d1fc8`-based array. Field offsets are anchored by
 * `_Static_assert` so any accidental layout drift trips a compile-time
 * error.
 */
struct tuto_record {
    int  id;                       /* 0x000 — first int on the line       */
    int  opcode;                   /* 0x004 — see TUTO_OP_*               */
    char text[TUTO_TEXT_SIZE];     /* 0x008 — set by CHR0/CHR1 / id<-1    */
    int  args[7];                  /* 0x108 — 7 ints for BUN0/GOTO/etc.   */
    int  chr_arg;                  /* 0x124 — single int for CHR0/CHR1    */
};

_Static_assert(sizeof(struct tuto_record) == 0x128, "tuto_record size");

/*
 * Engine-global array. BSS-zero at load → every untouched slot has
 * id=0, opcode=0 (=CHR0), empty text. The parser writes the touched
 * slots; the trailing slot after each file is stamped with opcode = -1
 * as a sentinel.
 */
extern struct tuto_record g_tuto[TUTO_RECORD_COUNT];

/*
 * Parse one tuto file (`tuto1.txt` → file_index 0, etc.) into the
 * shared records array. Writes start at slot
 * `file_index * TUTO_PARSER_STRIDE` and grow upward; the slot after
 * the last record is set to opcode = TUTO_OP_SENTINEL.
 *
 * Returns the number of records actually parsed (excludes the
 * sentinel). Caller is responsible for sizing `records` large enough
 * to absorb the engine's overflow — at minimum
 * `file_index * TUTO_PARSER_STRIDE + lines_in_file + 1`. The shared
 * `g_tuto[600]` is always large enough for vendor data.
 *
 * `data` is read as raw bytes; trailing NUL is not required since
 * `size` is authoritative. Lines may use LF or CRLF endings.
 */
int tables_parse_tuto(int file_index,
                      const unsigned char *data, size_t size,
                      struct tuto_record *records);

#endif /* OPENRECET_TABLES_TUTO_H */
