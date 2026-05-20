/*
 * test_tables_stage.c — unit tests for `idx/stage.idx` parsing.
 *
 * Covers the full key dispatch (57 keys), the per-record default-init
 * block, the `stage:X-Y` header machinery (including the unknown-ID
 * fallback at quirk #34), the field-sharing behaviour for sunpos /
 * sunset / moonpos (quirks #35 / #36), and a vendor-shape sanity
 * check against a representative inlined sample.
 */
#include "t.h"
#include "tables_stage.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */

int test_tables_stage_layout_byte_offsets(void)
{
    /* Per-record stride must equal the engine's 0x1b3c. _Static_assert
     * in the header already guards this at compile time, but a runtime
     * assert here gives a clear failure for anyone hand-editing fields. */
    T_ASSERT_EQ_U(sizeof(stage_record_t), 0x1b3c);
    T_ASSERT_EQ_U(offsetof(stage_record_t, mapbg),       0x004);
    T_ASSERT_EQ_U(offsetof(stage_record_t, dungeon_id),  0x104);
    T_ASSERT_EQ_U(offsetof(stage_record_t, map),         0x314);
    T_ASSERT_EQ_U(offsetof(stage_record_t, minimap),     0x1714);
    T_ASSERT_EQ_U(offsetof(stage_record_t, fishmap),     0x1814);
    T_ASSERT_EQ_U(offsetof(stage_record_t, startpos),    0x1914);
    T_ASSERT_EQ_U(offsetof(stage_record_t, waterfile),   0x1920);
    T_ASSERT_EQ_U(offsetof(stage_record_t, map_count),   0x1a2c);
    T_ASSERT_EQ_U(offsetof(stage_record_t, sun_pos),     0x1a7c);
    T_ASSERT_EQ_U(offsetof(stage_record_t, sunpos_mode), 0x1a88);
    T_ASSERT_EQ_U(offsetof(stage_record_t, moonpos_set), 0x1a8c);
    T_ASSERT_EQ_U(offsetof(stage_record_t, deathheight), 0x1b1c);
    T_ASSERT_EQ_U(offsetof(stage_record_t, smallwater),  0x1b38);
    return 0;
}

int test_tables_stage_empty(void)
{
    stage_state_t out;
    memset(&out, 0xCC, sizeof out);
    tables_parse_stage((const unsigned char *)"", 0, &out);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_stage_lines_before_first_header_dropped(void)
{
    /* Non-`stage:` content before any header must not crash and must
     * leave count = 0 (engine guard at L87: `local_10 < 0`). */
    const char input[] =
        "maptype:5\r\n"
        "map:nope.x\r\n"
        "// comment\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_stage_comments_and_blanks_skipped(void)
{
    const char input[] =
        "/leading-comment\r\n"
        "\r\n"
        "stage:0-1\r\n"
        "// commented value\r\n"
        "/maptype:99\r\n"   /* '/' prefix is a comment, not a /key */
        "\r\n"
        "maptype:7\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].maptype, 7);
    return 0;
}

int test_tables_stage_defaults_applied_on_open(void)
{
    /* A record with only the header opens it and applies the full
     * default block. Verify the engine's non-zero defaults are
     * present. */
    const char input[] = "stage:0-1\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->dungeon_id, 0);          /* "0-1" → index 0 */
    T_ASSERT_EQ_I(r->wateralpha, 0x7f);
    T_ASSERT_EQ_I(r->wateralpha_fish, -1);
    T_ASSERT_EQ_I(r->farclip, 600);
    T_ASSERT_EQ_I(r->wateranimnum, 1);
    T_ASSERT_EQ_I(r->watersize, 0x40);
    T_ASSERT_EQ_I(r->wateranimspeed, 4);
    T_ASSERT_EQ_I(r->smokecolor[0], 0xff);
    T_ASSERT_EQ_I(r->smokecolor[1], 0xcc);
    T_ASSERT_EQ_I(r->smokecolor[2], 0xb2);
    T_ASSERT_EQ_I(r->deathheight, -70);
    T_ASSERT_EQ_I(r->unk_b20, 1);
    T_ASSERT(r->fog[0] == 1.0f);
    T_ASSERT(r->fog[1] == 1.0f);
    T_ASSERT(r->lightdir[0] == 1.0f);
    T_ASSERT(r->lightdir[1] == 1.0f);
    T_ASSERT(r->lightdir[2] == 1.0f);
    T_ASSERT(r->waterheight == -1000.0f);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            T_ASSERT(r->maplight_d[i][j] == 1.0f);
            T_ASSERT(r->maplight_a[i][j] == 1.0f);
        }
    }
    return 0;
}

