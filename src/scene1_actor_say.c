/*
 * scene1_actor_say.c — see scene1_actor_say.h.
 */
#include "scene1_actor_say.h"

#include <stdio.h>

#include "scene1_particles_tick.h"   /* g_scene1_actor_pos (DAT_056da1d8 + 3·i) */
#include "scene1_render.h"           /* scene1_project_world (FUN_00490c78) */
#include "speech_bubble.h"
#include "tables_item.h"             /* g_item, tables_item_find_slot_by_id (FUN_004681f6) */

static int s_arg;        /* DAT_0438b758 */
static int s_actor;      /* DAT_0438b75c */
static int s_side;       /* DAT_0438b760 */
static int s_age;        /* DAT_0438b764 */
static int s_duration;   /* DAT_0438b768 */
static int s_msg;        /* DAT_0438b76c */
static char s_buf[256];  /* DAT_074b2dc0 */

void actor_say_set(int msg, int arg, int actor, int side, int duration)
{
    s_age = 1;
    s_arg = arg;
    s_actor = actor;
    s_msg = msg;
    s_duration = duration;
    s_side = side;
}

void actor_say_stage_reset(void)
{
    s_age = 0;
    s_msg = 0;
}

void actor_say_stage_reset_age(void) { s_age = 0; }

int actor_say_age(void) { return s_age; }
int actor_say_msg(void) { return s_msg; }

/* item record singular name (DAT_095d381a + 0x2cc·slot); "" when the id has
 * no record (retail reads record -1). */
static const char *item_name(int slot)
{
    if (slot < 0 || slot >= g_item.count)
        return "";
    return g_item.records[slot].singular;
}

const char *actor_say_text(void)
{
    const char *fmt = NULL;
    if (s_msg != 0) {
        switch (s_msg) {
        case ACTOR_SAY_CANT_CARRY:   fmt = "Can't carry any more!"; break;
        case ACTOR_SAY_DID_WELL:     fmt = "You did well today."; break;
        case ACTOR_SAY_WHAT_IS_IT:   fmt = "Yes? What is it?"; break;
        case ACTOR_SAY_USED:
        case ACTOR_SAY_BROKEN:
        case ACTOR_SAY_WORN_OFF: {
            const char *f = s_msg == ACTOR_SAY_USED   ? "Used %s."
                          : s_msg == ACTOR_SAY_BROKEN ? "%s<BR>has broken!"
                                                      : "The %s<BR>has worn off!";
            snprintf(s_buf, sizeof s_buf, f,
                     item_name(tables_item_find_slot_by_id(&g_item, s_arg >> 6)));
            return s_buf;
        }
        case ACTOR_SAY_DAY_CLOSE_0R: fmt = "I'm tiiiired..."; break;
        case ACTOR_SAY_DAY_CLOSE_0T: fmt = "Go rest, then.<BR>Tomorrow will<BR>be busy."; break;
        case ACTOR_SAY_DAY_CLOSE_1R: fmt = "Thanks for all your<BR>help today, Tear!"; break;
        case ACTOR_SAY_DAY_CLOSE_1T: fmt = "You also did well.<BR>You look tired;<BR>get some rest."; break;
        case ACTOR_SAY_DAY_CLOSE_2R: fmt = "G'night!"; break;
        case ACTOR_SAY_DAY_CLOSE_2T: fmt = "Good night."; break;
        default:                     return "";   /* unk_005ce768 */
        }
        snprintf(s_buf, sizeof s_buf, "%s", fmt);
        return s_buf;
    }

    /* msg 0: the pickup line.  arg = item code (id << 6 | flags | +N). */
    int slot = tables_item_find_slot_by_id(&g_item, s_arg >> 6);
    int plus = s_arg & 0xf;
    if (s_arg & 0x10) {
        /* an unidentified pickup: name the base record — walk back to the
         * nearest record whose price is -1 (stops at slot 0). */
        plus = 0;
        if (slot >= 0 && slot < g_item.count && g_item.records[slot].price != -1) {
            for (;;) {
                --slot;
                if (slot <= 0)
                    break;
                if (g_item.records[slot].price == -1)
                    break;
            }
        }
    }
    if (plus > 0)
        snprintf(s_buf, sizeof s_buf, "Got an %s+%d!", item_name(slot), plus);
    else
        snprintf(s_buf, sizeof s_buf, "Got an %s!", item_name(slot));
    return s_buf;
}

void actor_say_tick(void)
{
    if (s_age == 0)
        return;
    const char *text = actor_say_text();
    if (s_age == 1)
        speech_bubble_open(412.0f, 112.0f, text);
    ++s_age;
    if (s_duration >= 0 && s_age >= s_duration) {
        s_age = 0;
        speech_bubble_close(1);
    }

    /* keep the bubble over the speaker: head height 1.5 (Tear 0.5), then
     * right of the head (side 0) or left with a mirrored tail (side 1). */
    int a = (s_actor >= 0 && s_actor < 3) ? s_actor : 0;
    float head = (s_actor == 2) ? 0.5f : 1.5f;
    float sx = 0.0f, sy = 0.0f;
#ifdef _WIN32
    scene1_project_world(g_scene1_actor_pos[a][0], head + g_scene1_actor_pos[a][1],
                         g_scene1_actor_pos[a][2], &sx, &sy, NULL);
#endif
    float px, py;
    if (s_side == 1) {
        speech_bubble_set_mirror(1, 0);
        py = sy - 16.0f;
        px = sx - 192.0f;
    } else {
        py = sy - 48.0f;
        px = sx + 48.0f;
    }
    speech_bubble_set_pos(px, py);
    speech_bubble_set_text_scale(0.4f);
}
