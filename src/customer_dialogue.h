/*
 * customer_dialogue.h — the per-customer ("kyaku") runtime dialogue buffer.
 *
 * Each engine kyaku record (base DAT_06a5ea90 + idx*0x2c670) carries a large
 * dialogue tail that tables_kyaku.c deliberately discards (the lean tuning
 * struct keeps only the 18 meaningful fields).  This module re-adds the tail
 * the live haggle picker needs: the per-line TEXT / per-type variant COUNT /
 * portrait SPRITE id / voice id, parsed from each customer's `file:` script
 * (`kyaku/<name>.txt`, the same path tables_kyaku parses into file_path).
 *
 * Engine record offsets re-modelled here (flat slot s = variant + type*0x14):
 *     +0x6df8  int  count[type]              per-type variant count
 *     +0x51d8  int  sprite[s]                portrait/standee id
 *     +0x5b38  int  voice[s]                 voice id (-1 = none)
 *     +0x6e70  char text[s][0x100]           line text (raw; <BR>/<C> intact)
 *
 * Consumer: FUN_00460a1a (customer_service.c::cs_pick_line) reduces
 * `rand % count[type]` to a variant, then reads text/sprite/voice at slot s.
 * Loader: FUN_00475270's per-record fN.txt parse (all.c:74568-74715),
 * msg-block only (the grp standee art + se audio blocks are owned elsewhere).
 */
#ifndef OPENRECET_CUSTOMER_DIALOGUE_H
#define OPENRECET_CUSTOMER_DIALOGUE_H

#include <stddef.h>
#include <stdint.h>

/* type/variant grid sized to the engine record (slot s = variant + type*0x14;
 * max s*0x100 + 0x6e70 must stay < the 0x2c670 record stride → s < 0x258). */
#define KYAKU_DLG_TYPES     30        /* 0x1e — engine warns on msg type > 0x1d */
#define KYAKU_DLG_VARIANTS  20        /* 0x14 — engine warns on variant > 0x13   */
#define KYAKU_DLG_SLOTS     (KYAKU_DLG_TYPES * KYAKU_DLG_VARIANTS)  /* 600 = 0x258 */
#define KYAKU_DLG_TEXT_LEN  0x100     /* 256-byte line slot (record +0x6e70 stride) */

typedef struct kyaku_dialogue {
    int32_t count[KYAKU_DLG_TYPES];                       /* +0x6df8 */
    int32_t sprite[KYAKU_DLG_SLOTS];                      /* +0x51d8 */
    int32_t voice[KYAKU_DLG_SLOTS];                       /* +0x5b38 (-1 = none) */
    char    text[KYAKU_DLG_SLOTS][KYAKU_DLG_TEXT_LEN];    /* +0x6e70 */
} kyaku_dialogue_t;

/*
 * Parse one customer fN.txt blob (`blob`, `size` bytes; need not be
 * NUL-terminated) into `out` (caller must zero it first).  Reproduces the
 * engine's fixed-width `msgNN:SS:Vvv:text` parse: NN = line type, SS = sprite
 * id, Vvv = voice ("sno" = none, else `s`+digits = voice id), text = the rest
 * (raw, <BR>/<C> left intact for the picker/renderer).  Blank lines and `/`
 * comments are skipped; grp/se blocks are ignored (owned by scene_buy / audio).
 */
void kyaku_dialogue_parse(const char *blob, size_t size, kyaku_dialogue_t *out);

/* Per-record global store (heap-owned; NULL when a customer has no script). */
void                    kyaku_dialogue_set(int rec_index, kyaku_dialogue_t *dlg);
const kyaku_dialogue_t *kyaku_dialogue_get(int rec_index);
void                    kyaku_dialogue_free_all(void);

#endif /* OPENRECET_CUSTOMER_DIALOGUE_H */