int test_tables_stage_id_dispatch_short_keys(void)
{
    /* All 14 three-byte IDs map to indices 0..13 in order. */
    static const struct { const char *id; int expected; } cases[] = {
        {"0-1", 0}, {"0-2", 1}, {"0-3", 2}, {"0-4", 3}, {"0-5", 4},
        {"1-1", 5}, {"1-2", 6}, {"1-3", 7}, {"1-4", 8}, {"1-5", 9},
        {"1-6", 10}, {"1-7", 11}, {"1-8", 12}, {"1-9", 13},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char input[64];
        snprintf(input, sizeof input, "stage:%s\r\n", cases[i].id);
        stage_state_t out;
        tables_parse_stage((const unsigned char *)input, strlen(input), &out);
        T_ASSERT_EQ_I(out.count, 1);
        T_ASSERT_EQ_I(out.records[0].dungeon_id, cases[i].expected);
    }
    return 0;
}

int test_tables_stage_id_dispatch_long_keys(void)
{
    /* All 7 four-byte IDs map to indices 14..20. */
    static const struct { const char *id; int expected; } cases[] = {
        {"1-10", 14}, {"1-11", 15}, {"1-12", 16}, {"1-13", 17},
        {"1-14", 18}, {"1-15", 19}, {"1-16", 20},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char input[64];
        snprintf(input, sizeof input, "stage:%s\r\n", cases[i].id);
        stage_state_t out;
        tables_parse_stage((const unsigned char *)input, strlen(input), &out);
        T_ASSERT_EQ_I(out.count, 1);
        T_ASSERT_EQ_I(out.records[0].dungeon_id, cases[i].expected);
    }
    return 0;
}

int test_tables_stage_id_unknown_falls_back_to_1_16(void)
{
    /* Quirk #34: the chain's default `uVar5 = 0x14` (= 20, = "1-16")
     * stays in place when no ID matches, so an unknown stage ID is
     * treated as if it were "1-16". */
    const char input[] = "stage:99-99\r\nmaptype:1\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].dungeon_id, 0x14);
    T_ASSERT_EQ_I(out.records[0].maptype, 1);
    return 0;
}

int test_tables_stage_int_fields(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "maptype:3\r\n"
        "drawcode:2\r\n"
        "waterdrawcode:4\r\n"
        "wateralpha:80\r\n"
        "wateralpha_fish:50\r\n"
        "wateradd:1\r\n"
        "hikaridrawcode:5\r\n"
        "hikarialpha:96\r\n"
        "hikariadd:1\r\n"
        "farclip:200\r\n"
        "mapnumx:10\r\n"
        "mapnumz:20\r\n"
        "watersize:128\r\n"
        "wateranimnum:30\r\n"
        "wateranimspeed:2\r\n"
        "deathheight:-50\r\n"
        "maplight:3\r\n"
        "chrlight:2\r\n"
        "mapviewarea:1000\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->maptype,         3);
    T_ASSERT_EQ_I(r->drawcode,        2);
    T_ASSERT_EQ_I(r->waterdrawcode,   4);
    T_ASSERT_EQ_I(r->wateralpha,      80);
    T_ASSERT_EQ_I(r->wateralpha_fish, 50);
    T_ASSERT_EQ_I(r->wateradd,        1);
    T_ASSERT_EQ_I(r->hikaridrawcode,  5);
    T_ASSERT_EQ_I(r->hikarialpha,     96);
    T_ASSERT_EQ_I(r->hikariadd,       1);
    T_ASSERT_EQ_I(r->farclip,         200);
    T_ASSERT_EQ_I(r->mapnumx,         10);
    T_ASSERT_EQ_I(r->mapnumz,         20);
    T_ASSERT_EQ_I(r->watersize,       128);
    T_ASSERT_EQ_I(r->wateranimnum,    30);
    T_ASSERT_EQ_I(r->wateranimspeed,  2);
    T_ASSERT_EQ_I(r->deathheight,     -50);
    T_ASSERT_EQ_I(r->maplight,        3);
    T_ASSERT_EQ_I(r->chrlight,        2);
    T_ASSERT_EQ_I(r->mapviewarea,     1000);
    return 0;
}

