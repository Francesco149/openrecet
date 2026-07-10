/*
 * roster_golden_replay.h — headless golden-replay gate for the roster
 * scan (cs_roster_scan / customer_service_session_init general branch).
 *
 * When the env var OPENRECET_ROSTER_GOLDEN names a captured working-arena
 * snapshot (roster_scan_capture.py's <out>.arena.bin, SAVE_BANK_STRIDE_BYTES
 * bytes = the engine DAT_044e3798 slot-0 body), this runs the ported scan for
 * a seed sweep on that exact input and writes a JSON fixture identical in
 * shape to the retail golden — the binary bit-exact gate for the port.
 *
 * Env:
 *   OPENRECET_ROSTER_GOLDEN  path to the arena.bin (required to activate)
 *   OPENRECET_ROSTER_SEEDS   comma list of u32 seeds (default "1")
 *   OPENRECET_ROSTER_OUT     output JSON path (default "roster_port_out.json")
 *
 * Called once from WinMain after tables_load_all()+save_bank_init_all().
 * If the env var is unset it returns immediately (no effect on the game).
 * When active it runs the sweep, writes the JSON, and exits the process. */
#ifndef OPENRECET_ROSTER_GOLDEN_REPLAY_H
#define OPENRECET_ROSTER_GOLDEN_REPLAY_H

void roster_golden_replay_maybe(void);

#endif /* OPENRECET_ROSTER_GOLDEN_REPLAY_H */
