/*
 * model_anchor.h — the model enemies' anchor points (FUN_00456d48 /
 * FUN_00404870), a state write of retail's dungeon render.  See model_anchor.c.
 */
#ifndef MODEL_ANCHOR_H
#define MODEL_ANCHOR_H

#include <stddef.h>

/* reads a game file into a malloc'd buffer (*buf), returns its size (0: none) */
typedef size_t (*model_anchor_reader_fn)(const char *path, void **buf);
void model_anchor_set_reader(model_anchor_reader_fn fn);

/* FUN_00459dfd's two actor passes: every drawn model enemy's anchors */
void model_anchor_draw_pass(void);

#endif
