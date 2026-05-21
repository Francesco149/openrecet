/*
 * recet_ini.h — `recet.ini` reader (FUN_0047a474, the "pre-window init").
 *
 * Mirrors the engine's per-key GetPrivateProfileIntA storm: 28 input
 * bindings (2 controllers × (9 pad + 5 skill) shorts), 25 setup scalars,
 * 1 debug scalar, 2 config scalars, plus a `screen`→(width,height)
 * lookup table.
 *
 * The pure-C `recet_ini_parse()` is what runs under ASan in the unit
 * tests. The Win32 wrapper `recet_ini_load()` is a thin fread+parse.
 * Path discovery is `GetModuleFileNameA` + tail-strip + append (the
 * engine builds the same path via _splitpath + wsprintfA — same result).
 *
 * Boot order: must run BEFORE the main window is created (the engine
 * does — `screen` controls the requested back-buffer size).
 */

#ifndef OPENRECET_RECET_INI_H
#define OPENRECET_RECET_INI_H

#include <stdint.h>
#include <stddef.h>

#define RECET_INI_CONTROLLERS  2
#define RECET_INI_PAD_KEYS     9
#define RECET_INI_SKILL_KEYS   5

/*
 * Engine globals mirrored here. Each comment gives the original
 * DAT address (from FUN_0047a474 in docs/decompiled/by-address/47a474.c)
 * so future input/render code can be cross-checked against the engine
 * layout when it reads from here.
 */
struct recet_ini {
    /* [setup] section ─────────────────────────────────────────────── */
    int16_t pad[RECET_INI_CONTROLLERS][RECET_INI_PAD_KEYS];     /* 0x0438cce8 base, stride 0x1c */
    int16_t skill[RECET_INI_CONTROLLERS][RECET_INI_SKILL_KEYS]; /* 0x0438ccfa base, stride 0x1c */
    int aspect;       /* DAT_0438cce4 — default 1   ([setup] aspect)   */
    int winmode;      /* DAT_0438b164 — default 1   ([setup] winmode)  */
    int fps;          /* DAT_0438ccdc — default 0   ([setup] fps)      */
    int dispfps;      /* DAT_0438cce0 — default 0   ([setup] dispfps)  */
    int sfnouse;      /* DAT_0438b1b0 — default 0   ([setup] sfnouse)  */
    int texmode;      /* DAT_0438b174 — default 0                      */
    int mapmode;      /* DAT_0438b1ac — default 0                      */
    int demomode;     /* DAT_0438b1b4 — default 0                      */
    int usemipmap;    /* DAT_0438b178 — default 0                      */
    int usetree;      /* DAT_0438b17c — default 0                      */
    int uselighttex;  /* DAT_0438b180 — default 0                      */
    int texlevel;     /* DAT_0438b184 — default 0                      */
    int toorioff;     /* DAT_0438b188 — default 0                      */
    int windowpos;    /* DAT_0438b190 — default 0   (gates xpos/ypos saveback) */
    int winx;         /* DAT_0438b1a4 — default 0   ([setup] winx)     */
    int winy;         /* DAT_0438b1a8 — default 0   ([setup] winy)     */
    int nolight;      /* DAT_0438b194 — default 0                      */
    int nolight_s;    /* DAT_0438b198 — default 0                      */
    int easydisp;     /* DAT_0438b19c — default 0                      */
    int s_easydisp;   /* DAT_0438b1a0 — default 0                      */
    int bgnodisp;     /* DAT_0438b18c — NOT read; set to `easydisp` after loop (quirk) */
    int usefog;       /* DAT_0438cd60 — default 0                      */
    int screen;       /* raw value 0..3, dispatched into width/height  */
    int width;        /* DAT_005cbc04 — derived from `screen` (screen=0 → 640) */
    int height;       /* DAT_005cbc08 — derived from `screen` (screen=0 → 480) */

    /* [debug] section ─────────────────────────────────────────────── */
    int camfree;      /* DAT_0438cd5c — read twice in engine, same key both times (quirk) */

    /* [config] section ────────────────────────────────────────────── */
    int se;           /* DAT_0438ce7c — default 9, clamped to [0,9] */
    int mu;           /* DAT_0438ce80 — default 9, clamped to [0,9] */
};

/*
 * Hardcoded defaults, in the order pad00..pad08 then skill00..skill04,
 * for each of the 2 controllers. Sourced from the byte tables at
 * 0x005c81d8 (pad) and 0x005c8204 (skill) in the unpacked binary;
 * the engine adds 1 to each byte to get the default int value.
 *
 * These are exposed so unit tests can verify defaults without touching
 * private state, and so a future "reset to defaults" UI has somewhere
 * to read from.
 */
extern const int16_t recet_ini_pad_defaults[RECET_INI_CONTROLLERS][RECET_INI_PAD_KEYS];
extern const int16_t recet_ini_skill_defaults[RECET_INI_CONTROLLERS][RECET_INI_SKILL_KEYS];

/*
 * Apply all engine defaults to `out`, then parse `text` (a complete
 * recet.ini buffer of `len` bytes) and overwrite any matching keys.
 * Returns 0 on success. Pure C, no Win32 — safe under ASan.
 *
 * Semantics match GetPrivateProfileIntA for our key set:
 *   - section + key names are case-insensitive
 *   - leading/trailing whitespace around values is stripped
 *   - missing section/key/file → default applied
 *   - values parsed as signed decimal (atoi) — matches the engine
 *     since every recet.ini key is base-10 small int in practice
 */
int recet_ini_parse(const char *text, size_t len, struct recet_ini *out);

/* Apply engine defaults to `out` without parsing anything. Useful for
 * tests + for the "ini file missing" path (engine returns all defaults
 * silently in that case). */
void recet_ini_set_defaults(struct recet_ini *out);

/*
 * Map a raw `screen` value to (width, height). The engine's lookup:
 *   0 → 640×480   1 → 800×600   2 → 1024×768   default (incl. 3) → 1280×960
 */
void recet_ini_resolution(int screen, int *width, int *height);

#ifdef _WIN32
/*
 * Build the default ini path next to the running exe (mirrors the
 * engine's _splitpath + wsprintfA dance). Writes a null-terminated
 * absolute path to `buf` (caller provides MAX_PATH-sized buffer).
 * Returns 0 on success, -1 if GetModuleFileNameA fails.
 */
int recet_ini_default_path(char *buf, size_t buf_size);

/*
 * Load + parse the ini at `path` into `out`. If the file is missing
 * or unreadable, returns 0 and `out` holds the engine defaults
 * (matching GetPrivateProfileIntA's silent-default behavior).
 * Returns -1 only on out-of-memory / unexpected I/O error.
 */
int recet_ini_load(const char *path, struct recet_ini *out);
#endif

#endif /* OPENRECET_RECET_INI_H */
