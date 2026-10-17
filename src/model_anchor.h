/*
 * model_anchor.h — the model enemies' anchor points (FUN_00456d48 /
 * FUN_00404870), a state write of retail's dungeon render.  See model_anchor.c.
 */
#ifndef MODEL_ANCHOR_H
#define MODEL_ANCHOR_H

#include <stddef.h>
#include <stdint.h>

#include "actor_table.h"
#include "xfile.h"

/* reads a game file into a malloc'd buffer (*buf), returns its size (0: none) */
typedef size_t (*model_anchor_reader_fn)(const char *path, void **buf);
void model_anchor_set_reader(model_anchor_reader_fn fn);

/* FUN_00459dfd's two actor passes: every drawn model enemy's anchors */
void model_anchor_draw_pass(void);

/* FUN_00456d48's pose of an actor's model (the shared tree of its enemy
 * record's model, DAT_073da2f0 + 0x2f0 · model): NULL when it has none */
xfile_hier *model_anchor_pose(actor_t *a, int *model_out);
/* the model's .x (xfile2/…) */
const char *model_anchor_file(int model);

/* A hierarchical model's frame tree (FUN_00402551), read through the reader. */
xfile_hier *model_hier_load(const char *path);

/* FUN_00403d79 + FUN_00404870's frame update: every animated frame at the two
 * slots' per-track times (·4.8) cross-faded by `blend`, the root's matRot /
 * matTrans, then the combined matrices */
void model_hier_pose(xfile_hier *h, const int32_t *tr0, const int32_t *tr1, float blend,
                     const float rot[16], const float trans[16]);

#endif
