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
    /* The 68-entry engine PTR list @ 0x5c80c4 (transcribed via objdump of
     * .data; many slots share a sheet — e.g. sraim ×6, ropper ×5). The
     * count matches CHR_META_NUM_CHARS exactly. */
    static const char *const names[CHR_META_NUM_CHARS] = {
        "idx/recette.idx",      /*  0 */
        "idx/tear.idx",         /*  1 */
        "idx/tear.idx",         /*  2 */
        "idx/kensi.idx",        /*  3 */
        "idx/sraim.idx",        /*  4 */
        "idx/sraim.idx",        /*  5 */
        "idx/sraim.idx",        /*  6 */
        "idx/sraim.idx",        /*  7 */
        "idx/sraim.idx",        /*  8 */
        "idx/sraim.idx",        /*  9 */
        "idx/mobu_men01.idx",   /* 10 */
        "idx/alyman.idx",       /* 11 */
        "idx/alyman.idx",       /* 12 */
        "idx/alyman.idx",       /* 13 */
        "idx/alyman.idx",       /* 14 */
        "idx/killer_bee.idx",   /* 15 */
        "idx/killer_bee.idx",   /* 16 */
        "idx/killer_bee.idx",   /* 17 */
        "idx/kinoko.idx",       /* 18 */
        "idx/kabocha.idx",      /* 19 */
        "idx/kobolt.idx",       /* 20 */
        "idx/ropper.idx",       /* 21 */
        "idx/ropper.idx",       /* 22 */
        "idx/ropper.idx",       /* 23 */
        "idx/ropper.idx",       /* 24 */
        "idx/ropper.idx",       /* 25 */
        "idx/yukiusagi.idx",    /* 26 */
        "idx/felm.idx",         /* 27 */
        "idx/caillou.idx",      /* 28 */
        "idx/rui_shop.idx",     /* 29 */
        "idx/thierl_action.idx",/* 30 */
        "idx/nagi_action.idx",  /* 31 */
        "idx/felm_shop.idx",    /* 32 */
        "idx/boss_sraim.idx",   /* 33 */
        "idx/kinoko.idx",       /* 34 */
        "idx/mobu_yungmen01.idx",/* 35 */
        "idx/mobu_madam01.idx", /* 36 */
        "idx/mobu_girl01.idx",  /* 37 */
        "idx/mobu_jisan01.idx", /* 38 */
        "idx/mobu_guild_m.idx", /* 39 */
        "idx/eran.idx",         /* 40 */
        "idx/gurif_act.idx",    /* 41 */
        "idx/arma_act.idx",     /* 42 */
        "idx/euria.idx",        /* 43 */
        "idx/moai.idx",         /* 44 */
        "idx/moai.idx",         /* 45 */
        "idx/foe.idx",          /* 46 */
        "idx/danbo.idx",        /* 47 */
        "idx/alouette.idx",     /* 48 */
        "idx/bom.idx",          /* 49 */
        "idx/bom.idx",          /* 50 */
        "idx/bom.idx",          /* 51 */
        "idx/bom.idx",          /* 52 */
        "idx/nomal_knight.idx", /* 53 */
        "idx/nomal_knight.idx", /* 54 */
        "idx/nomal_knight.idx", /* 55 */
        "idx/nomal_knight.idx", /* 56 */
        "idx/nomal_knight.idx", /* 57 */
        "idx/nomal_knight.idx", /* 58 */
        "idx/skelton.idx",      /* 59 */
        "idx/skelton.idx",      /* 60 */
        "idx/ghost.idx",        /* 61 */
        "idx/ghost.idx",        /* 62 */
        "idx/boss_alyman.idx",  /* 63 */
        "idx/boss_kabocha.idx", /* 64 */
        "idx/rece_papa.idx",    /* 65 */
        "idx/prime.idx",        /* 66 */
        "idx/prime.idx",        /* 67 */
    };
    return names;
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
