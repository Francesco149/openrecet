/*
 * test_recet_ini.c — unit tests for `recet.ini` parsing.
 *
 * The pure-C parser is fed synthetic + vendor ini text. The Win32-only
 * paths (recet_ini_load with fopen, recet_ini_default_path with
 * GetModuleFileNameA) are not exercised here — they're trivial glue
 * over the parser.
 *
 * Coverage:
 *   1. Empty input → engine defaults applied (winmode=1, screen=0 → 640×480,
 *                                              se=mu=9, pad/skill defaults)
 *   2. Default pad/skill tables match the byte tables from the unpacked binary
 *   3. Screen lookup (all four branches incl. fallthrough)
 *   4. Per-key parsing (one assertion per [setup] scalar in engine order)
 *   5. [option] pad/skill grid with the formatted-name keys
 *   6. Case-insensitive section + key matching (Win32 INI semantics)
 *   7. Comments stripped (`;` and `#`)
 *   8. Whitespace tolerance around `=`
 *   9. `bgnodisp` auto-derives from `easydisp` (engine quirk)
 *  10. Volume clamp [0,9] for se/mu (over-range, under-range)
 *  11. Section-less keys ignored, unknown sections/keys ignored
 *  12. Vendor recet.ini round-trip — load the shipping file and verify
 *      the values that boot smoke prints
 */
#include "t.h"
#include "recet_ini.h"

#include <stdio.h>
#include <string.h>

static void parse(const char *s, struct recet_ini *out)
{
    recet_ini_parse(s, strlen(s), out);
}

int test_recet_ini_empty_applies_defaults(void)
{
    struct recet_ini ini;
    memset(&ini, 0xCC, sizeof ini);
    parse("", &ini);

    T_ASSERT_EQ_I(ini.winmode, 1);
    T_ASSERT_EQ_I(ini.aspect,  1);
    T_ASSERT_EQ_I(ini.se,      9);
    T_ASSERT_EQ_I(ini.mu,      9);
    T_ASSERT_EQ_I(ini.screen,  0);
    T_ASSERT_EQ_I(ini.width,   640);
    T_ASSERT_EQ_I(ini.height,  480);
    T_ASSERT_EQ_I(ini.fps,     0);
    T_ASSERT_EQ_I(ini.dispfps, 0);
    T_ASSERT_EQ_I(ini.usefog,  0);
    T_ASSERT_EQ_I(ini.windowpos, 0);
    T_ASSERT_EQ_I(ini.bgnodisp, 0);   /* easydisp default is 0 */
    T_ASSERT_EQ_I(ini.camfree,  0);
    return 0;
}

int test_recet_ini_default_pad_skill_tables(void)
{
    /* From the unpacked binary: 0x005c81d8 (pad), 0x005c8204 (skill).
     * Engine reads each byte and adds 1. */
    static const int16_t want_pad[2][9] = {
        {  1,  2,  3,  4, 39, 37, 16, 35, 36 },
        { 40, 41, 42, 43, 44, 45, 46, 47, 48 },
    };
    struct recet_ini ini;
    parse("", &ini);
    for (int c = 0; c < RECET_INI_CONTROLLERS; c++) {
        for (int k = 0; k < RECET_INI_PAD_KEYS; k++) {
            T_ASSERT_EQ_I(ini.pad[c][k], want_pad[c][k]);
        }
        for (int k = 0; k < RECET_INI_SKILL_KEYS; k++) {
            T_ASSERT_EQ_I(ini.skill[c][k], 0); /* 0xff + 1 truncates to 0 */
        }
    }
    return 0;
}

int test_recet_ini_screen_lookup_all_branches(void)
{
    int w, h;
    recet_ini_resolution(0, &w, &h); T_ASSERT_EQ_I(w, 640);  T_ASSERT_EQ_I(h, 480);
    recet_ini_resolution(1, &w, &h); T_ASSERT_EQ_I(w, 800);  T_ASSERT_EQ_I(h, 600);
    recet_ini_resolution(2, &w, &h); T_ASSERT_EQ_I(w, 1024); T_ASSERT_EQ_I(h, 768);
    recet_ini_resolution(3, &w, &h); T_ASSERT_EQ_I(w, 1280); T_ASSERT_EQ_I(h, 960);
    /* engine's switch falls through to 1280x960 for any out-of-range value */
    recet_ini_resolution(99, &w, &h); T_ASSERT_EQ_I(w, 1280); T_ASSERT_EQ_I(h, 960);
    recet_ini_resolution(-1, &w, &h); T_ASSERT_EQ_I(w, 1280); T_ASSERT_EQ_I(h, 960);
    return 0;
}