int test_tables_stage_float_fields(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "scroll:0.5\r\n"
        "mapposy:2.5\r\n"
        "chrlightoffset:1.0\r\n"
        "maplightspeed:0.25\r\n"
        "mapx:64\r\n"
        "mapz:128\r\n"
        "waterheight:-200\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->scroll         == 0.5f);
    T_ASSERT(r->mapposy        == 2.5f);
    T_ASSERT(r->chrlightoffset == 1.0f);
    T_ASSERT(r->maplightspeed  == 0.25f);
    T_ASSERT(r->mapx           == 64.0f);
    T_ASSERT(r->mapz           == 128.0f);
    T_ASSERT(r->waterheight    == -200.0f);
    return 0;
}

int test_tables_stage_flag_fields(void)
{
    /* All seven flag keys (windlerf, windbouble, windsnow, houshi,
     * windfire, smallwater, loopcamera, gakecheck) set their target
     * to 1 when the key matches — value bytes after the colon are
     * ignored. */
    const char input[] =
        "stage:0-1\r\n"
        "loopcamera:\r\n"
        "gakecheck:\r\n"
        "windlerf:\r\n"
        "windbouble:\r\n"
        "windsnow:\r\n"
        "houshi:\r\n"
        "windfire:\r\n"
        "smallwater:\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->loopcamera, 1);
    T_ASSERT_EQ_I(r->gakecheck,  1);
    T_ASSERT_EQ_I(r->windlerf,   1);
    T_ASSERT_EQ_I(r->windbouble, 1);
    T_ASSERT_EQ_I(r->windsnow,   1);
    T_ASSERT_EQ_I(r->houshi,     1);
    T_ASSERT_EQ_I(r->windfire,   1);
    T_ASSERT_EQ_I(r->smallwater, 1);
    return 0;
}

int test_tables_stage_string_fields(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "mapbg:xfile/sky.x\r\n"
        "minimap:bmp/map/mini.bmp\r\n"
        "fishmap:fish/town.bmp\r\n"
        "waterfile:bmp/umi/u_000\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(strcmp(r->mapbg,     "xfile/sky.x")      == 0);
    T_ASSERT(strcmp(r->minimap,   "bmp/map/mini.bmp") == 0);
    T_ASSERT(strcmp(r->fishmap,   "fish/town.bmp")    == 0);
    T_ASSERT(strcmp(r->waterfile, "bmp/umi/u_000")    == 0);
    T_ASSERT_EQ_I(r->mapbg_set, 1);  /* side-effect of "mapbg:" */
    return 0;
}

int test_tables_stage_map_slots_thread(void)
{
    /* Each `map:` line appends to the next slot and bumps map_count. */
    const char input[] =
        "stage:0-1\r\n"
        "map:a.x\r\n"
        "map:b.x\r\n"
        "map:c.x\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->map_count, 3);
    T_ASSERT(strcmp(r->map[0], "a.x") == 0);
    T_ASSERT(strcmp(r->map[1], "b.x") == 0);
    T_ASSERT(strcmp(r->map[2], "c.x") == 0);
    /* Slot 3 must remain empty. */
    T_ASSERT_EQ_I(r->map[3][0], '\0');
    return 0;
}

