/*
 * scene1_dialogue.h — opening-prologue dialogue interpreter (the 0x46c cluster).
 *
 * Recettear's opening prologue (Tear wakes Recette, then the move downstairs)
 * is a scripted cutscene driven by a text event-script interpreter. The
 * engine compiles a `.ivt` script into a flat table of {handler, arg1, arg2}
 * command triplets, then walks that table one command per frame-or-burst,
 * revealing dialogue text char-by-char and waiting for the advance button.
 *
 * RE ground truth: docs/findings/opening-prologue.md (§RESOLVED — the .ivt
 * script language + interpreter impl map). Engine functions:
 *   FUN_0046ddea — the parser/compiler (text → command table). PORTED here.
 *   FUN_0046c295 — loader (build path, storage_read, call parser).  PORTED.
 *   FUN_0046c320 — per-frame update (reveal counter + command walk).  [next]
 *   FUN_0046c9a2 — per-frame draw + reveal-completion (anchors).       [next]
 *
 * The opening is a TWO-script sequence pinned via retail probe
 * (runs/intro-script-probe): iv1_1.ivt (16 msg, "WAKE UP, PLEASE!") then
 * iv1_2.ivt (30 msg, "Capitalism, ho!") = 46 dialogue lines = the 46
 * TEXT_ANIM_START/END anchors Phase 0 captured.
 *
 * This module replaces the scene1_intro_events.c STUB (a fake double-load
 * gate) with the real interpreter. Glyph rasterization + the D3D draw calls
 * in FUN_0046c9a2 are DEFERRED (structural-parity pass — see the findings
 * "defer boundary"): the box runs and fires the anchors, the text is not yet
 * rasterized.
 *
 * Pure C, no Win32 in the parser — scene1_dialogue_parse() operates on an
 * in-memory script buffer and is host-testable (tests/test_scene1_dialogue.c).
 * The loader (scene1_dialogue_load) pulls the script via the storage layer
 * (src/storage.h) and is engine-side.
 */
#ifndef OPENRECET_SCENE1_DIALOGUE_H
#define OPENRECET_SCENE1_DIALOGUE_H

#include <stdint.h>

/*
 * Command opcodes — one per engine handler stub. The engine stores a raw
 * function pointer in each triplet's slot 0 (the 0x46d8xx–0x46ddxx handlers,
 * computed-call-only stubs absent from the decompiled C); we store an opcode
 * instead — an equivalent, position-faithful representation of the same
 * table. Comments give the engine handler VA. Walk-loop return contract
 * (FUN_0046c320): a handler returns 0=stop-frame, 2=advance+yield-frame,
 * 3=special, other-nonzero=advance+run-next-same-frame; IVE_OP_END (fn==NULL)
 * terminates the program.
 */
enum ive_op {
    IVE_OP_END = 0,        /* fn == NULL — end of program (loop stops) */
    IVE_OP_COLOR,          /* 0x46d8d3  color:i:r,g,b   (clear-colour)  */
    IVE_OP_BG,             /* 0x46d912  bgset / polybg                  */
    IVE_OP_BGSCROLL,       /* 0x46d8a5  bgscroll:f                      */
    IVE_OP_RMB,            /* 0x46d926  rmb:a,b (screen-shake frames)   */
    IVE_OP_WINDOWSET,      /* 0x46d8c6  windowset                       */
    IVE_OP_WINDOWPOS,      /* 0x46d8e6  windowpos:x,y                   */
    IVE_OP_SKIP,           /* 0x46d8fc  skipon(0) / skipoff(1)          */
    IVE_OP_FADEIN,         /* 0x46dd2c  fadein:f:r,g,b,a                */
    IVE_OP_FADEOUT,        /* 0x46dd53  fadeout:f:r,g,b,a               */
    IVE_OP_LIGHTON,        /* 0x46dd7a  lighton:a:b                     */
    IVE_OP_LIGHTOFF,       /* 0x46ddb1  lightoff                        */
    IVE_OP_RMB2,           /* 0x46dd76  (4-char marker after fade)      */
    IVE_OP_WAIT,           /* 0x46dcd6  wait:n            (YIELD)       */
    IVE_OP_MUSIC,          /* 0x46dcef  music:n                         */
    IVE_OP_HOLDMUSIC,      /* 0x46dce3  holdmusic                       */
    IVE_OP_MFADEIN,        /* 0x46dd02  mfadein:n                       */
    IVE_OP_MFADEOUT,       /* 0x46dd15  mfadeout:n                      */
    IVE_OP_SE,             /* 0x46d885  se:<bin>                        */
    IVE_OP_CHR_DIR,        /* 0x46da1e  chr:N:dir:left/right            */
    IVE_OP_CHR_MOVE_X,     /* 0x46da33  chr:N:move:x (paired w/ _Y)     */
    IVE_OP_CHR_MOVE_Y,     /* 0x46dc0a  chr:N:move:,y                   */
    IVE_OP_CHR_MOVETO_X,   /* 0x46da6e  chr:N:moveto:x (paired w/ _Y)   */
    IVE_OP_CHR_MOVETO_Y,   /* 0x46dc30  chr:N:moveto:,y                 */
    IVE_OP_CHR_CENTER,     /* 0x46da59  chr:N:center:n                  */
    IVE_OP_CHR_SPEED,      /* 0x46dc45  chr:N:speed:f                   */
    IVE_OP_CHR_ANIM,       /* 0x46dc97  chr:N:anim / chr:N:grp(name)    */
    IVE_OP_CHR_FADEFRAME,  /* 0x46dc82  chr:N:fadeframe:n               */
    IVE_OP_CHR_BLEND,      /* 0x46dcac  normal_shade/normal_add/...     */
    IVE_OP_CHR_COL,        /* 0x46da83  chr:N:col:r,g,b,a               */
    IVE_OP_CHR_COLTO,      /* 0x46db20  chr:N:colto:r,g,b,a             */
    IVE_OP_CHR_DISP,       /* 0x46da09  chr:N:disp                      */
    IVE_OP_MSG_SPEAKER,    /* 0x46d9f3  msg:a:b — speaker/portrait a,b  */
    IVE_OP_MSG_SHOW,       /* 0x46d97b  show rows[a1 .. a1+a2)  (YIELD) */
    IVE_OP_MSG_WAITKEY,    /* 0x46d93c  <KEY> — wait for advance        */
    IVE_OP_MSG_CLEAR,      /* 0x46d9e1  <C> — clear the box             */
    IVE_OP__COUNT
};

