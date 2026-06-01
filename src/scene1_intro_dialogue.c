/*
 * scene1_intro_dialogue.c — see the header. Drives iv1_1.ivt → iv1_2.ivt
 * through the scene1_dialogue_run interpreter and surfaces the TEXT_ANIM
 * anchor state.
 */
#include "scene1_intro_dialogue.h"

#include "scene1_dialogue.h"
#include "scene1_dialogue_run.h"

/* The opening prologue is scene selector 1, sub 1 then 2 (DAT_005c7a2c=1,
 * DAT_005c7a30 = 1 → 2). Pinned via runs/intro-script-probe. */
#define IVE_OPENING_SCENE 1
#define IVE_OPENING_SUB1  1
#define IVE_OPENING_SUB2  2

enum { D_IDLE = 0, D_SCRIPT1, D_SCRIPT2, D_DONE };

static int                g_state = D_IDLE;
static struct ive_program g_prog;   /* reused per script (~160 KiB) */
static struct ive_runtime g_rt;

void scene1_intro_dialogue_arm(void)
{
    g_state = D_SCRIPT1;
    g_rt.active   = 0;     /* lazy-load iv1_1 on the first tick */
    g_rt.complete = 0;
}

void scene1_intro_dialogue_reset(void)
{
    g_state = D_IDLE;
    g_rt.active   = 0;
    g_rt.complete = 0;
}

/* Load script `sub` of the opening scene and arm the interpreter on it.
 * Returns 1 on success; 0 (and the caller gives up) if the script is missing. */
static int start_script(int sub)
{
#ifdef _WIN32
    /* scene1_dialogue_load pulls the .ivt from the storage layer (Win32-only).
     * In the host test build there is no storage, so the driver stays dormant;
     * the interpreter itself is exercised directly in test_scene1_dialogue_run. */
    if (!scene1_dialogue_load(IVE_OPENING_SCENE, sub, &g_prog))
        return 0;
    ive_runtime_init(&g_rt, &g_prog);
    return 1;
#else
    (void)sub;
    return 0;
#endif
}

void scene1_intro_dialogue_tick(uint16_t held)
{
    switch (g_state) {
    case D_SCRIPT1:
        if (!g_rt.active && !g_rt.complete && !start_script(IVE_OPENING_SUB1)) {
            g_state = D_DONE;
            return;
        }
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete) {
            g_state = D_SCRIPT2;   /* next tick loads iv1_2 */
            g_rt.complete = 0;     /* clear so the script-2 lazy-load fires */
        }
        break;

    case D_SCRIPT2:
        if (!g_rt.active && !g_rt.complete && !start_script(IVE_OPENING_SUB2)) {
            g_state = D_DONE;
            return;
        }
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete)
            g_state = D_DONE;
        break;

    case D_IDLE:
    case D_DONE:
    default:
        break;
    }
}

int scene1_intro_dialogue_active(void)
{
    return ((g_state == D_SCRIPT1 || g_state == D_SCRIPT2) && g_rt.active) ? 1 : 0;
}

int32_t scene1_intro_dialogue_text_reveal(void)
{
    return scene1_intro_dialogue_active() ? g_rt.reveal : 0;
}

int scene1_intro_dialogue_text_revealed(void)
{
    return scene1_intro_dialogue_active() ? (g_rt.revealed ? 1 : 0) : 0;
}
