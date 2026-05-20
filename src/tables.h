/*
 * tables.h — gameplay-table loader (FUN_00475270 — "init indexfile ok").
 *
 * Loads the 14 fixed asset files (`idx/stage.idx`, `idx/config.idx`,
 * `data/item.txt`, `data/kyaku.txt`, ...) plus the tutorial loop
 * (`data/tuto_%d.txt`) into engine globals during boot. See
 * docs/findings/tables-loader.md for the full file list and the
 * structural rationale for the one-per-file decomposition.
 *
 * Per-file global tables will be declared here as they get ported.
 * Current state: dispatcher + 14 stubs.
 */

#ifndef OPENRECET_TABLES_H
#define OPENRECET_TABLES_H

/*
 * tables_load_all() — mirrors FUN_00475270.
 *
 * Called once during boot, between layers_init and the (not-yet-ported)
 * font system init. Returns void: the original function has no failure
 * signaling. Per-file errors are reported via stderr and the load
 * continues — matching the engine's behavior of MessageBoxA + continue
 * on a missing or malformed file.
 */
void tables_load_all(void);

#endif /* OPENRECET_TABLES_H */
