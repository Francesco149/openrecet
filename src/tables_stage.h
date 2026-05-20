/*
 * tables_stage.h — parser for `idx/stage.idx` (block #1 of
 * FUN_00475270 / `tables_load_all`).
 *
 * stage.idx defines the per-scene environment for every gameplay
 * stage — the town shop interiors (`stage:0-1`..`0-4`) and the
 * 16 dungeon floors / cutscene-rooms (`stage:1-1`..`1-16`). Each
 * record is a flat key:value bag covering geometry (`map:`,
 * `mapbg:`, `startpos:`), camera (`mapcamera:`, `scroll:`), lighting
 * (`lightdir:`, `lightcolor:`, `lightamb:`, `maplight_dr/dg/db/ar/ag/ab:`),
 * water surfaces (`waterfile:`, `wateralpha:`, `watersize:`, …),
 * weather toggles (`windfire:`, `windsnow:`, `houshi:`, `smallwater:`),
 * fog/colour ramp (`fog:`, `fogcolor:`, `smokecolor:`, `backcolor:`),
 * and a handful of misc. integers (`maptype:`, `drawcode:`,
 * `mapviewarea:`, `farclip:`, `deathheight:`).
 *
 * Engine record array: `&DAT_068dd2f8`, stride **0x1b3c** (6972) bytes,
 * 21 records (one per dungeon-key match — see `STAGE_KEY_COUNT`).
 *
 * The engine has no per-record "is populated" flag beyond the
 * `dungeon_id` field, which is set when a `stage:X-Y` header
 * matches. We carry an explicit `count` here for the boot trace
 * and tests; the engine derives it from `local_10 + 1` at the
 * very end of the loop (`DAT_0438b8dc = (int)local_10 + 1`).
 *
 * Pure C, no Win32 surface — module compiles under host gcc for
 * sanitizer-instrumented unit testing alongside the rest of
 * `tables_*`.
 */

#ifndef OPENRECET_TABLES_STAGE_H
#define OPENRECET_TABLES_STAGE_H

#include <stddef.h>
#include <stdint.h>

/* Per-record string-field width. The engine uses 0x100 (256) bytes
 * for every string-typed field; the actual write loops cap at 0x100
 * data bytes before NUL-terminating in-place. We preserve the exact
 * widths so the byte layout matches the engine and overlong inputs
 * truncate identically. */
#define STAGE_NAME_MAX 0x100  /* 256 — engine literal */

/* `map:` slot count. The map[] array occupies bytes +0x314..+0x1714
 * within a record — 0x1400 bytes = 0x14 (20) slots of 0x100 each.
 * Engine has no overflow check (writes via `local_c * 0x100` offset
 * from `&DAT_068dd60c`); a 21st `map:` line would clobber the
 * minimap field at +0x1714. Dormant in vendor (max ~12 map: lines).
 * The port asserts and stops writing further entries past slot 19. */
#define STAGE_MAP_SLOTS 20

/* `mapcamera:` slot count. Capacity 0x200 / 0x100 = 2 slots between
 * +0x10c and +0x30c. Engine has no overflow check; a 3rd
 * `mapcamera:` line would clobber the `mapcamera_count` field at
 * +0x30c. Dormant in vendor. Port caps at slot 1. */
#define STAGE_MAPCAMERA_SLOTS 2

/* Stage-key count. The engine dispatches a `stage:X-Y` header line
 * against a 21-entry table of literal IDs ("0-1".."0-5", "1-1".."1-16")
 * via a sequential prefix-compare chain. Note the chain's default
 * `uVar5 = 0x14` collides with the last entry "1-16" — so an
 * **unknown** `stage:` value is treated as if it were "1-16" (engine
 * quirk #34). Vendor data hits exactly 20 of the 21 keys ("0-5" is
 * reserved but not used). */
#define STAGE_KEY_COUNT 21

/* `sunpos_mode` enum (offset +0x1a88).
 *
 * Sources are mutually-exclusive via this field — `sunpos:` sets mode=1,
 * `sunset:` sets mode=2, `sunpos:off` (or the bug-coverage path of
 * `sunset:off`) sets mode=0. The shared X/Y/Z coordinates at +0x1a7c
 * are overwritten by whichever of `sunpos:` / `sunset:` / `moonpos:`
 * fires last on a given record. `moonpos:` does NOT touch
 * sunpos_mode — see quirk #35. */
enum {
    STAGE_SUN_OFF    = 0,
    STAGE_SUN_SUNPOS = 1,
    STAGE_SUN_SUNSET = 2,
};

/*
 * One stage record. Mirrors the engine layout at base
 * `&DAT_068dd2f8`, stride 0x1b3c (6972) bytes. Field offsets
 * extracted from FUN_00475270 L55..L313 (record-init defaults)
 * and L3174..L3957 (field-key dispatcher).
 *
 * The string and slot fields have engine-fixed widths (STAGE_NAME_MAX,
 * STAGE_MAP_SLOTS, STAGE_MAPCAMERA_SLOTS); the int / float fields are
 * unpadded at their listed offsets. Trailing fields are tight against
 * the 0x1b3c stride boundary.
 */