int test_recet_ini_screen_drives_width_height(void)
{
    struct recet_ini ini;
    parse("[setup]\nscreen=2\n", &ini);
    T_ASSERT_EQ_I(ini.screen, 2);
    T_ASSERT_EQ_I(ini.width,  1024);
    T_ASSERT_EQ_I(ini.height, 768);
    return 0;
}

int test_recet_ini_all_setup_scalars(void)
{
    /* One value per scalar in FUN_0047a474, in engine order. Distinct
     * non-default integers so any cross-mapping bug surfaces. */
    const char *src =
        "[setup]\n"
        "aspect=2\n"
        "winmode=0\n"
        "fps=60\n"
        "dispfps=1\n"
        "sfnouse=3\n"
        "texmode=4\n"
        "mapmode=5\n"
        "demomode=6\n"
        "usemipmap=7\n"
        "usetree=8\n"
        "uselighttex=9\n"
        "texlevel=10\n"
        "toorioff=11\n"
        "windowpos=12\n"
        "winx=13\n"
        "winy=14\n"
        "nolight=15\n"
        "nolight_s=16\n"
        "easydisp=17\n"
        "s_easydisp=18\n"
        "usefog=19\n"
        "screen=1\n";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.aspect,       2);
    T_ASSERT_EQ_I(ini.winmode,      0);
    T_ASSERT_EQ_I(ini.fps,          60);
    T_ASSERT_EQ_I(ini.dispfps,      1);
    T_ASSERT_EQ_I(ini.sfnouse,      3);
    T_ASSERT_EQ_I(ini.texmode,      4);
    T_ASSERT_EQ_I(ini.mapmode,      5);
    T_ASSERT_EQ_I(ini.demomode,     6);
    T_ASSERT_EQ_I(ini.usemipmap,    7);
    T_ASSERT_EQ_I(ini.usetree,      8);
    T_ASSERT_EQ_I(ini.uselighttex,  9);
    T_ASSERT_EQ_I(ini.texlevel,     10);
    T_ASSERT_EQ_I(ini.toorioff,     11);
    T_ASSERT_EQ_I(ini.windowpos,    12);
    T_ASSERT_EQ_I(ini.winx,         13);
    T_ASSERT_EQ_I(ini.winy,         14);
    T_ASSERT_EQ_I(ini.nolight,      15);
    T_ASSERT_EQ_I(ini.nolight_s,    16);
    T_ASSERT_EQ_I(ini.easydisp,     17);
    T_ASSERT_EQ_I(ini.s_easydisp,   18);
    T_ASSERT_EQ_I(ini.usefog,       19);
    T_ASSERT_EQ_I(ini.screen,       1);
    /* bgnodisp = easydisp (engine quirk) */
    T_ASSERT_EQ_I(ini.bgnodisp,     17);
    /* derived width/height from screen=1 */
    T_ASSERT_EQ_I(ini.width,        800);
    T_ASSERT_EQ_I(ini.height,       600);
    return 0;
}

int test_recet_ini_option_pad_grid(void)
{
    /* Override pad05 on controller 1 and skill03 on controller 0. */
    const char *src =
        "[option]\n"
        "pad15=99\n"
        "skill03=77\n";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.pad[1][5],   99);    /* overridden */
    T_ASSERT_EQ_I(ini.pad[0][5],   37);    /* default kept */
    T_ASSERT_EQ_I(ini.skill[0][3], 77);    /* overridden */
    T_ASSERT_EQ_I(ini.skill[1][3], 0);     /* default kept */
    return 0;
}

