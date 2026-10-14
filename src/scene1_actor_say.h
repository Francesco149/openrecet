/*
 * scene1_actor_say.h — the "actor says a line" driver of the house/dungeon
 * controller: a message id + argument picked by the caller, shown in the
 * speech bubble (speech_bubble.h) above one of the three actors and closed
 * after a duration.
 *
 * Engine: FUN_00485413 (set), FUN_0048544a (text), FUN_00485861 (the
 * FUN_0048670f / FUN_0048b3f6 tail tick).  Globals DAT_0438b758 (arg: an
 * item code for the pickup lines), b75c (actor 0/1/2), b760 (bubble side:
 * 1 = left of the actor, mirrored tail), b764 (age; 1 = open this tick,
 * 0 = idle), b768 (duration, < 0 = until closed), b76c (message id).
 */
#ifndef OPENRECET_SCENE1_ACTOR_SAY_H
#define OPENRECET_SCENE1_ACTOR_SAY_H

/* Message ids (DAT_0438b76c) with fixed text. */
#define ACTOR_SAY_CANT_CARRY     1     /* "Can't carry any more!" */
#define ACTOR_SAY_DID_WELL       2     /* "You did well today." */
#define ACTOR_SAY_WHAT_IS_IT     3     /* "Yes? What is it?" */
#define ACTOR_SAY_USED           4     /* "Used %s." */
#define ACTOR_SAY_BROKEN         5     /* "%s<BR>has broken!" */
#define ACTOR_SAY_WORN_OFF       6     /* "The %s<BR>has worn off!" */
#define ACTOR_SAY_DAY_CLOSE_0R   300   /* day-close lines, variant 0..2, */
#define ACTOR_SAY_DAY_CLOSE_0T   301   /*   R = Recette, T = Tear        */
#define ACTOR_SAY_DAY_CLOSE_1R   302
#define ACTOR_SAY_DAY_CLOSE_1T   303
#define ACTOR_SAY_DAY_CLOSE_2R   304
#define ACTOR_SAY_DAY_CLOSE_2T   305

/* FUN_00485413(msg, arg, actor, side, duration). */
void actor_say_set(int msg, int arg, int actor, int side, int duration);

/* FUN_0048544a — the line for the current (msg, arg); "" for an unknown
 * id.  msg 0 = the item-pickup line built from arg. */
const char *actor_say_text(void);

/* FUN_00485861 — open the bubble on the first tick, count the duration,
 * keep the bubble over the actor (projected head + side offset). */
void actor_say_tick(void);

/* FUN_00436f97's reset: b764 = b76c = 0. */
void actor_say_stage_reset(void);

/* FUN_004852fb / FUN_004706ea: b764 = 0 (the message id is kept). */
void actor_say_stage_reset_age(void);

int actor_say_age(void);   /* DAT_0438b764 */
int actor_say_msg(void);   /* DAT_0438b76c */

#endif /* OPENRECET_SCENE1_ACTOR_SAY_H */