int test_tables_stage_map_overflow_safe(void)
{
    /* The port silently truncates past STAGE_MAP_SLOTS (engine would
     * clobber the minimap field at +0x1714; dormant in vendor).
     * map_count still bumps for every map: line so callers see the
     * intended count, but slot writes stop at the cap. */
    char input[1024];
    int n = snprintf(input, sizeof input, "stage:0-1\r\n");
    for (int i = 0; i < STAGE_MAP_SLOTS + 5; i++) {
        n += snprintf(input + n, sizeof input - n, "map:%d.x\r\n", i);
    }
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, (size_t)n, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->map_count, STAGE_MAP_SLOTS + 5);
    /* Last in-cap slot must hold the (STAGE_MAP_SLOTS - 1)th entry. */
    char expect[32];
    snprintf(expect, sizeof expect, "%d.x", STAGE_MAP_SLOTS - 1);
    T_ASSERT(strcmp(r->map[STAGE_MAP_SLOTS - 1], expect) == 0);
    /* The minimap field at +0x1714 must still be empty (NOT clobbered). */
    T_ASSERT_EQ_I(r->minimap[0], '\0');
    return 0;
}

int test_tables_stage_mapcamera_slots_thread(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "mapcamera:cam0.x\r\n"
        "mapcamera:cam1.x\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->mapcamera_count, 2);
    T_ASSERT(strcmp(r->mapcamera[0], "cam0.x") == 0);
    T_ASSERT(strcmp(r->mapcamera[1], "cam1.x") == 0);
    return 0;
}

int test_tables_stage_fog_pair(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "fog:20:500\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->fog[0] == 20.0f);
    T_ASSERT(r->fog[1] == 500.0f);
    return 0;
}

int test_tables_stage_fog_one_value_keeps_default_second(void)
{
    /* Only the first atof runs; the second is gated on a ':' that
     * never arrives, so fog[1] stays at its 1.0f default. */
    const char input[] =
        "stage:0-1\r\n"
        "fog:42\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT(out.records[0].fog[0] == 42.0f);
    T_ASSERT(out.records[0].fog[1] == 1.0f);
    return 0;
}

int test_tables_stage_int_triples(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "startpos:10:20:30\r\n"
        "fogcolor:230:240:255\r\n"
        "smokecolor:1:2:3\r\n"
        "backcolor:4:5:6\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->startpos[0],  10);
    T_ASSERT_EQ_I(r->startpos[1],  20);
    T_ASSERT_EQ_I(r->startpos[2],  30);
    T_ASSERT_EQ_I(r->fogcolor[0],  230);
    T_ASSERT_EQ_I(r->fogcolor[1],  240);
    T_ASSERT_EQ_I(r->fogcolor[2],  255);
    T_ASSERT_EQ_I(r->smokecolor[0], 1);
    T_ASSERT_EQ_I(r->smokecolor[1], 2);
    T_ASSERT_EQ_I(r->smokecolor[2], 3);
    T_ASSERT_EQ_I(r->backcolor[0],  4);
    T_ASSERT_EQ_I(r->backcolor[1],  5);
    T_ASSERT_EQ_I(r->backcolor[2],  6);
    return 0;
}

int test_tables_stage_float_triples_colon(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "lightdir:1.0:-0.1:-0.1\r\n"
        "lightcolor:0.5:0.5:0.5\r\n"
        "lightamb:0.6:0.6:0.6\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->lightdir[0] ==  1.0f);
    T_ASSERT(r->lightdir[1] == -0.1f);
    T_ASSERT(r->lightdir[2] == -0.1f);
    T_ASSERT(r->lightcolor[0] == 0.5f);
    T_ASSERT(r->lightcolor[1] == 0.5f);
    T_ASSERT(r->lightcolor[2] == 0.5f);
    T_ASSERT(r->lightamb[0]   == 0.6f);
    T_ASSERT(r->lightamb[1]   == 0.6f);
    T_ASSERT(r->lightamb[2]   == 0.6f);
    return 0;
}