int test_recet_ini_section_and_key_case_insensitive(void)
{
    /* Win32 INI keys/sections are case-insensitive. */
    const char *src =
        "[Setup]\n"
        "WINMODE=0\n"
        "[CONFIG]\n"
        "Se=5\n";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.winmode, 0);
    T_ASSERT_EQ_I(ini.se,      5);
    return 0;
}

int test_recet_ini_comments_and_blanks_skipped(void)
{
    const char *src =
        "; full-line comment\n"
        "\n"
        "[setup]\n"
        "# another comment style\n"
        "screen=2\n";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.screen, 2);
    return 0;
}

int test_recet_ini_whitespace_around_equals(void)
{
    const char *src = "[setup]\n  winmode   =   0  \n";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.winmode, 0);
    return 0;
}

int test_recet_ini_bgnodisp_mirrors_easydisp(void)
{
    /* The engine never reads "bgnodisp" from the ini; instead, after the
     * loop, DAT_0438b18c = DAT_0438b19c (easydisp). So any explicit value
     * in the ini for bgnodisp is ignored — only easydisp matters. */
    const char *src =
        "[setup]\n"
        "easydisp=42\n"
        "bgnodisp=999\n";   /* deliberately misleading — engine ignores it */
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.easydisp, 42);
    T_ASSERT_EQ_I(ini.bgnodisp, 42);    /* not 999 */
    return 0;
}

int test_recet_ini_volume_clamp(void)
{
    struct recet_ini ini;
    parse("[config]\nse=42\nmu=-3\n", &ini);
    T_ASSERT_EQ_I(ini.se, 9);   /* clamped down */
    T_ASSERT_EQ_I(ini.mu, 0);   /* clamped up */
    return 0;
}

int test_recet_ini_unknown_keys_and_sections_ignored(void)
{
    const char *src =
        "[mystery]\n"
        "anything=1\n"
        "[setup]\n"
        "totally_made_up=5\n"
        "winmode=0\n";
    struct recet_ini ini;
    parse(src, &ini);
    /* Only the known key took effect. */
    T_ASSERT_EQ_I(ini.winmode, 0);
    /* Everything else still at default. */
    T_ASSERT_EQ_I(ini.aspect,  1);
    T_ASSERT_EQ_I(ini.se,      9);
    return 0;
}

int test_recet_ini_no_trailing_newline(void)
{
    /* Last line not newline-terminated still parses. */
    const char *src = "[setup]\nwinmode=0";
    struct recet_ini ini;
    parse(src, &ini);
    T_ASSERT_EQ_I(ini.winmode, 0);
    return 0;
}

int test_recet_ini_vendor_shape(void)
{
    /* Load the vendor recet.ini directly via the file API. The shipping
     * file is one of the smallest assets and is plain ASCII, so this is
     * a deterministic round-trip check. */
    const char *path = OPENRECET_ROOT "/vendor/original/recet.ini";
    FILE *f = fopen(path, "rb");
    if (!f) T_SKIP("vendor/original/recet.ini missing");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;

    struct recet_ini ini;
    recet_ini_parse(buf, got, &ini);
    free(buf);

    /* Cross-checked against the boot trace + the head of the file:
     *   [setup] winmode=1 screen=2 → 1024×768
     *   [config] se=9 mu=9
     *   [option] pad00=1 ... pad08=36, pad10=40 ... pad18=48, skill*=0
     */
    T_ASSERT_EQ_I(ini.winmode, 1);
    T_ASSERT_EQ_I(ini.screen,  2);
    T_ASSERT_EQ_I(ini.width,   1024);
    T_ASSERT_EQ_I(ini.height,  768);
    T_ASSERT_EQ_I(ini.se,      9);
    T_ASSERT_EQ_I(ini.mu,      9);
    T_ASSERT_EQ_I(ini.pad[0][0],   1);
    T_ASSERT_EQ_I(ini.pad[0][8],   36);
    T_ASSERT_EQ_I(ini.pad[1][0],   40);
    T_ASSERT_EQ_I(ini.pad[1][8],   48);
    for (int k = 0; k < RECET_INI_SKILL_KEYS; k++) {
        T_ASSERT_EQ_I(ini.skill[0][k], 0);
        T_ASSERT_EQ_I(ini.skill[1][k], 0);
    }
    return 0;
}