typedef struct {
    /* +0x000 */ int32_t maptype;
    /* +0x004 */ char    mapbg[STAGE_NAME_MAX];                    /* "mapbg:" */
    /* +0x104 */ int32_t dungeon_id;                                /* set by stage:X-Y header */
    /* +0x108 */ int32_t mapbg_set;                                 /* side-effect of "mapbg:" */
    /* +0x10c */ char    mapcamera[STAGE_MAPCAMERA_SLOTS][STAGE_NAME_MAX]; /* "mapcamera:" */
    /* +0x30c */ int32_t mapcamera_count;
    /* +0x310 */ int32_t loopcamera;                                /* flag — "loopcamera:" */
    /* +0x314 */ char    map[STAGE_MAP_SLOTS][STAGE_NAME_MAX];      /* "map:" slots */
    /* +0x1714 */ char    minimap[STAGE_NAME_MAX];                  /* "minimap:" */
    /* +0x1814 */ char    fishmap[STAGE_NAME_MAX];                  /* "fishmap:" */
    /* +0x1914 */ int32_t startpos[3];                              /* "startpos:X:Y:Z" */
    /* +0x1920 */ char    waterfile[STAGE_NAME_MAX];                /* "waterfile:" */
    /* +0x1a20 */ int32_t wateranimnum;
    /* +0x1a24 */ int32_t wateranimspeed;
    /* +0x1a28 */ int32_t watersize;
    /* +0x1a2c */ int32_t map_count;                                /* incremented per "map:" line */
    /* +0x1a30 */ float   mapx;                                     /* atoi→float */
    /* +0x1a34 */ float   mapz;                                     /* atoi→float */
    /* +0x1a38 */ float   fog[2];                                   /* "fog:near:far" */
    /* +0x1a40 */ int32_t drawcode;
    /* +0x1a44 */ int32_t waterdrawcode;
    /* +0x1a48 */ int32_t wateralpha;
    /* +0x1a4c */ int32_t wateralpha_fish;
    /* +0x1a50 */ int32_t wateradd;
    /* +0x1a54 */ int32_t hikaridrawcode;
    /* +0x1a58 */ int32_t hikarialpha;
    /* +0x1a5c */ int32_t hikariadd;
    /* +0x1a60 */ int32_t farclip;
    /* +0x1a64 */ int32_t mapnumx;
    /* +0x1a68 */ int32_t mapnumz;
    /* +0x1a6c */ float   scroll;
    /* +0x1a70 */ float   mapposy;
    /* +0x1a74 */ float   waterheight;                              /* atoi→float */
    /* +0x1a78 */ int32_t mapviewarea;
    /* +0x1a7c */ float   sun_pos[3];                               /* shared sunpos/sunset/moonpos */
    /* +0x1a88 */ int32_t sunpos_mode;                              /* STAGE_SUN_* */
    /* +0x1a8c */ int32_t moonpos_set;                              /* set by "moonpos:" — independent of sunpos_mode */
    /* +0x1a90 */ int32_t fogcolor[3];                              /* "fogcolor:R:G:B" */
    /* +0x1a9c */ int32_t smokecolor[3];                            /* "smokecolor:R:G:B" */
    /* +0x1aa8 */ int32_t backcolor[3];
    /* +0x1ab4 */ float   lightdir[3];
    /* +0x1ac0 */ float   lightcolor[3];
    /* +0x1acc */ float   lightamb[3];
    /* +0x1ad8 */ int32_t gakecheck;                                /* flag — "gakecheck:" */
    /* +0x1adc */ float   chrlightoffset;
    /* +0x1ae0 */ int32_t maplight;
    /* +0x1ae4 */ int32_t chrlight;
    /* +0x1ae8 */ float   maplightspeed;
    /* +0x1aec */ float   maplight_d[3][2];                         /* dr / dg / db, space-separated pair */
    /* +0x1b04 */ float   maplight_a[3][2];                         /* ar / ag / ab, space-separated pair */
    /* +0x1b1c */ int32_t deathheight;
    /* +0x1b20 */ int32_t unk_b20;                                  /* init = 1, no key writes it; engine flag of unknown purpose */
    /* +0x1b24 */ int32_t windlerf;                                 /* flag */
    /* +0x1b28 */ int32_t windsnow;                                 /* flag */
    /* +0x1b2c */ int32_t houshi;                                   /* flag */
    /* +0x1b30 */ int32_t windbouble;                               /* flag */
    /* +0x1b34 */ int32_t windfire;                                 /* flag */
    /* +0x1b38 */ int32_t smallwater;                               /* flag */
} stage_record_t;                                                    /* = 0x1b3c (6972) */

