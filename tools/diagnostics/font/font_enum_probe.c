/*
 * font_enum_probe.c — list every face that GDI knows under "MS Gothic"
 * or "MS PGothic", reporting all (face, charset) tuples.
 *
 * Build: i686-w64-mingw32-gcc -o font_enum_probe.exe font_enum_probe.c -lgdi32
 * Run:   ./font_enum_probe.exe
 *
 * Used to figure out why retail picks tmCharSet=0 and we pick
 * tmCharSet=128 for the same lfFaceName + lfCharSet input.
 */
#include <windows.h>
#include <stdio.h>

static int CALLBACK enum_cb(const LOGFONTA *lf, const TEXTMETRICA *tm,
                            DWORD font_type, LPARAM lparam)
{
    printf("  face='%-30s' cs=%3d weight=%d pitch_family=0x%02x type=%lu "
           "tmAscent=%ld tmDescent=%ld tmCharSet=%d\n",
           lf->lfFaceName,
           lf->lfCharSet,
           lf->lfWeight,
           lf->lfPitchAndFamily,
           (unsigned long)font_type,
           tm->tmAscent, tm->tmDescent, tm->tmCharSet);
    return 1;
}

int main(void) {
    HDC hdc = GetDC(NULL);

    /* List ALL fonts. */
    printf("== Enumerate all fonts ==\n");
    LOGFONTA all = {0};
    all.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExA(hdc, &all, enum_cb, 0, 0);

    printf("\n== Filter: face='MS Gothic' ==\n");
    LOGFONTA gothic = {0};
    gothic.lfCharSet = DEFAULT_CHARSET;
    strcpy(gothic.lfFaceName, "MS Gothic");
    EnumFontFamiliesExA(hdc, &gothic, enum_cb, 0, 0);

    printf("\n== Filter: face='MS Gothic' SJIS bytes ==\n");
    LOGFONTA gothic_sjis = {0};
    gothic_sjis.lfCharSet = DEFAULT_CHARSET;
    memcpy(gothic_sjis.lfFaceName, "\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e", 13);
    EnumFontFamiliesExA(hdc, &gothic_sjis, enum_cb, 0, 0);

    printf("\n== Filter: face='MS PGothic' SJIS bytes ==\n");
    LOGFONTA pgothic = {0};
    pgothic.lfCharSet = DEFAULT_CHARSET;
    memcpy(pgothic.lfFaceName, "\x82\x6c\x82\x72 \x82\x6f\x83\x53\x83\x56\x83\x62\x83\x4e", 15);
    EnumFontFamiliesExA(hdc, &pgothic, enum_cb, 0, 0);

    /* Run a couple of CreateFontIndirectA variants and report what
     * GDI actually selected for each. */
    struct test_case {
        const char *label;
        const char *face;
        int face_len;
        BYTE charset;
    } tests[] = {
        { "engine literal (SJIS PGothic + SHIFTJIS)",
          "\x82\x6c\x82\x72 \x82\x6f\x83\x53\x83\x56\x83\x62\x83\x4e", 15,
          SHIFTJIS_CHARSET },
        { "ASCII MS PGothic + SHIFTJIS",
          "MS PGothic", 10, SHIFTJIS_CHARSET },
        { "ASCII MS PGothic + ANSI",
          "MS PGothic", 10, ANSI_CHARSET },
        { "ASCII MS PGothic + DEFAULT",
          "MS PGothic", 10, DEFAULT_CHARSET },
        { "ASCII MS Gothic + SHIFTJIS",
          "MS Gothic", 9, SHIFTJIS_CHARSET },
        { "ASCII MS Gothic + ANSI",
          "MS Gothic", 9, ANSI_CHARSET },
    };
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        printf("\n== %s ==\n", tests[i].label);
        LOGFONTA lf = {0};
        lf.lfHeight = 0x2a;
        lf.lfCharSet = tests[i].charset;
        lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
        lf.lfQuality = ANTIALIASED_QUALITY;
        lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        memcpy(lf.lfFaceName, tests[i].face, tests[i].face_len);
        HFONT hf = CreateFontIndirectA(&lf);
        HGDIOBJ old = SelectObject(hdc, hf);
        char actual_face[128] = {0};
        int n = GetTextFaceA(hdc, sizeof actual_face, actual_face);
        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        printf("  -> face='%s' tmCharSet=%d tmHeight=%ld tmAscent=%ld\n",
               actual_face, tm.tmCharSet, tm.tmHeight, tm.tmAscent);
        /* Test glyph for 'o' (ASCII) and 0x8140 (full-width space). */
        MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
        GLYPHMETRICS gm;
        DWORD sz = GetGlyphOutlineA(hdc, 'o', GGO_GRAY4_BITMAP, &gm, 0, NULL, &mat);
        printf("     GGO('o')    -> size=%lu bb=%lux%lu inc=%ld\n",
               (unsigned long)sz, (unsigned long)gm.gmBlackBoxX,
               (unsigned long)gm.gmBlackBoxY, (long)gm.gmCellIncX);
        sz = GetGlyphOutlineA(hdc, 0x8140, GGO_GRAY4_BITMAP, &gm, 0, NULL, &mat);
        printf("     GGO(0x8140) -> size=%lu bb=%lux%lu inc=%ld\n",
               (unsigned long)sz, (unsigned long)gm.gmBlackBoxX,
               (unsigned long)gm.gmBlackBoxY, (long)gm.gmCellIncX);
        SelectObject(hdc, old);
        DeleteObject(hf);
    }

    /* The original final probe — keep variable names so compile stays
     * clean; clean up at end. */
    LOGFONTA lf = {0};
    lf.lfHeight = 0x2a;
    lf.lfCharSet = SHIFTJIS_CHARSET;
    lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    HFONT hf = CreateFontIndirectA(&lf);
    HGDIOBJ old = SelectObject(hdc, hf);
    char actual_face[128] = {0};
    int n = GetTextFaceA(hdc, sizeof actual_face, actual_face);
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    (void)n;

    SelectObject(hdc, old);
    DeleteObject(hf);
    ReleaseDC(NULL, hdc);
    return 0;
}