int test_tables_stage_maplight_d_a_space_pairs(void)
{
    /* maplight_d[rgb] and maplight_a[rgb] are float pairs separated
     * by a single space (NOT colon). Engine: lines 3607-3610. */
    const char input[] =
        "stage:0-1\r\n"
        "maplight_dr:0.1 0.9\r\n"
        "maplight_dg:0.2 0.8\r\n"
        "maplight_db:0.3 0.7\r\n"
        "maplight_ar:0.4 0.6\r\n"
        "maplight_ag:0.5 0.5\r\n"
        "maplight_ab:0.6 0.4\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->maplight_d[0][0] == 0.1f);
    T_ASSERT(r->maplight_d[0][1] == 0.9f);
    T_ASSERT(r->maplight_d[1][0] == 0.2f);
    T_ASSERT(r->maplight_d[1][1] == 0.8f);
    T_ASSERT(r->maplight_d[2][0] == 0.3f);
    T_ASSERT(r->maplight_d[2][1] == 0.7f);
    T_ASSERT(r->maplight_a[0][0] == 0.4f);
    T_ASSERT(r->maplight_a[0][1] == 0.6f);
    T_ASSERT(r->maplight_a[1][0] == 0.5f);
    T_ASSERT(r->maplight_a[1][1] == 0.5f);
    T_ASSERT(r->maplight_a[2][0] == 0.6f);
    T_ASSERT(r->maplight_a[2][1] == 0.4f);
    return 0;
}

int test_tables_stage_sunpos_numeric_sets_mode_1(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "sunpos:1000:800000:-1000000\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->sun_pos[0] ==  1000.0f);
    T_ASSERT(r->sun_pos[1] ==  800000.0f);
    T_ASSERT(r->sun_pos[2] == -1000000.0f);
    T_ASSERT_EQ_I(r->sunpos_mode, STAGE_SUN_SUNPOS);
    T_ASSERT_EQ_I(r->moonpos_set, 0);
    return 0;
}

int test_tables_stage_sunpos_off_sets_mode_0(void)
{
    /* `sunpos:off` short-circuits to mode=0; coords stay at defaults
     * (all zero). Engine: code_r0x004765a8 at L3958-3960. */
    const char input[] =
        "stage:0-1\r\n"
        "sunpos:off\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->sunpos_mode, STAGE_SUN_OFF);
    T_ASSERT(r->sun_pos[0] == 0.0f);
    T_ASSERT(r->sun_pos[1] == 0.0f);
    T_ASSERT(r->sun_pos[2] == 0.0f);
    return 0;
}

int test_tables_stage_sunset_numeric_sets_mode_2(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "sunset:1.0:2.0:3.0\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT(r->sun_pos[0] == 1.0f);
    T_ASSERT(r->sun_pos[1] == 2.0f);
    T_ASSERT(r->sun_pos[2] == 3.0f);
    T_ASSERT_EQ_I(r->sunpos_mode, STAGE_SUN_SUNSET);
    return 0;
}

int test_tables_stage_sunset_off_broken_quirk_36(void)
{
    /* Quirk #36: the engine's "off" check for `sunset:` compares
     * against the string "sunpos:off" (NOT "sunset:off"), so a real
     * `sunset:off` line does NOT short-circuit — it falls through to
     * the numeric path with atof("off") = 0.0f for sun_pos[0] and
     * leaves sun_pos[1]/sun_pos[2] at their previous values.
     * sunpos_mode still ends up at STAGE_SUN_SUNSET = 2. */
    const char input[] =
        "stage:0-1\r\n"
        "sunset:off\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->sunpos_mode, STAGE_SUN_SUNSET);  /* NOT _OFF! */
    T_ASSERT(r->sun_pos[0] == 0.0f);                   /* atof("off") */
    /* sun_pos[1]/[2] left at the post-init zero defaults. */
    T_ASSERT(r->sun_pos[1] == 0.0f);
    T_ASSERT(r->sun_pos[2] == 0.0f);
    return 0;
}

