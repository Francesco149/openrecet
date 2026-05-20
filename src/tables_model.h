/*
 * tables_model.h — parser for `data/model.txt` (block #9 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `model.txt` defines the 3D model assets used by the engine: each
 * record maps a numeric index to an `.x` filename (`fname`) and a set
 * of up to 20 named attachment points (bone/socket identifiers used by
 * the rendering and animation code to position attachments such as
 * weapons, effects, or sub-meshes relative to a parent model).
 *
 * The engine allocates 20 fixed records (`&DAT_073ae258`, stride 0x2b8
 * bytes). Indices 9, 16, and 19 have no entries in the vendor file and
 * remain all-zero after parse.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_MODEL_H
#define OPENRECET_TABLES_MODEL_H

#include <stddef.h>
#include <stdint.h>

/* Total number of model records in the engine's fixed array. */
#define MODEL_DEF_COUNT 20

/* Slots per record (attachment/bone point names). */
#define MODEL_DEF_POINT_SLOTS 20

/* Per-field width: fname + each point name share this stride. */
#define MODEL_DEF_NAME_MAX 0x20

/*
 * One model definition record.  Mirrors the engine layout at
 * `&DAT_073ae258` (stride 0x2b8 bytes):
 *
 *   +0x000  fname[0x20]          — the .x filename (e.g. "golem_g01.x")
 *   +0x020  count (uint32_t)     — number of populated point slots
 *   +0x024  point[20][0x20]      — bone / attachment-point names
 *   +0x2a4  used[20]  (uint8_t)  — 1 if slot N has been written at least once
 *
 * Total: 0x2b8 (696) bytes per record, 20 records = 0x3660 bytes.
 */
typedef struct {
    char     fname[MODEL_DEF_NAME_MAX];                              /* +0x000 */
    uint32_t count;                                                  /* +0x020 */
    char     point[MODEL_DEF_POINT_SLOTS][MODEL_DEF_NAME_MAX];      /* +0x024 */
    uint8_t  used[MODEL_DEF_POINT_SLOTS];                           /* +0x2a4 */
} tables_model_t;                                                    /* = 0x2b8 */

_Static_assert(offsetof(tables_model_t, count) == 0x020,
               "tables_model_t.count offset must be 0x020");
_Static_assert(offsetof(tables_model_t, point) == 0x024,
               "tables_model_t.point offset must be 0x024");
_Static_assert(offsetof(tables_model_t, used)  == 0x2a4,
               "tables_model_t.used offset must be 0x2a4");
_Static_assert(sizeof(tables_model_t) == 0x2b8,
               "tables_model_t size must be 0x2b8");

/* Engine-global array, populated from src/tables.c. Tests use the
 * out-parameter form and leave g_models untouched. */
extern tables_model_t g_models[MODEL_DEF_COUNT];

/*
 * Parse a model.txt buffer into `out[MODEL_DEF_COUNT]`. Zero-inits the
 * array first. `data` is read as bytes; it does not need to be
 * null-terminated since `size` is authoritative.
 *
 * Line dispatch (engine: FUN_00475270 L1441–L1519):
 *   /…, blank    — comment / skipped (first byte '/', '\r', or '\n')
 *   no:N         — set current model index to atoi(line+3); N outside
 *                  [0, MODEL_DEF_COUNT) is clamped/skipped (safety divergence)
 *   fname:…      — copy line+6 into out[current].fname (cap 0x1f + NUL)
 *   NN:…         — two-digit slot prefix; copy line+3 into
 *                  out[current].point[slot] (cap 0x1f + NUL), set
 *                  used[slot]=1, increment count
 */
void tables_parse_model(const unsigned char *data, size_t size,
                        tables_model_t out[MODEL_DEF_COUNT]);

#endif /* OPENRECET_TABLES_MODEL_H */