/* One compiled command (the engine's 12-byte {fn,arg1,arg2} triplet). */
struct ive_cmd {
    uint8_t op;   /* enum ive_op */
    int32_t a1;   /* engine slot DAT_0735f4fc */
    int32_t a2;   /* engine slot DAT_0735f500 */
};

/* Capacities. The engine's tables are fixed regions; these bounds are
 * generous for the prologue scripts (iv1_2 = the largest: ~30 msg). If a
 * script overflows, the parser stops adding and sets `overflow` (verify the
 * true engine bound before trusting a script that hits these). */
#define IVE_MAX_CMDS   2048   /* command-table entries */
#define IVE_MAX_ROWS    512   /* dialogue text rows (DAT_073652b8 stride 0x100) */
#define IVE_ROW_BYTES   256   /* bytes per row (0x100) */
#define IVE_MAX_NAMES    64    /* bg/se/polybg/chrname table slots */
#define IVE_NAME_BYTES  256

/*
 * The compiled program. Mirrors the engine's parser outputs:
 *   cmds[]       — DAT_0735f4f8 command table
 *   glyph[][]    — DAT_073652b8 dialogue text rows (raw script bytes; SJIS)
 *   bg/se/polybg — name tables (DAT_07350df0 / DAT_0734b9b0 / DAT_0734fff0)
 * Effects of the visual/audio commands are DEFERRED; the names are captured
 * for faithful index assignment (the script references them by slot index).
 */
struct ive_program {
    struct ive_cmd cmds[IVE_MAX_CMDS];
    int   n_cmds;

    char  glyph[IVE_MAX_ROWS][IVE_ROW_BYTES];
    int   n_rows;             /* running row counter (engine local_20) */

    char  bg[IVE_MAX_NAMES][IVE_NAME_BYTES];
    int   n_bg;               /* DAT_073a3df0 */
    char  polybg[IVE_MAX_NAMES][IVE_NAME_BYTES];
    int   n_polybg;           /* DAT_073a3dfc */
    char  se[IVE_MAX_NAMES][IVE_NAME_BYTES];
    int   n_se;               /* DAT_0735dd80 */

    /* chr:N:grp named graphics (DAT_07357830 chrname + DAT_073a3ab8 dims). */
    char  chrname[IVE_MAX_NAMES][IVE_NAME_BYTES];
    int   chr_w[IVE_MAX_NAMES];
    int   chr_h[IVE_MAX_NAMES];
    int   n_chrname;          /* DAT_073a3df8 */

    int   overflow;           /* nonzero if a capacity was exceeded */
};

/*
 * scene1_dialogue_parse(text, prog) — mirror of FUN_0046ddea.
 *
 * Compiles a NUL-terminated `.ivt` script text into `prog`. `prog` is fully
 * overwritten (callers need not pre-zero). Returns 1 on success, 0 if the
 * script produced no commands (engine: parse-fail → dialogue-disabled).
 *
 * Pure: no globals, no Win32, no I/O. Host-testable.
 */
int scene1_dialogue_parse(const char *text, struct ive_program *prog);

/*
 * scene1_dialogue_load(scene, sub, prog) — mirror of the FUN_0046c295 loader
 * core. Builds "iv/iv%d_%d.ivt", reads it via the storage layer into a
 * scratch buffer, NUL-terminates, and parses into `prog`. Engine-side (needs
 * storage_init()). Returns scene1_dialogue_parse()'s result, or 0 if the
 * script could not be read.
 */
int scene1_dialogue_load(int scene, int sub, struct ive_program *prog);

#endif /* OPENRECET_SCENE1_DIALOGUE_H */