int test_tables_stage_moonpos_quirk_35_shares_coords_keeps_sun_mode(void)
{
    /* Quirk #35: moonpos: writes the SAME sun_pos fields used by
     * sunpos/sunset, but does NOT touch sunpos_mode. So a record
     * with both `sunpos:` and `moonpos:` ends up with the SECOND
     * key's coords (whichever fired last) plus the FIRST key's mode
     * flag plus moonpos_set=1. */
    const char input[] =
        "stage:0-1\r\n"
        "sunpos:100:200:300\r\n"
        "moonpos:1:2:3\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    const stage_record_t *r = &out.records[0];
    /* moonpos: ran second → its coords win. */
    T_ASSERT(r->sun_pos[0] == 1.0f);
    T_ASSERT(r->sun_pos[1] == 2.0f);
    T_ASSERT(r->sun_pos[2] == 3.0f);
    /* sunpos_mode still reflects the earlier `sunpos:` numeric match. */
    T_ASSERT_EQ_I(r->sunpos_mode, STAGE_SUN_SUNPOS);
    T_ASSERT_EQ_I(r->moonpos_set, 1);
    return 0;
}

int test_tables_stage_multiple_records_thread(void)
{
    const char input[] =
        "stage:0-1\r\n"
        "maptype:5\r\n"
        "stage:0-2\r\n"
        "maptype:7\r\n"
        "stage:1-5\r\n"
        "maptype:9\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 3);
    T_ASSERT_EQ_I(out.records[0].dungeon_id, 0);
    T_ASSERT_EQ_I(out.records[0].maptype,    5);
    T_ASSERT_EQ_I(out.records[1].dungeon_id, 1);
    T_ASSERT_EQ_I(out.records[1].maptype,    7);
    T_ASSERT_EQ_I(out.records[2].dungeon_id, 9);   /* "1-5" → 9 */
    T_ASSERT_EQ_I(out.records[2].maptype,    9);
    return 0;
}

int test_tables_stage_no_trailing_newline(void)
{
    /* Last line without trailing CRLF must still apply. */
    const char input[] =
        "stage:0-1\r\n"
        "maptype:42";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].maptype, 42);
    return 0;
}

