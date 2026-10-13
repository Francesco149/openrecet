/* link stubs for P1b (functions unused by the tested paths) */
#include <stdint.h>
#include "tables_enemylist.h"
enemylist_state_t g_enemylist;
int32_t g_scene1_combat_stage_id;
void call_trace_enter(uint32_t va, const void *ret, int stub) { (void)va; (void)ret; (void)stub; }
void audio_trace_emit_fade_start(int c, int s, int32_t cb) { (void)c; (void)s; (void)cb; }