_Static_assert(sizeof(stage_record_t) == 0x1b3c,        "stage record must be 6972 bytes");
_Static_assert(offsetof(stage_record_t, maptype)         == 0x000,  "maptype @ 0");
_Static_assert(offsetof(stage_record_t, mapbg)           == 0x004,  "mapbg @ 4");
_Static_assert(offsetof(stage_record_t, dungeon_id)      == 0x104,  "dungeon_id @ 0x104");
_Static_assert(offsetof(stage_record_t, mapbg_set)       == 0x108,  "mapbg_set @ 0x108");
_Static_assert(offsetof(stage_record_t, mapcamera)       == 0x10c,  "mapcamera @ 0x10c");
_Static_assert(offsetof(stage_record_t, mapcamera_count) == 0x30c,  "mapcamera_count @ 0x30c");
_Static_assert(offsetof(stage_record_t, loopcamera)      == 0x310,  "loopcamera @ 0x310");
_Static_assert(offsetof(stage_record_t, map)             == 0x314,  "map @ 0x314");
_Static_assert(offsetof(stage_record_t, minimap)         == 0x1714, "minimap @ 0x1714");
_Static_assert(offsetof(stage_record_t, fishmap)         == 0x1814, "fishmap @ 0x1814");
_Static_assert(offsetof(stage_record_t, startpos)        == 0x1914, "startpos @ 0x1914");
_Static_assert(offsetof(stage_record_t, waterfile)       == 0x1920, "waterfile @ 0x1920");
_Static_assert(offsetof(stage_record_t, wateranimnum)    == 0x1a20, "wateranimnum @ 0x1a20");
_Static_assert(offsetof(stage_record_t, map_count)       == 0x1a2c, "map_count @ 0x1a2c");
_Static_assert(offsetof(stage_record_t, mapx)            == 0x1a30, "mapx @ 0x1a30");
_Static_assert(offsetof(stage_record_t, fog)             == 0x1a38, "fog @ 0x1a38");
_Static_assert(offsetof(stage_record_t, drawcode)        == 0x1a40, "drawcode @ 0x1a40");
_Static_assert(offsetof(stage_record_t, farclip)         == 0x1a60, "farclip @ 0x1a60");
_Static_assert(offsetof(stage_record_t, waterheight)     == 0x1a74, "waterheight @ 0x1a74");
_Static_assert(offsetof(stage_record_t, sun_pos)         == 0x1a7c, "sun_pos @ 0x1a7c");
_Static_assert(offsetof(stage_record_t, sunpos_mode)     == 0x1a88, "sunpos_mode @ 0x1a88");
_Static_assert(offsetof(stage_record_t, moonpos_set)     == 0x1a8c, "moonpos_set @ 0x1a8c");
_Static_assert(offsetof(stage_record_t, fogcolor)        == 0x1a90, "fogcolor @ 0x1a90");
_Static_assert(offsetof(stage_record_t, lightdir)        == 0x1ab4, "lightdir @ 0x1ab4");
_Static_assert(offsetof(stage_record_t, lightamb)        == 0x1acc, "lightamb @ 0x1acc");
_Static_assert(offsetof(stage_record_t, chrlightoffset)  == 0x1adc, "chrlightoffset @ 0x1adc");
_Static_assert(offsetof(stage_record_t, maplight_d)      == 0x1aec, "maplight_d @ 0x1aec");
_Static_assert(offsetof(stage_record_t, maplight_a)      == 0x1b04, "maplight_a @ 0x1b04");
_Static_assert(offsetof(stage_record_t, deathheight)     == 0x1b1c, "deathheight @ 0x1b1c");
_Static_assert(offsetof(stage_record_t, unk_b20)         == 0x1b20, "unk_b20 @ 0x1b20");
_Static_assert(offsetof(stage_record_t, smallwater)      == 0x1b38, "smallwater @ 0x1b38");

/*
 * Aggregate state populated by `tables_parse_stage`.
 *
 * `records[i]` is one stage record; `count` is the number of valid
 * records (= matched `stage:X-Y` headers seen). The engine's
 * `DAT_0438b8dc` mirrors `count - 1 + 1 = count`. Records past
 * `count` are left in their post-init-defaults state (see
 * `stage_record_init_defaults` in tables_stage.c).
 */
typedef struct {
    stage_record_t records[STAGE_KEY_COUNT];
    int32_t        count;
} stage_state_t;

extern stage_state_t g_stage;

/*
 * Parse a stage.idx buffer into `out`.
 *
 * `out` is fully zero-initialised first; then each record-on-open
 * gets its engine defaults (`stage_record_init_defaults`). Records
 * past the actual stage count remain at zero (NOT default-init —
 * engine never touches them, so we don't either).
 *
 * The engine has side-effects after the stage loop completes
 * (player inventory / equip defaults at `&DAT_0438cc6c` etc.) —
 * those are unrelated game-state globals, not part of stage.idx
 * record state, so they are NOT performed here. They will move
 * to a dedicated boot-state init when the surrounding gameplay
 * subsystems get ported.
 */
void tables_parse_stage(const unsigned char *data, size_t size,
                        stage_state_t *out);

#endif /* OPENRECET_TABLES_STAGE_H */