int test_tables_stage_vendor_shape_minified(void)
{
    /* Mirrors the structure of the real vendor file in miniature: a
     * comment header, a stage:0-1 with several fields including
     * `/map:…` (commented-out), `sunpos:off` and `sunpos:N:N:N`
     * across consecutive stages, and an unknown ID at the end that
     * falls back to "1-16" via quirk #34. Each stage exercises a
     * different subset of the field dispatch chain.
     *
     * Validates the full thread: comments → headers → string fields
     * → multi-value parses → flag toggles → record threading. */
    const char input[] =
        "// stage table\r\n"
        "\r\n"
        "stage:0-1\r\n"
        "maptype:0\r\n"
        "mapviewarea:1000\r\n"
        "/map:xfile/town_disabled.x\r\n"   /* comment, NOT a map slot */
        "map:xfile/shop/shop_1st.x\r\n"
        "map:xfile/jutan/shop_jutan.x\r\n"
        "mapbg:xfile/sougen/sora.x\r\n"
        "minimap:bmp/map/minimap_town.bmp\r\n"
        "fishmap:fish/fishtown.bmp\r\n"
        "fog:20:500\r\n"
        "fogcolor:230:240:255\r\n"
        "startpos:0:0:0\r\n"
        "drawcode:2\r\n"
        "maplight:3\r\n"
        "lightdir:1.0:-1.0:-1.0\r\n"
        "lightcolor:0.0:0.0:0.0\r\n"
        "lightamb:0.0:0.0:0.2\r\n"
        "sunpos:off\r\n"
        "waterdrawcode:2\r\n"
        "waterfile:bmp/umi/umi_000\r\n"
        "wateranimnum:30\r\n"
        "wateranimspeed:2\r\n"
        "watersize:64\r\n"
        "wateralpha:80\r\n"
        "wateradd:0\r\n"
        "smallwater:\r\n"
        "hikaridrawcode:2\r\n"
        "hikarialpha:96\r\n"
        "hikariadd:1\r\n"
        "chrlightoffset:1.0\r\n"
        "\r\n"
        "stage:0-2\r\n"
        "maptype:0\r\n"
        "sunpos:1000:800000:-1000000\r\n"
        "maplight_dr:1.0 1.0\r\n"
        "maplight_ar:0.5 0.5\r\n"
        "\r\n"
        "stage:7-7\r\n"        /* unknown — falls back to dungeon_id 0x14 */
        "maptype:9\r\n";
    stage_state_t out;
    tables_parse_stage((const unsigned char *)input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 3);

    /* Record 0: town shop level 1. */
    const stage_record_t *r0 = &out.records[0];
    T_ASSERT_EQ_I(r0->dungeon_id,        0);
    T_ASSERT_EQ_I(r0->maptype,           0);
    T_ASSERT_EQ_I(r0->mapviewarea,       1000);
    T_ASSERT_EQ_I(r0->map_count,         2);
    T_ASSERT(strcmp(r0->map[0],          "xfile/shop/shop_1st.x")    == 0);
    T_ASSERT(strcmp(r0->map[1],          "xfile/jutan/shop_jutan.x") == 0);
    T_ASSERT(strcmp(r0->mapbg,           "xfile/sougen/sora.x")      == 0);
    T_ASSERT(strcmp(r0->minimap,         "bmp/map/minimap_town.bmp") == 0);
    T_ASSERT(strcmp(r0->fishmap,         "fish/fishtown.bmp")        == 0);
    T_ASSERT(r0->fog[0] ==  20.0f);
    T_ASSERT(r0->fog[1] == 500.0f);
    T_ASSERT_EQ_I(r0->fogcolor[0], 230);
    T_ASSERT_EQ_I(r0->fogcolor[1], 240);
    T_ASSERT_EQ_I(r0->fogcolor[2], 255);
    T_ASSERT_EQ_I(r0->startpos[0], 0);
    T_ASSERT_EQ_I(r0->startpos[1], 0);
    T_ASSERT_EQ_I(r0->startpos[2], 0);
    T_ASSERT_EQ_I(r0->drawcode,   2);
    T_ASSERT_EQ_I(r0->maplight,   3);
    T_ASSERT(r0->lightdir[0] ==  1.0f);
    T_ASSERT(r0->lightdir[1] == -1.0f);
    T_ASSERT(r0->lightdir[2] == -1.0f);
    T_ASSERT(r0->lightamb[2] ==  0.2f);
    T_ASSERT_EQ_I(r0->sunpos_mode,    STAGE_SUN_OFF);
    T_ASSERT_EQ_I(r0->waterdrawcode,  2);
    T_ASSERT(strcmp(r0->waterfile,    "bmp/umi/umi_000") == 0);
    T_ASSERT_EQ_I(r0->wateranimnum,   30);
    T_ASSERT_EQ_I(r0->wateranimspeed, 2);
    T_ASSERT_EQ_I(r0->watersize,      64);
    T_ASSERT_EQ_I(r0->wateralpha,     80);
    T_ASSERT_EQ_I(r0->wateradd,       0);
    T_ASSERT_EQ_I(r0->smallwater,     1);
    T_ASSERT_EQ_I(r0->hikaridrawcode, 2);
    T_ASSERT_EQ_I(r0->hikarialpha,    96);
    T_ASSERT_EQ_I(r0->hikariadd,      1);
    T_ASSERT(r0->chrlightoffset == 1.0f);

    /* Record 1: town shop level 2 (sunpos numeric + maplight_d/a). */
    const stage_record_t *r1 = &out.records[1];
    T_ASSERT_EQ_I(r1->dungeon_id, 1);
    T_ASSERT(r1->sun_pos[0] ==  1000.0f);
    T_ASSERT(r1->sun_pos[1] ==  800000.0f);
    T_ASSERT(r1->sun_pos[2] == -1000000.0f);
    T_ASSERT_EQ_I(r1->sunpos_mode, STAGE_SUN_SUNPOS);
    T_ASSERT(r1->maplight_d[0][0] == 1.0f);
    T_ASSERT(r1->maplight_d[0][1] == 1.0f);
    T_ASSERT(r1->maplight_a[0][0] == 0.5f);
    T_ASSERT(r1->maplight_a[0][1] == 0.5f);

    /* Record 2: unknown stage ID → dungeon_id = 0x14 (quirk #34). */
    const stage_record_t *r2 = &out.records[2];
    T_ASSERT_EQ_I(r2->dungeon_id, 0x14);
    T_ASSERT_EQ_I(r2->maptype,    9);

    return 0;
}
