/*
 * chr_sprite_meta_load.c — Cchr.2a: storage-backed loaders for the
 * character sprite metadata.
 *
 * Split out from chr_sprite_meta.c because these functions depend on
 * storage.c (lnkdatas / bmpdata asset access), which the host test
 * suite does not link.  The data layer (parser + accessors) stays
 * asset-independent and host-testable in chr_sprite_meta.c.
 *
 * Engine refs: FUN_004341fe tail (chr/formdata.bin → DAT_0438abe0) and
 * FUN_00479f78 (the per-character .idx descriptor build).
 */
#include "chr_sprite_meta.h"

#include <stdlib.h>
#include <string.h>

#include "storage.h"

int chr_formdata_load(void)
{
    /* FUN_004341fe tail: try the packed file first (handled by storage's
     * bmpdata overlay), else the lnkdatas entry.  storage_get_size +
     * storage_read cover both via the same name. */
    static const char *const names[] = {
        "bmp/chr/formdata.bin",
        "formdata.bin",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t sz = storage_get_size(names[i]);
        if (sz == 0)
            continue;
        uint8_t *buf = (uint8_t *)malloc(sz);
        if (buf == NULL)
            return 0;
        if (storage_read(names[i], buf) == 0) {
            free(buf);
            continue;
        }
        free(g_chr_formdata);
        g_chr_formdata = buf;
        g_chr_formdata_size = sz;
        return 1;
    }
    return 0;
}

const char *const *chr_meta_idx_names(void)
{
    /* TODO Cchr.2a-followup: transcribe the 68-entry engine PTR list at
     * 0x5c80c4 (idx/recette.idx .. idx/mint.idx).  Until then the
     * descriptor build cannot run against real assets. */
    return NULL;
}

int chr_meta_load(void)
{
    const char *const *names = chr_meta_idx_names();
    if (names == NULL || !chr_meta_alloc())
        return 0;

    int parsed = 0;
    for (int i = 0; i < CHR_META_NUM_CHARS && names[i] != NULL; i++) {
        size_t sz = storage_get_size(names[i]);
        if (sz == 0)
            continue;
        char *text = (char *)malloc(sz + 1);
        if (text == NULL)
            break;
        if (storage_read(names[i], text) == 0) {
            free(text);
            continue;
        }
        text[sz] = '\0';
        /* record the idx path in the block (engine sprintf "%s") */
        uint8_t *block = chr_meta_block(i);
        if (block != NULL) {
            memset(block + CHR_META_OFF_PATH, 0, 0x20);
            strncpy((char *)(block + CHR_META_OFF_PATH), names[i], 0x1f);
        }
        chr_meta_parse_idx(i, text);
        free(text);
        parsed++;
    }
    return parsed;
}
