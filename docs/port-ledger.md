# OpenRecet — port ledger

> **DERIVED FILE** — regenerate with `python3 tools/gen_port_ledger.py`.
> See `STATUS.md` for the headline.

Per-engine-function lifecycle, two independent axes: an **INVENTORY** rung
derived from `src/` markers (`FUN_` / `PORT-OF` / `CALL_TRACE_ENTER`) and a
**RUNTIME** rung that needs runtime evidence (none bound since the
2026-10-13 methodology reset). A `src/` marker is INVENTORY, never parity.

## Summary

- non-thunk engine functions: **2548** (of 2620 incl. thunks)
- **inventory:** instrumented **90** (80 full + 10 stub), implemented 0, source-referenced 682, discovered 1776 → 772 referenced-or-better (30.3%)
- **runtime:** 0 proven (0.0%) — no runtime evidence bound
- orphan refs (in src/, not in function table): 10

## instrumented (90) — CALL_TRACE_ENTER(_STUB) probe wired (INVENTORY — not runtime-proven)

| VA | name | size | call-target | flags | src |
|----|------|-----:|:-----------:|-------|-----|
| 0x405552 | FUN_00405552 | 498 | ✓ | stub | debug_param_tick.c, debug_param_tick.h, sim.c |
| 0x4060ff | FUN_004060ff | 90 | ✓ |  | scene.c, scene.h, scene1_postload.c (+8) |
| 0x406241 | FUN_00406241 | 390 | ✓ |  | render_quad.c, render_quad.h, scene1_top_hud.c (+1) |
| 0x4063c7 | FUN_004063c7 | 394 | ✓ |  | nowloading.c, render_quad.c, render_quad.h |
| 0x406d50 | FUN_00406d50 | 1445 | ✓ |  | main.c, scene1_hud.c, scene1_top_hud.c (+2) |
| 0x409925 | FUN_00409925 | 3434 | ✓ |  | customer_service.h, customer_service_render.c, scene1_counter_menu.c (+7) |
| 0x40a765 | FUN_0040a765 | 7558 | ✓ |  | customer_service.h, customer_service_render.c, main.c (+20) |
| 0x40fb3a | FUN_0040fb3a | 8071 | ✓ |  | main.c, scene1_particles_tick.c, scene1_particles_tick.h (+9) |
| 0x4141c0 | FUN_004141c0 | 389 | ✓ | stub | scene1_hud.c, scene1_hud.h |
| 0x414929 | FUN_00414929 | 1465 | ✓ |  | scene1_particles_tick.c, scene1_particles_tick.h, scene1_per_frame_open.c (+2) |
| 0x41f252 | FUN_0041f252 | 113 | ✓ |  | actor_spawn.c, actor_spawn.h, scene1_particles_tick.h (+2) |
| 0x43195d | FUN_0043195d | 51 | ✓ |  | actor_ai.c, diff_entry.c, diff_entry.h (+7) |
| 0x431990 | FUN_00431990 | 70 | ✓ |  | diff_entry.c, diff_entry.h, record_hit.c (+2) |
| 0x4319d6 | FUN_004319d6 | 170 | ✓ |  | dungeon_flow.c, enemy_place.h, floor_layout.h (+8) |
| 0x43244c | FUN_0043244c | 39 | ✓ |  | map_collision.h, scene1_preload.c, stage_palette.c (+1) |
| 0x434d6a | FUN_00434d6a | 85 | ✓ |  | scene1_player_ctrl.c, scene_guild.c, scene_pause.c (+3) |
| 0x435117 | FUN_00435117 | 615 | ✓ |  | scene1_hud.c, scene_adv_guild.c, scene_guild.c (+3) |
| 0x43537e | FUN_0043537e | 660 | ✓ | stub | choice_box.c, choice_box.h, customer_service_render.c (+8) |
| 0x4356cd | FUN_004356cd | 67 | ✓ |  | dungeon_results.c, scene_pause.c, scene_title.c (+4) |
| 0x435747 | FUN_00435747 | 300 | ✓ |  | choice_box.c, choice_box.h, main.c (+8) |
| 0x435c98 | FUN_00435c98 | 309 | ✓ |  | chara_equip.h, chara_skills.h, dungeon_results.c (+10) |
| 0x435dcd | FUN_00435dcd | 494 | ✓ |  | record_hit.c, stage_post_load.c, stage_post_load.h |
| 0x435fbb | FUN_00435fbb | 224 | ✓ |  | record_hit.c, scene1_postload.c, stage_post_load.c (+1) |
| 0x4360b6 | FUN_004360b6 | 202 | ✓ |  | chara_skills.c, chara_skills.h, dungeon_party.c (+4) |
| 0x43ae20 | FUN_0043ae20 | 25750 | ✓ |  | scene1_combat_sm.h, scene1_player_ctrl.h, scene1_preload.c (+5) |
| 0x4427d3 | FUN_004427d3 | 30 | ✓ | stub | actor_ai.c, scene1_particles_tick.h, scene1_sim.c (+4) |
| 0x44284b | FUN_0044284b | 1083 | ✓ |  | scene1_records_c_tick.c, scene1_records_c_tick.h, scene1_sim.c (+2) |
| 0x442cef | FUN_00442cef | 2490 | ✓ |  | actor_ai.h, customer_service.c, dungeon_flow.h (+8) |
| 0x451874 | FUN_00451874 | 47 | ✓ |  | actor_ai.c, audio.h, audio_mci.c (+19) |
| 0x4523e6 | FUN_004523e6 | 387 | ✓ |  | main.c, scene1_fps.c, scene1_fps.h |
| 0x4526f5 | FUN_004526f5 | 276 | ✓ |  | customer_service.c, dungeon_flow.c, dungeon_results.c (+16) |
| 0x45281c | FUN_0045281c | 151 | ✓ |  | customer_service.c, dungeon_flow.c, dungeon_results.c (+16) |
| 0x4528b3 | FUN_004528b3 | 47 | ✓ |  | customer_service.c, dungeon_flow.c, dungeon_results.c (+11) |
| 0x452917 | FUN_00452917 | 38 | ✓ |  | customer_service.c, main.c, scene.h (+2) |
| 0x452cde | FUN_00452cde | 41 | ✓ |  | dungeon_results.c, music.c, scene.c (+13) |
| 0x453147 | FUN_00453147 | 362 | ✓ |  | nowloading.c, nowloading.h, render_quad.h (+1) |
| 0x4532df | FUN_004532df | 129 | ✓ |  | scene_pause.c, sim.c, sim.h |
| 0x453e8f | FUN_00453e8f | 444 | ✓ |  | fade.c, fade.h, nowloading.h |
| 0x45404b | FUN_0045404b | 326 | ✓ | stub | main.c, scene1_fx_overlays.c, scene1_fx_overlays.h (+3) |
| 0x454191 | FUN_00454191 | 1391 | ✓ |  | main.c, scene.h, scene1_fx_overlays.c (+4) |
| 0x4547ab | FUN_004547ab | 1670 | ✓ |  | input.c, input.h, main.c (+25) |
| 0x4552d0 | FUN_004552d0 | 5210 | ✓ |  | scene1_bg_npc.h, scene1_chr_walker.h, scene1_player_ctrl.c (+7) |
| 0x457714 | FUN_00457714 | 5323 | ✓ |  | scene1_alpha_walker.c, scene1_alpha_walker.h, scene1_emit_record.h (+6) |
| 0x459dfd | FUN_00459dfd | 1906 | ✓ | stub | main.c, mesh_draw.c, mesh_draw.h (+14) |
| 0x45a56f | FUN_0045a56f | 1223 | ✓ |  | chr_sprite_meta.h, scene1_bg_npc.c, scene1_bg_npc.h (+8) |
| 0x45aa36 | FUN_0045aa36 | 4493 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_chr_shadow.c (+3) |
| 0x45bbf9 | FUN_0045bbf9 | 134 | ✓ |  | main.c, mesh_draw.c, mesh_draw.h (+3) |
| 0x45bc7f | FUN_0045bc7f | 107 | ✓ |  | floor_layout.c, floor_layout.h, scene_adv_guild.c |
| 0x4681f6 | FUN_004681f6 | 42 | ✓ |  | chara_equip.h, customer_roster.c, customer_service.c (+15) |
| 0x4682d0 | FUN_004682d0 | 8 | ✓ |  | customer_service.c, dungeon_results.c, scene.c (+9) |
| 0x4693e3 | FUN_004693e3 | 41 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene1_player_ctrl.c (+4) |
| 0x46f1e9 | FUN_0046f1e9 | 186 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_postload.c (+1) |
| 0x46f2a3 | FUN_0046f2a3 | 894 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_day_close.h (+2) |
| 0x46f621 | FUN_0046f621 | 39 | ✓ |  | input_segtrace.h, main.c, scene1_bg_npc.c (+3) |
| 0x46f648 | FUN_0046f648 | 239 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_chr_shadow.c (+1) |
| 0x46f737 | FUN_0046f737 | 347 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_shop_walker.c |
| 0x470385 | FUN_00470385 | 246 | ✓ |  | scene1_bg_npc.c, scene1_bg_npc.h, scene1_chr_shadow.c (+2) |
| 0x4705a3 | FUN_004705a3 | 327 | ✓ |  | scene1_bg_npc.h, scene1_shop_walker.c, scene1_shop_walker.h |
| 0x471905 | FUN_00471905 | 54 | ✓ |  | d3d_pool.c, d3d_pool.h, dungeon_results.c (+1) |
| 0x47281e | FUN_0047281e | 24 | ✓ |  | mesh_load.c, scene1_preload.c, scene1_preload.h |
| 0x473474 | FUN_00473474 | 9 | ✓ |  | d3d_pool.c, d3d_pool.h, scene.c |
| 0x474681 | FUN_00474681 | 123 | ✓ |  | map_collision.h, scene1_preload.c, scene1_preload.h (+5) |
| 0x474a9a | FUN_00474a9a | 760 | ✓ |  | main.c, scene.c, scene1_alpha_walker.c (+16) |
| 0x47a8c0 | FUN_0047a8c0 | 368 | ✓ |  | dungeon_party.c, save_bank.c, save_bank.h (+2) |
| 0x47b73c | FUN_0047b73c | 1779 | ✓ |  | input.c, input.h, sim.c (+1) |
| 0x47be2f | FUN_0047be2f | 99 | ✓ |  | tick.c, tick.h |
| 0x47c29d | FUN_0047c29d | 215 | ✓ |  | font.c, font.h, sim.c |
| 0x47d622 | FUN_0047d622 | 1952 | ✓ |  | floor_layout.c, floor_layout.h |
| 0x47ddc2 | FUN_0047ddc2 | 395 | ✓ |  | floor_layout.c, floor_layout.h |
| 0x47df4d | FUN_0047df4d | 1980 | ✓ |  | floor_layout.c, floor_layout.h, scene1_postload.c (+3) |
| 0x48093f | FUN_0048093f | 136 | ✓ |  | chara_equip.c, chara_equip.h, customer_service.c (+3) |
| 0x48407f | FUN_0048407f | 795 | ✓ |  | dungeon_party.c, dungeon_party.h, scene1_companion_ctrl.h (+5) |
| 0x4844ef | FUN_004844ef | 310 | ✓ |  | chara_equip.c, chara_equip.h, dungeon_party.c (+6) |
| 0x4850ec | FUN_004850ec | 18 | ✓ | stub | anchor_trace.c, scene1_player_ctrl.c, scene1_player_ctrl.h (+2) |
| 0x485861 | FUN_00485861 | 280 | ✓ |  | customer_service.c, customer_service.h, dungeon_flow.c (+4) |
| 0x48a331 | FUN_0048a331 | 23 | ✓ |  | dungeon_party.c, stage_post_load.h, xp_curve.c (+1) |
| 0x48b3f6 | FUN_0048b3f6 | 663 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_actor_say.h (+2) |
| 0x48b850 | FUN_0048b850 | 5030 | ✓ | stub | collision_resolve.c, dungeon_flow.c, dungeon_flow.h (+15) |
| 0x48fe43 | FUN_0048fe43 | 315 | ✓ |  | main.c, scene1_dungeon_clear_banner.c, scene1_dungeon_clear_banner.h (+1) |
| 0x48ff93 | FUN_0048ff93 | 70 | ✓ |  | chara_equip.h, save_bank.c |
| 0x48ffd9 | FUN_0048ffd9 | 67 | ✓ |  | save_bank.c, scene1_postload.c, scene1_postload.h (+1) |
| 0x49001c | FUN_0049001c | 422 | ✓ |  | save_bank.c, save_bank.h, scene.c (+2) |
| 0x49065b | FUN_0049065b | 314 | ✓ | stub | actor_ai.c, actor_ai.h, scene1_hud.c (+3) |
| 0x490e56 | FUN_00490e56 | 494 | ✓ |  | npc_schedule.c, npc_schedule.h, scene.c (+1) |
| 0x499583 | FUN_00499583 | 231 | ✓ |  | audio.c, audio.h, audio_fade.c (+11) |
| 0x49966a | FUN_0049966a | 1412 | ✓ |  | audio.c, audio.h, audio_fade.h (+7) |
| 0x49a558 | FUN_0049a558 | 35 | ✓ |  | music.c, music.h |
| 0x49b425 | FUN_0049b425 | 207 | ✓ |  | main.c, render_quad.c, render_quad.h (+11) |
| 0x49c644 | FUN_0049c644 | 3233 | ✓ | stub | anchor_trace.c, anchor_trace.h, save_picker.h (+4) |
| 0x49de18 | FUN_0049de18 | 8 | ✓ |  | nowloading.h, scene.c, scene.h (+3) |

## implemented (0) — PORT-OF(0xVA) attestation, no probe (INVENTORY)

| VA | name | size | call-target | flags | src |
|----|------|-----:|:-----------:|-------|-----|

## source-referenced (682) — a FUN_<va> mention in src/ — NOT a port claim

| VA | name | size | call-target | flags | src |
|----|------|-----:|:-----------:|-------|-----|
| 0x40110f | FUN_0040110f | 27 | ✓ |  | main.c |
| 0x40112a | FUN_0040112a | 20 | ✓ |  | main.c |
| 0x40120c | FUN_0040120c | 116 | ✓ |  | main.c, scene1_camera.c, scene1_camera.h (+1) |
| 0x4038e4 | FUN_004038e4 | 90 | ✓ |  | layers.c, layers.h |
| 0x403d79 | FUN_00403d79 | 186 | ✓ |  | scene1_render.c, scene1_shop_walker.c, scene1_shop_walker.h |
| 0x403eb7 | FUN_00403eb7 | 108 | ✓ |  | scene1_render.c, scene1_render.h, scene1_shop_walker.h |
| 0x404209 | FUN_00404209 | 759 | ✓ |  | scene1_render.c, scene1_render.h |
| 0x404757 | FUN_00404757 | 117 | ✓ |  | scene1_render.h, scene1_shop_walker.h |
| 0x4047df | FUN_004047df | 135 | ✓ |  | scene1_render.c, scene1_render.h, scene1_shop_walker.h |
| 0x404866 | FUN_00404866 | 10 | ✓ |  | scene1_shop_walker.c, scene1_shop_walker.h |
| 0x404870 | FUN_00404870 | 432 | ✓ |  | scene1_render.c, scene1_shop_walker.c, scene1_shop_walker.h |
| 0x404a20 | FUN_00404a20 | 45 | ✓ |  | scene1_render.c, scene1_render.h, scene1_shop_walker.c (+2) |
| 0x404bb8 | FUN_00404bb8 | 84 | ✓ |  | math3d.h, scene1_camera.c, scene1_camera.h (+4) |
| 0x404e44 | FUN_00404e44 | 29 | ✓ |  | prewindow.c, prewindow.h, render_quad.c (+1) |
| 0x404e61 | FUN_00404e61 | 55 | ✓ |  | encyclopedia.c, render_quad.c, render_quad.h (+1) |
| 0x404e98 | FUN_00404e98 | 100 | ✓ |  | render_quad.c, render_quad.h |
| 0x404efc | FUN_00404efc | 562 | ✓ |  | customer_service_render.c, render_quad.c, render_quad.h (+6) |
| 0x405354 | FUN_00405354 | 76 | ✓ |  | customer_service_render.c, font_draw.c, render_quad.c (+5) |
| 0x4054c0 | FUN_004054c0 | 146 | ✓ |  | font_draw.h |
| 0x405744 | FUN_00405744 | 373 |  |  | debug_param_tick.h |
| 0x405a52 | FUN_00405a52 | 162 | ✓ |  | scene1_dialogue_draw.c, scene1_dialogue_run.c |
| 0x405b1a | FUN_00405b1a | 598 | ✓ |  | scene1_render.c, scene1_render.h |
| 0x405d70 | FUN_00405d70 | 911 | ✓ |  | scene1_render.c, scene1_render.h, stage_palette.h |
| 0x406159 | FUN_00406159 | 185 | ✓ |  | customer_service.c, speech_bubble.c, speech_bubble.h |
| 0x406212 | FUN_00406212 | 29 | ✓ |  | scene1_player_ctrl.c, speech_bubble.c, speech_bubble.h |
| 0x40622f | FUN_0040622f | 18 | ✓ |  | scene_day_end.c, speech_bubble.c, speech_bubble.h |
| 0x406551 | FUN_00406551 | 29 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_postload.c (+1) |
| 0x40656e | FUN_0040656e | 22 | ✓ |  | scene1_per_frame_open.c, scene1_per_frame_open.h, scene1_top_hud.c (+1) |
| 0x406584 | FUN_00406584 | 1017 | ✓ |  | dungeon_flow.h, dungeon_party.c, dungeon_party.h (+10) |
| 0x40697d | FUN_0040697d | 19 | ✓ |  | speech_bubble.c, speech_bubble.h |
| 0x406990 | FUN_00406990 | 11 | ✓ |  | speech_bubble.c, speech_bubble.h |
| 0x4069a6 | FUN_004069a6 | 21 | ✓ |  | speech_bubble.c, speech_bubble.h |
| 0x406a60 | FUN_00406a60 | 516 | ✓ |  | save_picker.c, scene1_top_hud.c, scene1_top_hud.h (+2) |
| 0x4072f5 | FUN_004072f5 | 1983 | ✓ |  | chara_skills.h |
| 0x407ab4 | FUN_00407ab4 | 504 | ✓ |  | scene1_merchant_hud.c, scene1_top_hud.c |
| 0x40a68f | FUN_0040a68f | 214 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x40c4eb | FUN_0040c4eb | 1059 | ✓ |  | main.c, scene1_hud.c, speech_bubble.c (+1) |
| 0x40c90e | FUN_0040c90e | 20 | ✓ |  | scene1_spawn.c |
| 0x40c923 | FUN_0040c923 | 23 | ✓ |  | dungeon_party.c |
| 0x40cc87 | FUN_0040cc87 | 378 | ✓ |  | main.c, movie.h, scene_title.c |
| 0x40ce0d | FUN_0040ce0d | 153 | ✓ |  | movie.h, sim.c |
| 0x40cea6 | FUN_0040cea6 | 226 | ✓ |  | movie.c, movie.h, scene_title.c (+1) |
| 0x40cf88 | FUN_0040cf88 | 403 | ✓ |  | movie.c, music.c |
| 0x40d11b | FUN_0040d11b | 23 | ✓ |  | scene1_bg_npc.c, scene1_chr_shadow.c, scene1_shop_walker.c |
| 0x40d132 | FUN_0040d132 | 9497 | ✓ |  | actor_spawn.h, scene1_overlay.h, scene1_overlay_helpers.c (+2) |
| 0x40f64b | FUN_0040f64b | 128 | ✓ |  | actor_ai.c, scene1_per_frame_open.h, scene1_postload.c (+5) |
| 0x41276e | FUN_0041276e | 795 |  |  | scene1_per_frame_open.c, scene1_per_frame_open.h |
| 0x412a89 | FUN_00412a89 | 490 | ✓ |  | main.c, scene1_overlay.c, scene1_overlay.h (+2) |
| 0x4132c1 | FUN_004132c1 | 92 | ✓ |  | customer_service.c, scene1_per_frame_open.h |
| 0x41331d | FUN_0041331d | 89 | ✓ |  | actor_ai.c, dungeon_party.c, record_hit.c (+2) |
| 0x414345 | FUN_00414345 | 1057 | ✓ |  | actor_ai.c, customer_service.c, scene1_overlay.c (+3) |
| 0x414766 | FUN_00414766 | 111 | ✓ |  | customer_service.c |
| 0x4147d5 | FUN_004147d5 | 62 | ✓ |  | actor_ai.c, dungeon_party.c, party.h (+4) |
| 0x414813 | FUN_00414813 | 239 | ✓ |  | scene1_overlay.c |
| 0x414902 | FUN_00414902 | 39 | ✓ |  | scene1_per_frame_open.c, scene1_per_frame_open.h, scene1_records.c (+1) |
| 0x414ee2 | FUN_00414ee2 | 4006 | ✓ |  | scene1_overlay.c, scene1_overlay.h, scene1_overlay_helpers.c (+4) |
| 0x415e90 | FUN_00415e90 | 36 | ✓ |  | scene1_overlay.c, scene1_overlay.h |
| 0x415f2e | FUN_00415f2e | 125 | ✓ |  | scene1_wide_followup.h, scene1_wide_followup_helpers.c |
| 0x415fab | FUN_00415fab | 540 | ✓ |  | save_bank.h, scene1_wide_followup.c, scene1_wide_followup.h (+1) |
| 0x4161c7 | FUN_004161c7 | 4925 | ✓ |  | save_bank.h, scene1_pass_f.c, scene1_pass_f.h (+7) |
| 0x417504 | FUN_00417504 | 506 | ✓ |  | main.c, scene1_hud.h, scene1_render.c (+1) |
| 0x4176ff | FUN_004176ff | 30395 | ✓ |  | scene1_maplight.c, scene1_render.c, scene1_render.h (+4) |
| 0x41edf1 | FUN_0041edf1 | 35 | ✓ |  | worker_load.h |
| 0x41ee24 | FUN_0041ee24 | 365 | ✓ |  | sim.c |
| 0x41f319 | FUN_0041f319 | 340 | ✓ |  | actor_table.h, scene1_combat_sm.h |
| 0x41f46d | FUN_0041f46d | 57 | ✓ |  | record_hit.h, scene1_combat_sm.h |
| 0x41f4a6 | FUN_0041f4a6 | 690 | ✓ |  | actor_table.h |
| 0x41f759 | FUN_0041f759 | 48 | ✓ |  | actor_ai.c |
| 0x41f789 | FUN_0041f789 | 378 | ✓ |  | actor_ai.c |
| 0x41f903 | FUN_0041f903 | 308 | ✓ |  | actor_ai.c, actor_table.h, dungeon_flow.c (+2) |
| 0x41fa37 | FUN_0041fa37 | 55 | ✓ |  | record_hit.c |
| 0x41fa6e | FUN_0041fa6e | 2851 | ✓ |  | actor_ai.c |
| 0x420591 | FUN_00420591 | 84 | ✓ |  | record_hit.c |
| 0x422ed6 | FUN_00422ed6 | 1616 | ✓ |  | actor_ai.c |
| 0x42353c | FUN_0042353c | 330 | ✓ |  | scene1_records_b_tick.c, scene1_records_b_tick.h |
| 0x423789 | FUN_00423789 | 975 | ✓ |  | actor_ai.c |
| 0x423b58 | FUN_00423b58 | 2118 | ✓ |  | dungeon_flow.c, scene1_top_hud.c |
| 0x42439e | FUN_0042439e | 363 | ✓ |  | actor_ai.c |
| 0x424509 | FUN_00424509 | 257 | ✓ |  | actor_ai.c |
| 0x42460a | FUN_0042460a | 55 | ✓ |  | record_hit.c |
| 0x425bff | FUN_00425bff | 64 | ✓ |  | record_hit.c |
| 0x4262b7 | FUN_004262b7 | 1370 | ✓ |  | actor_ai.c |
| 0x426811 | FUN_00426811 | 1690 | ✓ |  | actor_ai.c |
| 0x427f59 | FUN_00427f59 | 698 | ✓ |  | actor_ai.c |
| 0x428213 | FUN_00428213 | 679 | ✓ |  | actor_ai.c |
| 0x4284e1 | FUN_004284e1 | 567 | ✓ |  | actor_ai.c |
| 0x428718 | FUN_00428718 | 972 | ✓ |  | actor_ai.c |
| 0x428ae4 | FUN_00428ae4 | 1062 | ✓ |  | actor_ai.c |
| 0x428f0a | FUN_00428f0a | 1254 | ✓ |  | actor_ai.c |
| 0x4293f0 | FUN_004293f0 | 1394 | ✓ |  | actor_ai.c |
| 0x429962 | FUN_00429962 | 1293 | ✓ |  | actor_ai.c |
| 0x429e6f | FUN_00429e6f | 973 | ✓ |  | actor_ai.c |
| 0x42a23c | FUN_0042a23c | 258 | ✓ |  | actor_ai.c |
| 0x42a33e | FUN_0042a33e | 1038 | ✓ |  | actor_ai.c |
| 0x42a74c | FUN_0042a74c | 1226 | ✓ |  | actor_ai.c |
| 0x42aef7 | FUN_0042aef7 | 1984 | ✓ |  | actor_ai.c |
| 0x42b6b7 | FUN_0042b6b7 | 4590 | ✓ |  | actor_ai.c, scene1_particles_tick.c, scene1_records_b_tick.c |
| 0x42c8a5 | FUN_0042c8a5 | 270 | ✓ |  | actor_ai.c |
| 0x42c9b3 | FUN_0042c9b3 | 1124 | ✓ |  | actor_ai.c, actor_ai.h |
| 0x42ce17 | FUN_0042ce17 | 441 | ✓ |  | actor_ai.c |
| 0x42d392 | FUN_0042d392 | 740 | ✓ |  | actor_ai.c |
| 0x42d676 | FUN_0042d676 | 935 | ✓ |  | actor_ai.c |
| 0x42da1d | FUN_0042da1d | 1336 | ✓ |  | actor_ai.c |
| 0x42df55 | FUN_0042df55 | 593 | ✓ |  | actor_ai.c, actor_ai.h, actor_spawn.h |
| 0x42e1a6 | FUN_0042e1a6 | 1515 | ✓ |  | actor_ai.c, record_hit.c, record_hit.h |
| 0x42e791 | FUN_0042e791 | 676 | ✓ |  | actor_ai.c, record_hit.c, record_hit.h (+3) |
| 0x42ea35 | FUN_0042ea35 | 2135 | ✓ |  | actor_ai.c, scene1_sim.c |
| 0x42f28c | FUN_0042f28c | 795 | ✓ |  | actor_ai.c, actor_ai.h |
| 0x42f685 | FUN_0042f685 | 193 | ✓ |  | actor_ai.c |
| 0x42f746 | FUN_0042f746 | 103 | ✓ |  | actor_ai.c |
| 0x42f7ad | FUN_0042f7ad | 3334 | ✓ |  | actor_ai.c, actor_ai.h |
| 0x4304b3 | FUN_004304b3 | 1869 | ✓ |  | actor_ai.c, actor_ai.h |
| 0x430c00 | FUN_00430c00 | 109 | ✓ |  | actor_ai.c, actor_ai.h, scene1_sim.c (+1) |
| 0x430c6d | FUN_00430c6d | 3022 | ✓ |  | actor_ai.c, actor_ai.h, scene1_particles_tick.c (+2) |
| 0x43183e | FUN_0043183e | 287 | ✓ |  | record_hit.c, record_hit.h |
| 0x431a80 | FUN_00431a80 | 156 | ✓ |  | scene1_preload.c, scene1_preload.h |
| 0x431b1c | FUN_00431b1c | 568 | ✓ |  | enemy_place.h, scene1_postload.c |
| 0x431d54 | FUN_00431d54 | 1762 | ✓ |  | actor_ai.c, actor_table.h, enemy_place.c (+2) |
| 0x432473 | FUN_00432473 | 1064 | ✓ |  | dungeon_objects.c, dungeon_objects.h, scene1_postload.c |
| 0x43289b | FUN_0043289b | 555 | ✓ |  | collision_mesh.c, collision_mesh.h, map_collision.c (+1) |
| 0x432ac6 | FUN_00432ac6 | 906 | ✓ |  | collision_mesh.c, collision_mesh.h, map_collision.c (+2) |
| 0x432e50 | FUN_00432e50 | 2084 | ✓ |  | actor_ai.c, collision_house.c, collision_mesh.h (+24) |
| 0x433674 | FUN_00433674 | 2354 | ✓ |  | actor_ai.c, collision_resolve.c, collision_resolve.h (+11) |
| 0x433fa6 | FUN_00433fa6 | 426 | ✓ |  | party_move.c, party_move.h |
| 0x434198 | FUN_00434198 | 60 | ✓ |  | party_move.h |
| 0x4341d4 | FUN_004341d4 | 42 | ✓ |  | storage.c |
| 0x4341fe | FUN_004341fe | 903 | ✓ |  | chr_sprite_meta.c, chr_sprite_meta.h, chr_sprite_meta_load.c (+4) |
| 0x434585 | FUN_00434585 | 314 | ✓ |  | scene1_overlay_table.c, scene1_overlay_table.h, scene1_per_frame_open.c (+2) |
| 0x4346bf | FUN_004346bf | 805 | ✓ |  | scene1_dialogue_load.c, scene1_overlay_table.c, scene1_overlay_table.h (+3) |
| 0x4349e4 | FUN_004349e4 | 1 | ✓ |  | storage.c, storage.h |
| 0x4349e5 | FUN_004349e5 | 333 | ✓ |  | lnk_lzss.c, lnk_lzss.h, storage.c |
| 0x434b32 | FUN_00434b32 | 250 | ✓ |  | bmp_lzw.c, bmp_lzw.h, storage.c |
| 0x434c2c | FUN_00434c2c | 125 | ✓ |  | bmp_lzw.c, bmp_lzw.h |
| 0x434ca9 | FUN_00434ca9 | 58 | ✓ |  | bmp_lzw.c, bmp_lzw.h |
| 0x434ce3 | FUN_00434ce3 | 8 | ✓ |  | scene_adv_guild.c, scene_guild.c, scene_title.c (+2) |
| 0x434ceb | FUN_00434ceb | 127 | ✓ |  | scene_guild.c, scene_pause.c, title_continue_picker.c (+2) |
| 0x434dbf | FUN_00434dbf | 23 | ✓ |  | choice_box.c, choice_box.h, main.c (+1) |
| 0x434dd6 | FUN_00434dd6 | 25 | ✓ |  | choice_box.c, choice_box.h, scene_pause.c |
| 0x434def | FUN_00434def | 227 | ✓ |  | choice_box.c, choice_box.h, scene.h (+6) |
| 0x434ed2 | FUN_00434ed2 | 581 | ✓ |  | choice_box.c, choice_box.h, customer_service.c (+5) |
| 0x435612 | FUN_00435612 | 8 | ✓ |  | customer_service.c, dungeon_results.c, encyclopedia.c (+14) |
| 0x43561a | FUN_0043561a | 11 | ✓ |  | encyclopedia.c, main.c, scene1_display_menu.c (+8) |
| 0x435625 | FUN_00435625 | 6 | ✓ |  | scene_pause.c, skip_event.c, title_save_dialog.c (+1) |
| 0x435644 | FUN_00435644 | 79 | ✓ |  | encyclopedia.c, scene_pause.c, skip_event.c (+2) |
| 0x435693 | FUN_00435693 | 58 | ✓ |  | customer_service.c, main.c, scene1_counter_menu.c (+9) |
| 0x435710 | FUN_00435710 | 55 | ✓ |  | customer_service.c, scene1_counter_menu.c, scene1_display_menu.c (+8) |
| 0x43609b | FUN_0043609b | 27 | ✓ |  | main.c |
| 0x436180 | FUN_00436180 | 50 |  |  | news_daily.c, news_daily.h |
| 0x4361b2 | FUN_004361b2 | 532 | ✓ |  | customer_haggle.h, customer_service.c, customer_service.h (+7) |
| 0x4363c6 | FUN_004363c6 | 185 | ✓ |  | news_daily.c, news_daily.h |
| 0x43647f | FUN_0043647f | 61 | ✓ |  | actor_ai.c, dungeon_flow.h, item_use.c (+5) |
| 0x4364bc | FUN_004364bc | 359 | ✓ |  | dungeon_flow.h, dungeon_party.c, tables_snews.h |
| 0x436623 | FUN_00436623 | 2420 | ✓ |  | customer_service.c, news_daily.c, news_daily.h (+4) |
| 0x436f97 | FUN_00436f97 | 4788 | ✓ |  | actor_spawn.h, collision_house.c, collision_house.h (+41) |
| 0x43824b | FUN_0043824b | 940 | ✓ |  | record_hit.c, scene1_combat_sm.c, scene1_combat_sm.h (+1) |
| 0x4385fb | FUN_004385fb | 99 | ✓ |  | record_hit.h, scene1_spawn.c, scene1_spawn.h |
| 0x43865e | FUN_0043865e | 8059 | ✓ |  | main.c, record_hit.c, record_hit.h (+6) |
| 0x43a5d9 | FUN_0043a5d9 | 1429 | ✓ |  | dungeon_objects.c, dungeon_objects.h, scene1_particles_tick.h (+3) |
| 0x43ab6e | FUN_0043ab6e | 690 | ✓ |  | scene1_records_b_tick.c, scene1_records_b_tick.h |
| 0x4412b6 | FUN_004412b6 | 2037 | ✓ |  | record_hit.c, record_hit.h, scene1_combat_sm.c (+1) |
| 0x441aab | FUN_00441aab | 403 | ✓ |  | record_hit.c, record_hit.h, scene1_render.c (+1) |
| 0x441c3e | FUN_00441c3e | 2217 | ✓ |  | actor_ai.c, scene1_camera.c, scene1_camera.h (+3) |
| 0x4424e7 | FUN_004424e7 | 429 | ✓ |  | scene1_camera.c, scene1_camera.h, scene1_render.c (+1) |
| 0x44269c | FUN_0044269c | 11 | ✓ |  | actor_ai.c, scene1_sim.c |
| 0x4426a7 | FUN_004426a7 | 300 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_sim.c (+2) |
| 0x4427f1 | FUN_004427f1 | 90 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_sim.c (+1) |
| 0x442c86 | FUN_00442c86 | 105 | ✓ |  | dungeon_results.c, dungeon_results.h, scene1_sim.c |
| 0x44375e | FUN_0044375e | 12 | ✓ |  | scene1_records_b_tick.c |
| 0x44376a | FUN_0044376a | 8538 | ✓ |  | dungeon_party.c, main.c, party.h (+7) |
| 0x4458c4 | FUN_004458c4 | 456 | ✓ |  | record_hit.c |
| 0x445a8c | FUN_00445a8c | 8952 | ✓ |  | actor_ai.c, scene1_records.h, scene1_records_b_spawn.c (+2) |
| 0x447f4f | FUN_00447f4f | 11826 | ✓ |  | actor_ai.c, dungeon_objects.c, dungeon_party.c (+13) |
| 0x44ad81 | FUN_0044ad81 | 367 | ✓ |  | dungeon_objects.c, dungeon_objects.h, scene1_postload.c |
| 0x44aef0 | FUN_0044aef0 | 96 | ✓ |  | record_hit.c, scene1_records.h, scene1_records_c_spawn.c (+2) |
| 0x44af50 | FUN_0044af50 | 419 | ✓ |  | scene1_records_c_spawn.c, scene1_records_c_spawn.h |
| 0x44b0f3 | FUN_0044b0f3 | 60 | ✓ |  | record_hit.c, scene1_particles_tick.c, scene1_records_c_spawn.c (+2) |
| 0x44b12f | FUN_0044b12f | 61 | ✓ |  | dungeon_party.c, scene1_records_c_spawn.c, scene1_records_c_spawn.h |
| 0x44b16c | FUN_0044b16c | 84 | ✓ |  | record_hit.h, scene1_combat_sm.h |
| 0x44b1c0 | FUN_0044b1c0 | 89 | ✓ |  | actor_ai.c, actor_ai.h |
| 0x44b219 | FUN_0044b219 | 60 | ✓ |  | actor_ai.c, record_hit.c, scene1_camera.c (+4) |
| 0x44b255 | FUN_0044b255 | 1 | ✓ |  | scene1_records_b_tick.c, scene1_records_b_tick.h |
| 0x44b257 | FUN_0044b257 | 55 | ✓ |  | dungeon_flow.c, dungeon_objects.c, dungeon_objects.h (+1) |
| 0x44b2e6 | FUN_0044b2e6 | 85 | ✓ |  | dungeon_flow.c, dungeon_flow.h |
| 0x44b38c | FUN_0044b38c | 681 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_sim.c |
| 0x44b635 | FUN_0044b635 | 1015 | ✓ |  | dungeon_flow.h, scene1_sim.c |
| 0x44ba2c | FUN_0044ba2c | 63 | ✓ |  | actor_ai.c, customer_service.c, dungeon_flow.c (+10) |
| 0x44baad | FUN_0044baad | 109 | ✓ |  | scene1_intro_dialogue.c |
| 0x44bb1a | FUN_0044bb1a | 455 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_sim.c |
| 0x44bce7 | FUN_0044bce7 | 28 | ✓ |  | scene_adv_guild.c, scene_worldmap.c |
| 0x44bd03 | FUN_0044bd03 | 8 | ✓ |  | scene1_postload.c |
| 0x44bd0b | FUN_0044bd0b | 1 | ✓ |  | scene1_player_ctrl.c |
| 0x44bd0d | FUN_0044bd0d | 2723 |  |  | dungeon_flow.h, encyclopedia.c, scene1_intro_dialogue.c (+6) |
| 0x44c7bb | FUN_0044c7bb | 8 | ✓ |  | scene_adv_guild.c |
| 0x44c7c3 | FUN_0044c7c3 | 204 | ✓ |  | scene_adv_guild.c, scene_adv_guild.h |
| 0x44c88f | FUN_0044c88f | 299 | ✓ |  | scene1_postload.c, stage_palette.h |
| 0x44c9ba | FUN_0044c9ba | 554 |  |  | dungeon_flow.c, dungeon_flow.h, scene1_camera.c (+1) |
| 0x44cbe4 | FUN_0044cbe4 | 156 | ✓ |  | dungeon_flow.c |
| 0x44cc80 | FUN_0044cc80 | 1060 | ✓ |  | dungeon_flow.c |
| 0x44d0a4 | FUN_0044d0a4 | 320 | ✓ |  | dungeon_flow.c |
| 0x44d1e4 | FUN_0044d1e4 | 665 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_postload.c |
| 0x44d47d | FUN_0044d47d | 1366 |  |  | dungeon_flow.c |
| 0x44d9d3 | FUN_0044d9d3 | 361 | ✓ |  | dungeon_flow.c, dungeon_flow.h, scene1_postload.c |
| 0x44db3c | FUN_0044db3c | 1294 | ✓ |  | dungeon_flow.c |
| 0x44e08f | FUN_0044e08f | 873 | ✓ |  | scene1_postload.c |
| 0x44f009 | FUN_0044f009 | 20 | ✓ |  | actor_ai.c |
| 0x44f078 | FUN_0044f078 | 197 | ✓ |  | dungeon_party.c, scene1_conversation_pose.c |
| 0x44f13d | FUN_0044f13d | 4870 | ✓ |  | scene1_postload.h, scene1_preload.c |
| 0x450877 | FUN_00450877 | 1641 |  |  | dungeon_flow.c |
| 0x451546 | FUN_00451546 | 555 |  |  | dungeon_flow.c |
| 0x451790 | FUN_00451790 | 211 | ✓ |  | main.c, prewindow.h, rng.h (+2) |
| 0x451863 | FUN_00451863 | 17 | ✓ |  | audio_mci.c, audio_mci.h, main.c |
| 0x451ea7 | FUN_00451ea7 | 1343 | ✓ |  | audio_mci.h |
| 0x452569 | FUN_00452569 | 312 | ✓ |  | prewindow.c, prewindow.h |
| 0x4526ab | FUN_004526ab | 74 | ✓ |  | fade.c, fade.h, sim.c |
| 0x452809 | FUN_00452809 | 11 | ✓ |  | scene1_intro_dialogue.c, scene1_tutorial_dispatch.c, scene_adv_guild.c (+1) |
| 0x4528e2 | FUN_004528e2 | 47 | ✓ |  | fade.h, scene1_sim.c |
| 0x452911 | FUN_00452911 | 6 | ✓ |  | esc_dispatch.c, esc_dispatch.h, music.c (+2) |
| 0x452d07 | FUN_00452d07 | 55 | ✓ |  | scene1_intro_dialogue.c, scene1_tutorial_dispatch.c, scene1_tutorial_dispatch.h (+5) |
| 0x452d3e | FUN_00452d3e | 71 | ✓ |  | customer_service.c, scene_buy.h, worker_load.c |
| 0x452d85 | FUN_00452d85 | 60 | ✓ |  | scene1_preload.h, scene_walls.h, worker_load.c |
| 0x452dc1 | FUN_00452dc1 | 60 | ✓ |  | scene1_player_ctrl.c, scene_floor.h, worker_load.c |
| 0x452dfd | FUN_00452dfd | 60 | ✓ |  | scene_jutan.h, worker_load.c |
| 0x452e39 | FUN_00452e39 | 60 | ✓ |  | scene_table.h, worker_load.c |
| 0x452eed | FUN_00452eed | 41 | ✓ |  | music.c, scene1_preload.c, sim.c (+3) |
| 0x452f16 | FUN_00452f16 | 66 | ✓ |  | scene1_sim.c, sim.c, sim.h (+1) |
| 0x452f58 | FUN_00452f58 | 491 | ✓ |  | scene1_overlay.c, scene1_overlay.h, scene1_overlay_helpers.c (+2) |
| 0x4532b1 | FUN_004532b1 | 11 | ✓ |  | dungeon_flow.c, scene1_fx_overlays.h, scene1_render.h (+1) |
| 0x4532bc | FUN_004532bc | 29 | ✓ |  | actor_ai.c, record_hit.c, scene1_fx_overlays.h (+5) |
| 0x453373 | FUN_00453373 | 8 | ✓ |  | dungeon_results.c, main.c, scene_day_end.c (+2) |
| 0x45337b | FUN_0045337b | 9 | ✓ |  | esc_dispatch.c, esc_dispatch.h, scene_pause.c (+2) |
| 0x453384 | FUN_00453384 | 821 | ✓ |  | esc_dispatch.c, scene.h, scene1_day_close.c (+11) |
| 0x4536b9 | FUN_004536b9 | 9 | ✓ |  | scene1_day_close.c, scene1_day_close.h, scene_day_end.c (+2) |
| 0x4536c2 | FUN_004536c2 | 9 | ✓ |  | scene1_sim.c, scene_pause.c |
| 0x4536cb | FUN_004536cb | 1745 | ✓ |  | font.h, input_segtrace.h, movie.h (+16) |
| 0x453d9c | FUN_00453d9c | 243 | ✓ |  | main.c, scene1_fx_overlays.c, scene1_fx_overlays.h (+1) |
| 0x454e69 | FUN_00454e69 | 154 | ✓ |  | layers.c, layers.h, main.c |
| 0x454f03 | FUN_00454f03 | 120 | ✓ |  | mesh_draw.c, mesh_draw.h, scene1_alpha_walker.c (+5) |
| 0x454f7c | FUN_00454f7c | 104 | ✓ |  | scene1_emit_record.c, scene1_emit_record.h, scene1_walker_pass_init.c |
| 0x454fe4 | FUN_00454fe4 | 429 | ✓ |  | scene1_emit_record.c, scene1_emit_record.h, scene1_render.c (+1) |
| 0x455191 | FUN_00455191 | 217 | ✓ |  | scene1_chr_prepass.c, scene1_chr_prepass.h, scene1_emit_record.c (+7) |
| 0x45526a | FUN_0045526a | 102 | ✓ |  | encyclopedia.c, scene1_chr_prepass.c, scene1_chr_prepass.h (+2) |
| 0x45672a | FUN_0045672a | 1317 | ✓ |  | chr_sprite_meta.h, scene1_chr_prepass.c, scene1_chr_prepass.h (+2) |
| 0x456c4f | FUN_00456c4f | 249 | ✓ |  | scene1_chr_prepass.c, scene1_chr_prepass.h |
| 0x456d48 | FUN_00456d48 | 526 | ✓ |  | scene1_chr_walker.c, scene1_chr_walker.h, scene1_render.c (+3) |
| 0x456f56 | FUN_00456f56 | 1982 | ✓ |  | chr_sprite_meta.h, main.c, scene1_camera.h (+7) |
| 0x458bdf | FUN_00458bdf | 904 | ✓ |  | scene1_alpha_walker.c, scene1_alpha_walker.h, scene1_render.c (+1) |
| 0x458f67 | FUN_00458f67 | 2118 | ✓ |  | scene1_maplight.c, scene1_maplight.h, scene1_render.c (+1) |
| 0x4597ad | FUN_004597ad | 48 | ✓ |  | scene1_render.c, scene1_render.h |
| 0x4597dd | FUN_004597dd | 106 | ✓ |  | scene1_render.c, scene1_render.h |
| 0x459847 | FUN_00459847 | 1444 | ✓ |  | scene1_alpha_walker.c, scene1_alpha_walker.h, scene1_chr_shadow.h (+3) |
| 0x45bcea | FUN_0045bcea | 56 | ✓ |  | scene1_sim.c, scene_adv_guild.c, scene_adv_guild.h (+1) |
| 0x45bd22 | FUN_0045bd22 | 15 | ✓ |  | floor_layout.c, floor_layout.h |
| 0x45bd36 | FUN_0045bd36 | 140 | ✓ |  | scene_adv_guild.c, scene_adv_guild.h, scene_pause.c |
| 0x45bdc2 | FUN_0045bdc2 | 546 | ✓ |  | party_roster.h, scene_adv_guild.c, scene_adv_guild.h (+1) |
| 0x45c025 | FUN_0045c025 | 44 | ✓ |  | dungeon_flow.c |
| 0x45c051 | FUN_0045c051 | 3021 | ✓ |  | scene1_tutorial_dispatch.h, scene_adv_guild.c, scene_adv_guild.h (+1) |
| 0x45cc1e | FUN_0045cc1e | 103 | ✓ |  | main.c, scene_adv_guild.c, scene_adv_guild.h |
| 0x45cc85 | FUN_0045cc85 | 4579 | ✓ |  | main.c, scene_adv_guild.c, scene_adv_guild.h |
| 0x45de68 | FUN_0045de68 | 433 | ✓ |  | event_probe.c, event_probe.h, scene_guild.c (+7) |
| 0x45e019 | FUN_0045e019 | 15 | ✓ |  | scene_town_event.h, scene_worldmap.c |
| 0x45e028 | FUN_0045e028 | 43 | ✓ |  | customer_service.c, dungeon_results.c, scene1_day_close.c (+3) |
| 0x45e053 | FUN_0045e053 | 201 | ✓ |  | event_probe.h, scene_town_event.c, scene_town_event.h (+1) |
| 0x45e11c | FUN_0045e11c | 122 | ✓ |  | scene_town_event.h |
| 0x45e196 | FUN_0045e196 | 15 | ✓ |  | scene_town_event.h, scene_worldmap.c |
| 0x45e1a5 | FUN_0045e1a5 | 175 | ✓ |  | scene_town_event.c, scene_town_event.h, sim.c |
| 0x45e254 | FUN_0045e254 | 122 | ✓ |  | scene_town_event.h |
| 0x45e2dd | FUN_0045e2dd | 118 | ✓ |  | scene_town_event.c, scene_town_event.h, sim.c |
| 0x45e353 | FUN_0045e353 | 122 | ✓ |  | scene_town_event.h |
| 0x45e3cd | FUN_0045e3cd | 15 | ✓ |  | scene_town_event.h, scene_worldmap.c |
| 0x45e3dc | FUN_0045e3dc | 175 | ✓ |  | scene_town_event.c, scene_town_event.h, sim.c |
| 0x45e48b | FUN_0045e48b | 122 | ✓ |  | main.c, scene_town_event.c, scene_town_event.h |
| 0x45e505 | FUN_0045e505 | 50 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x45e55c | FUN_0045e55c | 290 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x45e67e | FUN_0045e67e | 39 | ✓ |  | scene_day_end.c |
| 0x45e6a5 | FUN_0045e6a5 | 59 | ✓ |  | customer_service.c, customer_service.h, scene_pause.c |
| 0x45e6e0 | FUN_0045e6e0 | 215 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x45e7b7 | FUN_0045e7b7 | 6 | ✓ |  | music.h |
| 0x45e80f | FUN_0045e80f | 298 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x45ecc0 | FUN_0045ecc0 | 82 | ✓ |  | customer_haggle.c, customer_haggle.h, customer_service.c |
| 0x45ed12 | FUN_0045ed12 | 152 | ✓ |  | customer_roster.c, customer_roster.h, customer_service.c |
| 0x45edaa | FUN_0045edaa | 4455 | ✓ |  | customer_roster.h, customer_service.c, customer_service.h (+5) |
| 0x45ff11 | FUN_0045ff11 | 32 | ✓ |  | customer_service.c |
| 0x45ff31 | FUN_0045ff31 | 249 | ✓ |  | customer_service.c |
| 0x46002a | FUN_0046002a | 89 | ✓ |  | customer_service.c |
| 0x460083 | FUN_00460083 | 222 | ✓ |  | customer_service.c, customer_service.h, dungeon_results.c (+1) |
| 0x460161 | FUN_00460161 | 622 | ✓ |  | customer_haggle.c, customer_haggle.h, customer_service.c |
| 0x4603cf | FUN_004603cf | 675 | ✓ |  | customer_haggle.c, customer_haggle.h, customer_service.c |
| 0x460672 | FUN_00460672 | 138 | ✓ |  | customer_haggle.c, customer_haggle.h, customer_service.c |
| 0x4606fc | FUN_004606fc | 205 | ✓ |  | customer_service.c, customer_service.h |
| 0x4607f3 | FUN_004607f3 | 196 | ✓ |  | customer_service.c, dialogue_macros.h |
| 0x46094a | FUN_0046094a | 69 | ✓ |  | customer_service.c |
| 0x46098f | FUN_0046098f | 139 | ✓ |  | customer_service.c |
| 0x460a1a | FUN_00460a1a | 288 | ✓ |  | customer_dialogue.c, customer_dialogue.h, customer_service.c (+1) |
| 0x460b3a | FUN_00460b3a | 88 | ✓ |  | customer_service.c, save_bank.h |
| 0x460b93 | FUN_00460b93 | 447 | ✓ |  | customer_service.c |
| 0x460d52 | FUN_00460d52 | 254 | ✓ |  | customer_service.c |
| 0x460e50 | FUN_00460e50 | 106 | ✓ |  | customer_service.c |
| 0x460f16 | FUN_00460f16 | 67 | ✓ |  | customer_service.c, customer_service.h |
| 0x460f59 | FUN_00460f59 | 78 | ✓ |  | customer_service.c, save_bank.h |
| 0x460fa7 | FUN_00460fa7 | 106 | ✓ |  | customer_service.c |
| 0x461011 | FUN_00461011 | 87 |  |  | customer_service.c |
| 0x461068 | FUN_00461068 | 667 | ✓ |  | customer_service.c |
| 0x461303 | FUN_00461303 | 1167 | ✓ |  | customer_service.c, scene1_player_ctrl.c |
| 0x461792 | FUN_00461792 | 1124 | ✓ |  | customer_seller.h, customer_service.c |
| 0x461bf6 | FUN_00461bf6 | 10 | ✓ |  | customer_service.h, scene1_player_ctrl.c |
| 0x461c00 | FUN_00461c00 | 1753 | ✓ |  | customer_service.c, customer_service.h, tables_tuto.h |
| 0x4622d9 | FUN_004622d9 | 227 | ✓ |  | customer_service.c |
| 0x4623bc | FUN_004623bc | 71 | ✓ |  | customer_service.c |
| 0x462403 | FUN_00462403 | 5618 | ✓ |  | customer_seller.h, customer_service.c, customer_service.h (+6) |
| 0x4639f5 | FUN_004639f5 | 774 | ✓ |  | customer_service.c |
| 0x463cfb | FUN_00463cfb | 3371 | ✓ |  | customer_service.c, customer_service.h |
| 0x464af0 | FUN_00464af0 | 2178 | ✓ |  | customer_service.c |
| 0x465372 | FUN_00465372 | 1337 | ✓ |  | customer_service.c |
| 0x4658ab | FUN_004658ab | 1289 | ✓ |  | customer_service.c, customer_service.h, save_bank.h |
| 0x465db4 | FUN_00465db4 | 634 | ✓ |  | customer_service.c, customer_service_render.c, dialogue_macros.c (+4) |
| 0x46602e | FUN_0046602e | 2668 | ✓ |  | customer_service.c, customer_service.h, customer_service_render.c (+2) |
| 0x466a9a | FUN_00466a9a | 225 | ✓ |  | customer_service_render.c |
| 0x466b7b | FUN_00466b7b | 5305 | ✓ |  | customer_service.c, customer_service.h, customer_service_render.c (+2) |
| 0x468034 | FUN_00468034 | 253 | ✓ |  | customer_service_render.c |
| 0x468131 | FUN_00468131 | 117 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_adv_guild.c (+1) |
| 0x4681a6 | FUN_004681a6 | 45 |  |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x4681d3 | FUN_004681d3 | 8 | ✓ |  | customer_service.c, dungeon_results.c, encyclopedia.c (+3) |
| 0x4681db | FUN_004681db | 11 | ✓ |  | customer_service.c, dungeon_results.c, encyclopedia.c (+5) |
| 0x4681e6 | FUN_004681e6 | 6 | ✓ |  | customer_service.c, dungeon_results.c, scene1_display_menu.h (+2) |
| 0x4681ec | FUN_004681ec | 10 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene1_player_ctrl.c |
| 0x468233 | FUN_00468233 | 19 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_adv_guild.c (+1) |
| 0x468246 | FUN_00468246 | 64 |  |  | scene1_display_menu.h |
| 0x468286 | FUN_00468286 | 14 | ✓ |  | customer_service.c, dungeon_results.c, encyclopedia.c (+5) |
| 0x468294 | FUN_00468294 | 37 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x4682b9 | FUN_004682b9 | 6 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x4682bf | FUN_004682bf | 6 | ✓ |  | scene_adv_guild.c, scene_guild.c, scene_pause.c (+1) |
| 0x4682c5 | FUN_004682c5 | 11 | ✓ |  | scene_adv_guild.c, scene_guild.c, scene_pause.c (+1) |
| 0x4682d8 | FUN_004682d8 | 11 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_guild.c |
| 0x4682e3 | FUN_004682e3 | 11 | ✓ |  | scene_adv_guild.c, scene_pause.c, stage_load_pulse.h |
| 0x4682ee | FUN_004682ee | 74 | ✓ |  | scene_adv_guild.c |
| 0x468338 | FUN_00468338 | 2490 | ✓ |  | customer_service.c, dungeon_results.c, main.c (+10) |
| 0x468cf2 | FUN_00468cf2 | 19 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h |
| 0x468d05 | FUN_00468d05 | 7 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x468d0c | FUN_00468d0c | 7 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x468d13 | FUN_00468d13 | 15 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x468d22 | FUN_00468d22 | 73 | ✓ |  | customer_service.c, dungeon_results.c, encyclopedia.c (+7) |
| 0x468d6b | FUN_00468d6b | 113 | ✓ |  | customer_service.c |
| 0x468ddc | FUN_00468ddc | 303 | ✓ |  | customer_service.c, customer_service.h, scene1_display_menu.c (+1) |
| 0x468f6d | FUN_00468f6d | 99 | ✓ |  | dungeon_results.c, scene1_display_menu.c, scene1_display_menu.h (+1) |
| 0x468fd0 | FUN_00468fd0 | 328 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_guild.c (+1) |
| 0x469241 | FUN_00469241 | 99 | ✓ |  | customer_service.c, scene1_display_menu.c, scene1_display_menu.h (+2) |
| 0x469351 | FUN_00469351 | 73 | ✓ |  | customer_service.c, scene1_display_menu.c, scene1_display_menu.h |
| 0x46939a | FUN_0046939a | 73 | ✓ |  | dungeon_results.c, scene1_display_menu.c, scene1_display_menu.h (+3) |
| 0x46940c | FUN_0046940c | 8 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_pause.c |
| 0x469414 | FUN_00469414 | 1516 | ✓ |  | customer_service.c, dungeon_results.c, scene1_display_menu.c (+5) |
| 0x469a00 | FUN_00469a00 | 131 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h, scene_guild.c |
| 0x469a83 | FUN_00469a83 | 28 | ✓ |  | scene1_display_menu.c, scene1_display_menu.h |
| 0x469a9f | FUN_00469a9f | 28 | ✓ |  | customer_service.c, scene1_display_menu.c, scene1_display_menu.h (+4) |
| 0x469abb | FUN_00469abb | 127 | ✓ |  | customer_service_render.c, encyclopedia.c, scene1_display_menu.c |
| 0x469b3a | FUN_00469b3a | 2044 | ✓ |  | scene1_display_menu.c |
| 0x46a336 | FUN_0046a336 | 2722 | ✓ |  | chara_equip.c, chara_equip.h, encyclopedia.c (+1) |
| 0x46add8 | FUN_0046add8 | 562 | ✓ |  | scene1_display_menu.c, scene_adv_guild.c |
| 0x46b00a | FUN_0046b00a | 3640 | ✓ |  | customer_service.c, main.c, scene1_display_menu.c (+4) |
| 0x46bf38 | FUN_0046bf38 | 230 | ✓ |  | mesh_load.h, scene1_dialogue_draw.c, scene_sc1.c (+2) |
| 0x46c01e | FUN_0046c01e | 27 | ✓ |  | scene_sc1.h, worker_load.c, worker_load.h |
| 0x46c039 | FUN_0046c039 | 87 | ✓ |  | sim.c |
| 0x46c090 | FUN_0046c090 | 30 | ✓ |  | choice_box.c, main.c, scene1_dialogue_draw.c (+3) |
| 0x46c0ae | FUN_0046c0ae | 487 | ✓ |  | scene1_dialogue_run.c, scene1_dialogue_run.h |
| 0x46c295 | FUN_0046c295 | 54 | ✓ |  | scene1_dialogue.c, scene1_dialogue.h, scene1_dialogue_load.c (+2) |
| 0x46c2cb | FUN_0046c2cb | 85 | ✓ |  | choice_box.h, scene.h, scene1_intro_dialogue.c (+5) |
| 0x46c320 | FUN_0046c320 | 1353 | ✓ |  | save_work.h, scene1_dialogue.c, scene1_dialogue.h (+6) |
| 0x46c869 | FUN_0046c869 | 6 | ✓ |  | main.c, scene1_intro_dialogue.c, scene1_intro_dialogue.h (+1) |
| 0x46c86f | FUN_0046c86f | 307 | ✓ |  | customer_service_render.c, scene1_dialogue_run.c, scene1_dialogue_run.h (+2) |
| 0x46c9a2 | FUN_0046c9a2 | 3800 | ✓ |  | main.c, scene1_dialogue.c, scene1_dialogue.h (+5) |
| 0x46dc45 | FUN_0046dc45 | 61 |  |  | scene1_dialogue.c |
| 0x46ddea | FUN_0046ddea | 5119 | ✓ |  | scene1_dialogue.c, scene1_dialogue.h, scene1_dialogue_load.c |
| 0x46f892 | FUN_0046f892 | 40 | ✓ |  | customer_service.c, scene1_postload.c, scene1_postload.h (+1) |
| 0x46f8ba | FUN_0046f8ba | 90 | ✓ |  | customer_service.c, scene1_shop_walker.h, scene1_shop_walker_helpers.c |
| 0x46f914 | FUN_0046f914 | 285 | ✓ |  | scene1_shop_walker.h, scene1_shop_walker_helpers.c |
| 0x46fa31 | FUN_0046fa31 | 390 | ✓ |  | scene1_shop_walker.h, scene1_shop_walker_helpers.c |
| 0x46fbb7 | FUN_0046fbb7 | 55 | ✓ |  | scene1_shop_walker.h, scene1_shop_walker_helpers.c |
| 0x46fbee | FUN_0046fbee | 1457 | ✓ |  | scene1_shop_walker.h, scene1_shop_walker_helpers.c, story_visitor.c (+1) |
| 0x47019f | FUN_0047019f | 486 | ✓ |  | customer_service.c, scene1_conversation_pose.c, scene1_player_ctrl.c (+2) |
| 0x47047b | FUN_0047047b | 296 | ✓ |  | scene1_chr_walker.c, scene1_chr_walker.h, scene1_shop_walker.c |
| 0x4706ea | FUN_004706ea | 525 | ✓ |  | scene1_actor_say.h, scene1_day_close.c, scene1_player_ctrl.h (+2) |
| 0x4708f7 | FUN_004708f7 | 121 | ✓ |  | dungeon_party.c, scene1_conversation_pose.c, story_visitor.c (+1) |
| 0x470970 | FUN_00470970 | 214 | ✓ |  | scene1_conversation_pose.c, scene1_conversation_pose.h, story_visitor.c (+1) |
| 0x470a46 | FUN_00470a46 | 766 | ✓ |  | scene1_conversation_pose.c, scene1_conversation_pose.h, scene1_tutorial_dispatch.c (+2) |
| 0x470d44 | FUN_00470d44 | 292 | ✓ |  | scene1_bg_npc.h, scene1_shop_walker.c |
| 0x471050 | FUN_00471050 | 11 | ✓ |  | main.c, prewindow.h, rng.h |
| 0x471089 | FUN_00471089 | 34 | ✓ |  | actor_ai.c, actor_table.c, customer_haggle.c (+18) |
| 0x47183b | FUN_0047183b | 151 | ✓ |  | d3d_pool.c, d3d_pool.h, scene.c |
| 0x4718d2 | FUN_004718d2 | 51 | ✓ |  | d3d_pool.h |
| 0x47193c | FUN_0047193c | 488 | ✓ |  | d3d_tex_names.h, scene1_dungeon_clear_banner.c, scene1_preload.c (+14) |
| 0x471b24 | FUN_00471b24 | 467 | ✓ |  | d3d_tex_names.h, mesh_load.c, mesh_load.h (+1) |
| 0x471d45 | FUN_00471d45 | 2777 | ✓ |  | collision_mesh.c, collision_mesh.h, map_collision.c (+5) |
| 0x472836 | FUN_00472836 | 1609 | ✓ |  | mesh.h, mesh_load.c, mesh_load.h (+6) |
| 0x472f5d | FUN_00472f5d | 821 | ✓ |  | main.c, scene1_preload.c, scene1_preload.h (+4) |
| 0x47329b | FUN_0047329b | 151 | ✓ |  | main.c, scene_buy.c, scene_buy.h (+1) |
| 0x473332 | FUN_00473332 | 9 | ✓ |  | customer_service.c |
| 0x47333b | FUN_0047333b | 145 | ✓ |  | customer_service.c, main.c, scene_buy.c (+2) |
| 0x4733cc | FUN_004733cc | 9 | ✓ |  | customer_service.c |
| 0x4733d5 | FUN_004733d5 | 159 | ✓ |  | main.c, scene_pause.c, scene_title.c (+2) |
| 0x47347d | FUN_0047347d | 215 | ✓ |  | worker_load.h |
| 0x47355d | FUN_0047355d | 31 | ✓ |  | scene_gameover.h, worker_load.h |
| 0x47357c | FUN_0047357c | 9 | ✓ |  | scene_gameover.c |
| 0x473585 | FUN_00473585 | 31 | ✓ |  | worker_load.h |
| 0x4735ad | FUN_004735ad | 98 | ✓ |  | main.c, scene.h, scene_worldmap.c (+2) |
| 0x47360f | FUN_0047360f | 9 | ✓ |  | scene_worldmap.c |
| 0x473618 | FUN_00473618 | 80 | ✓ |  | scene_day_end.c, scene_day_end.h, scene_pause.c |
| 0x473668 | FUN_00473668 | 9 | ✓ |  | scene_day_end.c, scene_pause.c |
| 0x473672 | FUN_00473672 | 9 | ✓ |  | dungeon_results.c |
| 0x4736bd | FUN_004736bd | 163 | ✓ |  | worker_load.h |
| 0x473769 | FUN_00473769 | 258 | ✓ |  | scene_guild.c, scene_guild.h, worker_load.h |
| 0x47386b | FUN_0047386b | 9 | ✓ |  | scene_guild.c |
| 0x473874 | FUN_00473874 | 245 | ✓ |  | scene_adv_guild.c, scene_adv_guild.h, worker_load.h |
| 0x473969 | FUN_00473969 | 9 | ✓ |  | scene_adv_guild.c, scene_town_event.c, scene_town_event.h |
| 0x473972 | FUN_00473972 | 31 | ✓ |  | scene_town_event.c, scene_town_event.h, worker_load.h |
| 0x473991 | FUN_00473991 | 75 | ✓ |  | scene_town_event.c, scene_town_event.h, worker_load.h |
| 0x4739dc | FUN_004739dc | 31 | ✓ |  | scene_town_event.c, scene_town_event.h, worker_load.h |
| 0x4739fb | FUN_004739fb | 31 | ✓ |  | scene_town_event.c, scene_town_event.h, worker_load.h |
| 0x473a3e | FUN_00473a3e | 453 | ✓ |  | anchor_trace.c, main.c, scene_pause.c (+2) |
| 0x473c03 | FUN_00473c03 | 9 | ✓ |  | scene1_intro_dialogue.c, scene_pause.c |
| 0x473c15 | FUN_00473c15 | 2476 | ✓ |  | scene1_preload.c, scene1_preload.h, scene_adv_guild.c (+1) |
| 0x4746fc | FUN_004746fc | 48 | ✓ |  | scene1_preload.c, worker_load.h |
| 0x47472c | FUN_0047472c | 34 | ✓ |  | scene1_preload.c, worker_load.h |
| 0x47474e | FUN_0047474e | 142 | ✓ |  | main.c, scene1_preload.c, scene_floor.c (+6) |
| 0x4747dc | FUN_004747dc | 142 | ✓ |  | main.c, scene1_preload.c, scene_floor.c (+3) |
| 0x47486a | FUN_0047486a | 142 | ✓ |  | main.c, scene1_preload.c, scene_jutan.c (+2) |
| 0x4748f8 | FUN_004748f8 | 169 | ✓ |  | mesh_load.h, scene1_preload.c, scene_table.c (+2) |
| 0x474d92 | FUN_00474d92 | 232 | ✓ |  | dungeon_results.c, scene1_player_ctrl.c, scene1_sim.c (+2) |
| 0x474e7a | FUN_00474e7a | 153 | ✓ |  | main.c |
| 0x474f14 | FUN_00474f14 | 58 | ✓ |  | lnkdatas_hash.c, lnkdatas_hash.h, storage.c |
| 0x474f4f | FUN_00474f4f | 801 | ✓ |  | main.c, scene1_overlay.h, scene1_overlay_table.c (+3) |
| 0x475270 | FUN_00475270 | 19645 | ✓ |  | customer_dialogue.c, customer_dialogue.h, main.c (+36) |
| 0x479f4d | FUN_00479f4d | 43 | ✓ |  | item_use.c, scene1_dialogue.c, tables.c (+3) |
| 0x479f78 | FUN_00479f78 | 1227 | ✓ |  | chr_sprite_meta.c, chr_sprite_meta.h, chr_sprite_meta_load.c (+1) |
| 0x47a474 | FUN_0047a474 | 912 | ✓ |  | main.c, recet_ini.c, recet_ini.h |
| 0x47aa30 | FUN_0047aa30 | 1 | ✓ |  | main.c |
| 0x47aa31 | FUN_0047aa31 | 1 | ✓ |  | tables_enemylist.c, tables_gousei.c, tables_snews.c |
| 0x47aa3b | FUN_0047aa3b | 80 | ✓ |  | movie.c |
| 0x47aa8b | FUN_0047aa8b | 402 | ✓ |  | main.c |
| 0x47ac6a | FUN_0047ac6a | 507 | ✓ |  | main.c |
| 0x47ae65 | FUN_0047ae65 | 237 | ✓ |  | main.c, screen_rt.c, screen_rt.h |
| 0x47af52 | FUN_0047af52 | 413 | ✓ |  | input.c, input.h, main.c |
| 0x47b0ef | FUN_0047b0ef | 120 | ✓ |  | input.c, input.h |
| 0x47b1f2 | FUN_0047b1f2 | 99 |  |  | input.c |
| 0x47b29e | FUN_0047b29e | 73 | ✓ |  | main.c, prewindow.c, scene.h (+1) |
| 0x47b2e7 | FUN_0047b2e7 | 1061 |  |  | esc_dispatch.c, esc_dispatch.h, main.c |
| 0x47be92 | FUN_0047be92 | 289 | ✓ |  | input.c, input.h, main.c (+5) |
| 0x47bfb3 | FUN_0047bfb3 | 629 | ✓ |  | main.c |
| 0x47c228 | FUN_0047c228 | 61 | ✓ |  | font.h, main.c |
| 0x47c3a5 | FUN_0047c3a5 | 207 | ✓ |  | font.h, font_atlas.c, font_atlas.h (+1) |
| 0x47c474 | FUN_0047c474 | 1425 | ✓ |  | font.h, font_atlas.c, font_atlas.h (+1) |
| 0x47ca05 | FUN_0047ca05 | 454 | ✓ |  | choice_box.c, encyclopedia.c, font.h (+6) |
| 0x47cbcb | FUN_0047cbcb | 855 | ✓ |  | font.h, font_alloc.c, font_alloc.h (+3) |
| 0x47cf22 | FUN_0047cf22 | 456 | ✓ |  | font.h, font_upload.c, font_upload.h |
| 0x47d0ea | FUN_0047d0ea | 98 | ✓ |  | font_draw.c, font_draw.h, scene1_hud.c |
| 0x47d14c | FUN_0047d14c | 399 | ✓ |  | choice_box.c, font_draw.h, scene1_counter_menu.c (+5) |
| 0x47d2db | FUN_0047d2db | 393 | ✓ |  | font_draw.h |
| 0x47d464 | FUN_0047d464 | 445 | ✓ |  | font_draw.c, font_draw.h, scene1_dialogue_draw.c |
| 0x47e711 | FUN_0047e711 | 403 | ✓ |  | sim.c |
| 0x47f172 | FUN_0047f172 | 46 | ✓ |  | save_work.h, scene_adv_guild.c, scene_pause.c |
| 0x47f1a0 | FUN_0047f1a0 | 46 | ✓ |  | save_work.h, scene_pause.c |
| 0x47f1ce | FUN_0047f1ce | 296 | ✓ |  | customer_service.c, party_roster.h, scene1_display_menu.c (+3) |
| 0x47f2f6 | FUN_0047f2f6 | 372 | ✓ |  | dungeon_party.c, party_roster.h, scene_pause.c (+2) |
| 0x47f46a | FUN_0047f46a | 338 | ✓ |  | scene1_sim.c, scene_pause.c |
| 0x47f5bc | FUN_0047f5bc | 1171 | ✓ |  | anchor_trace.h, save_picker.h, scene_pause.c (+1) |
| 0x47fa4f | FUN_0047fa4f | 39 | ✓ |  | scene_adv_guild.c, stage_palette.h |
| 0x47fa76 | FUN_0047fa76 | 462 | ✓ |  | encyclopedia.h, scene_pause.c, scene_pause.h (+1) |
| 0x47fc44 | FUN_0047fc44 | 596 | ✓ |  | audio_fade.h, scene_pause.c, scene_pause.h |
| 0x47fe98 | FUN_0047fe98 | 168 | ✓ |  | scene_pause.c |
| 0x47ff40 | FUN_0047ff40 | 911 | ✓ |  | dungeon_party.c, item_use.h, scene1_display_menu.c (+2) |
| 0x4802cf | FUN_004802cf | 837 | ✓ |  | scene_pause.c, scene_pause.h |
| 0x480614 | FUN_00480614 | 718 | ✓ |  | encyclopedia.h, scene_pause.c, scene_pause.h |
| 0x4808fb | FUN_004808fb | 68 | ✓ |  | scene_gameover.c |
| 0x480b65 | FUN_00480b65 | 1919 | ✓ |  | chara_skills.h |
| 0x4812e4 | FUN_004812e4 | 552 | ✓ |  | save_picker.h, scene_pause.c, scene_pause.h (+1) |
| 0x48150c | FUN_0048150c | 102 | ✓ |  | scene_pause.c, settings_panel.h |
| 0x48196b | FUN_0048196b | 415 | ✓ |  | scene1_display_menu.h, scene_pause.c |
| 0x481ec3 | FUN_00481ec3 | 368 | ✓ |  | save_picker.c, scene1_merchant_hud.c, scene1_merchant_hud.h (+2) |
| 0x482033 | FUN_00482033 | 38 | ✓ |  | scene_pause.c, scene_pause.h |
| 0x482059 | FUN_00482059 | 97 | ✓ |  | scene1_tutorial_dispatch.c, scene_day_end.c, scene_pause.c (+1) |
| 0x4820ba | FUN_004820ba | 2455 | ✓ |  | choice_box.c, encyclopedia.h, main.c (+2) |
| 0x482a51 | FUN_00482a51 | 32 | ✓ |  | actor_ai.c, actor_table.h, dungeon_flow.c (+11) |
| 0x482a71 | FUN_00482a71 | 118 | ✓ |  | actor_ai.c, dungeon_party.c, scene1_bg_npc.c (+8) |
| 0x482ae7 | FUN_00482ae7 | 348 | ✓ |  | actor_ai.c, dungeon_party.c, dungeon_party.h (+1) |
| 0x482c43 | FUN_00482c43 | 1198 | ✓ |  | party_move.h |
| 0x4830f1 | FUN_004830f1 | 127 | ✓ |  | collision_resolve.c, collision_resolve.h, party_move.h |
| 0x483170 | FUN_00483170 | 3339 | ✓ |  | collision_query.h, collision_resolve.c, collision_resolve.h (+6) |
| 0x483e7b | FUN_00483e7b | 516 | ✓ |  | dungeon_party.c, dungeon_party.h, scene1_conversation_pose.c (+1) |
| 0x48439a | FUN_0048439a | 341 | ✓ |  | customer_roster.h, customer_service.c, save_bank.h (+2) |
| 0x4846e8 | FUN_004846e8 | 204 | ✓ |  | dungeon_party.c |
| 0x4847b4 | FUN_004847b4 | 187 | ✓ |  | dungeon_party.c |
| 0x48486f | FUN_0048486f | 85 | ✓ |  | encyclopedia.c |
| 0x4848c4 | FUN_004848c4 | 132 | ✓ |  | encyclopedia.c, item_use.h, scene_pause.c |
| 0x484948 | FUN_00484948 | 94 | ✓ |  | encyclopedia.c, item_use.h, scene_pause.c |
| 0x4849a6 | FUN_004849a6 | 549 | ✓ |  | item_use.h, scene_pause.c |
| 0x484bcb | FUN_00484bcb | 412 | ✓ |  | item_use.h, scene_pause.c |
| 0x484da3 | FUN_00484da3 | 46 | ✓ |  | dungeon_party.c, dungeon_party.h, dungeon_results.c |
| 0x484dd1 | FUN_00484dd1 | 116 | ✓ |  | dungeon_party.c, dungeon_party.h, scene1_records_c_tick.c (+2) |
| 0x484e45 | FUN_00484e45 | 82 | ✓ |  | dungeon_party.c, dungeon_party.h, record_hit.c (+1) |
| 0x484e97 | FUN_00484e97 | 286 | ✓ |  | dungeon_party.c, scene1_records_c_spawn.h |
| 0x4850fe | FUN_004850fe | 228 | ✓ |  | scene1_counter_menu.c, scene1_counter_menu.h, scene1_player_ctrl.c |
| 0x4851e2 | FUN_004851e2 | 139 | ✓ |  | scene1_day_close.c, scene1_day_close.h, scene1_player_ctrl.c (+3) |
| 0x48526d | FUN_0048526d | 142 | ✓ |  | customer_service.c, scene1_day_close.h, scene1_player_ctrl.h (+3) |
| 0x4852fb | FUN_004852fb | 280 | ✓ |  | scene1_actor_say.h, scene1_camera.h, scene1_conversation_pose.c (+8) |
| 0x485413 | FUN_00485413 | 55 | ✓ |  | record_hit.c, scene1_actor_say.h, scene1_combat_sm.h |
| 0x48544a | FUN_0048544a | 408 | ✓ |  | scene1_actor_say.h |
| 0x4855e2 | FUN_004855e2 | 245 | ✓ |  | dungeon_party.c, dungeon_party.h |
| 0x4856d7 | FUN_004856d7 | 59 | ✓ |  | dungeon_party.c |
| 0x485712 | FUN_00485712 | 317 | ✓ |  | dungeon_party.c, dungeon_party.h, record_hit.c (+3) |
| 0x48584f | FUN_0048584f | 18 | ✓ |  | record_hit.c |
| 0x485979 | FUN_00485979 | 731 | ✓ |  | dungeon_flow.c, record_hit.c, record_hit.h (+1) |
| 0x485c74 | FUN_00485c74 | 333 | ✓ |  | dungeon_flow.c, dungeon_flow.h, record_hit.c (+1) |
| 0x485f8c | FUN_00485f8c | 316 | ✓ |  | scene1_wide_followup.c, scene1_wide_followup.h |
| 0x4860c8 | FUN_004860c8 | 215 | ✓ |  | scene1_player_ctrl.c, scene1_shop_display.c, scene1_shop_display.h |
| 0x48619f | FUN_0048619f | 328 | ✓ |  | scene1_player_ctrl.c, scene1_player_ctrl.h, scene1_shop_display.h |
| 0x486435 | FUN_00486435 | 200 | ✓ |  | scene1_player_ctrl.c, scene1_player_ctrl.h |
| 0x4864fd | FUN_004864fd | 216 | ✓ |  | scene_guild.c |
| 0x48670f | FUN_0048670f | 11519 | ✓ |  | customer_service.h, dungeon_party.h, party_roster.h (+16) |
| 0x48960d | FUN_0048960d | 441 | ✓ |  | scene1_player_ctrl.c, scene1_shop_display.c, scene1_shop_display.h |
| 0x4897c6 | FUN_004897c6 | 870 | ✓ |  | dungeon_party.c, dungeon_party.h, party.h (+2) |
| 0x489b2c | FUN_00489b2c | 333 | ✓ |  | dungeon_party.c |
| 0x489c79 | FUN_00489c79 | 217 | ✓ |  | chara_skills.h, dungeon_party.c |
| 0x489d52 | FUN_00489d52 | 102 | ✓ |  | chara_skills.h, dungeon_party.c |
| 0x489db8 | FUN_00489db8 | 174 | ✓ |  | dungeon_party.c |
| 0x489e66 | FUN_00489e66 | 1227 | ✓ |  | dungeon_party.c |
| 0x48a348 | FUN_0048a348 | 59 | ✓ |  | record_hit.c, scene1_combat_sm.h |
| 0x48a383 | FUN_0048a383 | 334 | ✓ |  | dungeon_party.c, record_hit.c, xp_curve.h |
| 0x48a4d1 | FUN_0048a4d1 | 866 | ✓ |  | customer_service.c, dungeon_party.c, dungeon_party.h (+1) |
| 0x48a833 | FUN_0048a833 | 3011 | ✓ |  | collision_query.c, customer_service.c, customer_service.h (+9) |
| 0x48b6ad | FUN_0048b6ad | 407 | ✓ |  | dungeon_party.c, scene_pause.c |
| 0x48b844 | FUN_0048b844 | 12 | ✓ |  | scene1_sim.c |
| 0x48cbf6 | FUN_0048cbf6 | 470 | ✓ |  | dungeon_party.c |
| 0x48cdcc | FUN_0048cdcc | 2058 | ✓ |  | dungeon_party.c, scene1_player_ctrl.c |
| 0x48d5d6 | FUN_0048d5d6 | 842 | ✓ |  | chara_skills.h, dungeon_party.c |
| 0x48d954 | FUN_0048d954 | 67 | ✓ |  | scene_day_end.c, scene_day_end.h, scene_gameover.c |
| 0x48d997 | FUN_0048d997 | 218 | ✓ |  | scene_day_end.c, scene_pause.c, scene_pause.h |
| 0x48da71 | FUN_0048da71 | 394 | ✓ |  | scene1_tutorial_dispatch.h, scene_day_end.c, scene_day_end.h (+2) |
| 0x48dbfb | FUN_0048dbfb | 2209 | ✓ |  | scene1_camera.c, scene1_camera.h, scene1_day_close.h (+4) |
| 0x48e49c | FUN_0048e49c | 2173 | ✓ |  | scene_day_end.c |
| 0x48edee | FUN_0048edee | 137 | ✓ |  | encyclopedia.c, scene_adv_guild.c, scene_day_end.c |
| 0x48ee77 | FUN_0048ee77 | 137 | ✓ |  | encyclopedia.c, scene_adv_guild.c, scene_day_end.c |
| 0x48ef00 | FUN_0048ef00 | 2288 | ✓ |  | main.c, scene_day_end.c, scene_day_end.h |
| 0x48f7f0 | FUN_0048f7f0 | 88 | ✓ |  | dungeon_results.c, dungeon_results.h, scene1_sim.c |
| 0x48f848 | FUN_0048f848 | 52 | ✓ |  | dungeon_results.c, dungeon_results.h |
| 0x48f87c | FUN_0048f87c | 63 | ✓ |  | dungeon_results.c, dungeon_results.h, scene_pause.c |
| 0x48f8bb | FUN_0048f8bb | 118 | ✓ |  | dungeon_results.c, dungeon_results.h |
| 0x48f931 | FUN_0048f931 | 1150 | ✓ |  | dungeon_results.c, dungeon_results.h, sim.c |
| 0x48fdaf | FUN_0048fdaf | 148 | ✓ |  | scene1_display_menu.c |
| 0x48ff7e | FUN_0048ff7e | 21 | ✓ |  | customer_service.c, save_bank.c, save_bank.h |
| 0x4901c2 | FUN_004901c2 | 151 | ✓ |  | main.c, save_bank.c, save_bank.h (+2) |
| 0x490259 | FUN_00490259 | 81 | ✓ |  | save_work.c, save_work.h, scene.c (+3) |
| 0x4902aa | FUN_004902aa | 84 | ✓ |  | save_bank.h, save_work.c, save_work.h (+1) |
| 0x4902fe | FUN_004902fe | 682 | ✓ |  | audio_fade.h, main.c, save_bank.h (+2) |
| 0x4905a8 | FUN_004905a8 | 179 | ✓ |  | main.c, save_io.c, save_io.h (+5) |
| 0x490795 | FUN_00490795 | 139 | ✓ |  | actor_ai.h |
| 0x490820 | FUN_00490820 | 348 | ✓ |  | scene1_records_b_tick.c, scene1_records_b_tick.h |
| 0x49097c | FUN_0049097c | 41 | ✓ |  | actor_ai.h |
| 0x490c78 | FUN_00490c78 | 77 | ✓ |  | actor_ai.c, actor_ai.h, dungeon_party.c (+5) |
| 0x490cc6 | FUN_00490cc6 | 99 | ✓ |  | actor_ai.c, scene1_render.c |
| 0x490d29 | FUN_00490d29 | 84 | ✓ |  | actor_ai.c, scene1_render.c, scene1_render.h |
| 0x490e15 | FUN_00490e15 | 1 | ✓ |  | scene_pause.c |
| 0x490e16 | FUN_00490e16 | 14 | ✓ |  | scene.h, scene_guild.c, scene_guild.h (+1) |
| 0x490e24 | FUN_00490e24 | 17 | ✓ |  | scene_guild.c, scene_guild.h, sim.c |
| 0x490e35 | FUN_00490e35 | 15 | ✓ |  | main.c, scene_guild.c, scene_guild.h |
| 0x491044 | FUN_00491044 | 81 | ✓ |  | tables.c, tables_item.c, tables_item.h |
| 0x491095 | FUN_00491095 | 385 | ✓ |  | tables_item.c, tables_item.h |
| 0x491216 | FUN_00491216 | 85 | ✓ |  | tables_item.c, tables_item.h |
| 0x49126b | FUN_0049126b | 115 | ✓ |  | encyclopedia.c, encyclopedia.h, scene1_display_menu.c |
| 0x4912de | FUN_004912de | 820 | ✓ |  | tables.c, tables_item.c, tables_item.h |
| 0x491612 | FUN_00491612 | 6 | ✓ |  | scene1_display_menu.c |
| 0x49161d | FUN_0049161d | 138 | ✓ |  | scene_guild.c, scene_guild.h, scene_pause.c |
| 0x4916a7 | FUN_004916a7 | 167 | ✓ |  | scene_guild.c |
| 0x49174e | FUN_0049174e | 354 |  |  | scene_guild.c, scene_guild.h, scene_worldmap.c |
| 0x4918b0 | FUN_004918b0 | 191 | ✓ |  | scene_guild.c |
| 0x49196f | FUN_0049196f | 423 | ✓ |  | scene1_display_menu.c |
| 0x491b16 | FUN_00491b16 | 41 | ✓ |  | customer_seller.h, customer_service.c, scene1_display_menu.c (+3) |
| 0x491b3f | FUN_00491b3f | 23 | ✓ |  | main.c |
| 0x491bc0 | FUN_00491bc0 | 544 | ✓ |  | scene_guild.c |
| 0x491de0 | FUN_00491de0 | 1248 | ✓ |  | scene_guild.c |
| 0x4922c0 | FUN_004922c0 | 4950 | ✓ |  | event_probe.h, scene_guild.c, scene_guild.h (+1) |
| 0x493616 | FUN_00493616 | 2613 | ✓ |  | scene_guild.c |
| 0x49404b | FUN_0049404b | 2600 | ✓ |  | scene_guild.c, scene_title.c |
| 0x494a73 | FUN_00494a73 | 561 | ✓ |  | main.c, scene_guild.c, scene_guild.h |
| 0x494ca4 | FUN_00494ca4 | 289 | ✓ |  | dungeon_party.c |
| 0x494dc5 | FUN_00494dc5 | 450 | ✓ |  | dungeon_party.c |
| 0x494f87 | FUN_00494f87 | 261 | ✓ |  | dungeon_party.c |
| 0x495305 | FUN_00495305 | 543 | ✓ |  | dungeon_party.c |
| 0x495a32 | FUN_00495a32 | 522 | ✓ |  | dungeon_party.c |
| 0x4960a4 | FUN_004960a4 | 617 | ✓ |  | dungeon_party.c |
| 0x4964bf | FUN_004964bf | 240 | ✓ |  | dungeon_party.c |
| 0x496a63 | FUN_00496a63 | 517 | ✓ |  | dungeon_party.c |
| 0x496f12 | FUN_00496f12 | 184 | ✓ |  | dungeon_party.c |
| 0x496fca | FUN_00496fca | 196 | ✓ |  | dungeon_party.c |
| 0x49708e | FUN_0049708e | 687 | ✓ |  | dungeon_party.c |
| 0x49755c | FUN_0049755c | 338 | ✓ |  | dungeon_party.c |
| 0x4976ae | FUN_004976ae | 119 | ✓ |  | actor_ai.c |
| 0x4977df | FUN_004977df | 320 | ✓ |  | dungeon_party.c |
| 0x49791f | FUN_0049791f | 868 | ✓ |  | chara_skills.h, dungeon_party.c |
| 0x497c83 | FUN_00497c83 | 439 | ✓ |  | dungeon_party.c |
| 0x497e3a | FUN_00497e3a | 1250 | ✓ |  | dungeon_party.c |
| 0x49831c | FUN_0049831c | 419 | ✓ |  | dungeon_party.c |
| 0x4984bf | FUN_004984bf | 474 | ✓ |  | dungeon_party.c |
| 0x4986ee | FUN_004986ee | 1994 | ✓ |  | dungeon_party.c |
| 0x498ef4 | FUN_00498ef4 | 736 | ✓ |  | audio.c, audio.h, main.c |
| 0x499200 | FUN_00499200 | 219 | ✓ |  | audio.c, audio.h, music.c (+1) |
| 0x49933c | FUN_0049933c | 439 | ✓ |  | actor_ai.c, audio.c, audio.h (+11) |
| 0x4994f3 | FUN_004994f3 | 38 | ✓ |  | audio.c, audio.h |
| 0x499519 | FUN_00499519 | 23 | ✓ |  | actor_ai.c, audio.c, audio.h (+23) |
| 0x499538 | FUN_00499538 | 20 | ✓ |  | music.h |
| 0x49954c | FUN_0049954c | 20 | ✓ |  | music.h |
| 0x499560 | FUN_00499560 | 15 |  |  | music.c, music.h, scene_title.c |
| 0x499579 | FUN_00499579 | 10 | ✓ |  | worker_load.c, worker_load.h |
| 0x499c63 | FUN_00499c63 | 477 | ✓ |  | audio.c, audio.h, audio_fade.h (+3) |
| 0x499fda | FUN_00499fda | 120 | ✓ |  | actor_ai.c, dungeon_party.c, dungeon_party.h |
| 0x49a052 | FUN_0049a052 | 120 | ✓ |  | dungeon_party.c |
| 0x49a2a9 | FUN_0049a2a9 | 120 | ✓ |  | actor_ai.c, dungeon_party.c, dungeon_party.h |
| 0x49a324 | FUN_0049a324 | 127 | ✓ |  | save_io.c, save_io.h, scene_title.c (+1) |
| 0x49a3a3 | FUN_0049a3a3 | 154 | ✓ |  | main.c, music.h, scene_pause.c (+3) |
| 0x49a43d | FUN_0049a43d | 283 | ✓ |  | main.c, save_io.c, save_io.h (+2) |
| 0x49a585 | FUN_0049a585 | 25 | ✓ |  | esc_dispatch.c, esc_dispatch.h |
| 0x49a59e | FUN_0049a59e | 3719 | ✓ |  | audio_fade.h, d3d_pool.h, main.c (+16) |
| 0x49b4f4 | FUN_0049b4f4 | 67 | ✓ |  | scene_title.c, title_continue_picker.c, title_continue_picker.h |
| 0x49b537 | FUN_0049b537 | 31 | ✓ |  | save_picker.c, save_picker.h, scene_pause.c (+3) |
| 0x49b556 | FUN_0049b556 | 2810 | ✓ |  | anchor_trace.c, anchor_trace.h, font_draw.h (+7) |
| 0x49c050 | FUN_0049c050 | 1001 | ✓ |  | anchor_trace.c, anchor_trace.h, scene_pause.c (+3) |
| 0x49c439 | FUN_0049c439 | 523 | ✓ |  | anchor_trace.c, anchor_trace.h, scene_title.c |
| 0x49d2e5 | FUN_0049d2e5 | 64 | ✓ |  | scene_day_end.c, scene_gameover.c, scene_gameover.h |
| 0x49d325 | FUN_0049d325 | 72 | ✓ |  | scene_gameover.c |
| 0x49d36d | FUN_0049d36d | 495 | ✓ |  | scene1_postload.c, scene1_postload.h |
| 0x49d55c | FUN_0049d55c | 840 | ✓ |  | scene_gameover.c, scene_gameover.h |
| 0x49d8a4 | FUN_0049d8a4 | 355 | ✓ |  | scene_gameover.c, scene_gameover.h, scene_title.c (+1) |
| 0x49da9e | FUN_0049da9e | 34 | ✓ |  | scene_day_end.c |
| 0x49db8a | FUN_0049db8a | 487 | ✓ |  | scene_day_end.c, sim.c |
| 0x49de08 | FUN_0049de08 | 6 | ✓ |  | scene_new_game.h |
| 0x49de0e | FUN_0049de0e | 10 | ✓ |  | scene1_player_ctrl.c, scene_adv_guild.c, scene_guild.c (+4) |
| 0x49de20 | FUN_0049de20 | 374 | ✓ |  | event_probe.h, main.c, scene.h (+4) |
| 0x49dfc1 | FUN_0049dfc1 | 409 | ✓ |  | scene_worldmap.c |
| 0x49e163 | FUN_0049e163 | 575 | ✓ |  | scene.h, scene1_day_close.c, scene1_postload.h (+5) |
| 0x49e3a3 | FUN_0049e3a3 | 739 | ✓ |  | main.c, scene.h, scene_worldmap.c (+1) |
| 0x49e686 | FUN_0049e686 | 45 | ✓ |  | main.c, scene_worldmap.c |
| 0x49e6b3 | FUN_0049e6b3 | 203 | ✓ |  | news_daily.c, news_daily.h |
| 0x49e849 | FUN_0049e849 | 350 | ✓ |  | tables_item.c, tables_item.h |
| 0x49e9a7 | FUN_0049e9a7 | 387 | ✓ |  | tables_item.c, tables_kyaku.c, tables_kyaku.h (+3) |
| 0x49eb2a | FUN_0049eb2a | 488 | ✓ |  | tables_item.c, tables_item.h, tables_kyaku.c (+1) |
| 0x49ed12 | FUN_0049ed12 | 99 | ✓ |  | customer_service.c |
| 0x49ed75 | FUN_0049ed75 | 515 | ✓ |  | tables_item.c, tables_item.h |
| 0x49ef78 | FUN_0049ef78 | 64 | ✓ |  | encyclopedia.c, item_use.h, news_daily.c (+1) |
| 0x49efb8 | FUN_0049efb8 | 90 | ✓ |  | encyclopedia.c, encyclopedia.h |
| 0x49f012 | FUN_0049f012 | 851 | ✓ |  | encyclopedia.c, encyclopedia.h, save_bank.h (+2) |
| 0x49f365 | FUN_0049f365 | 1363 | ✓ |  | encyclopedia.c, encyclopedia.h, scene_pause.c (+1) |
| 0x49f8b8 | FUN_0049f8b8 | 2033 | ✓ |  | anchor_trace.c, anchor_trace.h, encyclopedia.c (+3) |
| 0x4a2a03 | FUN_004a2a03 | 13 |  |  | math3d.h, scene1_pass_f.c, scene1_records_b_spawn.c (+3) |
| 0x4a2f35 | FUN_004a2f35 | 13 |  |  | math3d.h |
| 0x4a33d2 | FUN_004a33d2 | 46 |  |  | scene1_render.c, scene1_wide_followup_helpers.c |
| 0x4a3462 | FUN_004a3462 | 46 |  |  | scene1_render.c, scene1_wide_followup_helpers.c |
| 0x4a3537 | FUN_004a3537 | 28 |  |  | scene1_wide_followup_helpers.c |
| 0x4a35d3 | FUN_004a35d3 | 28 |  |  | scene1_records_b_spawn.c, scene1_records_b_spawn.h |
| 0x4a3670 | FUN_004a3670 | 28 |  |  | scene1_wide_followup_helpers.c |
| 0x4a3b52 | FUN_004a3b52 | 328 | ✓ |  | math3d.h |
| 0x4a3ee8 | FUN_004a3ee8 | 148 | ✓ |  | main.c, math3d.h, mesh_draw.c (+2) |
| 0x4a4454 | FUN_004a4454 | 13 |  |  | math3d.h |
| 0x4a4f52 | FUN_004a4f52 | 13 |  |  | math3d.h |
| 0x4aaad7 | FUN_004aaad7 | 278 | ✓ |  | mesh.c, mesh.h |
| 0x4c75e3 | FUN_004c75e3 | 4634 | ✓ |  | mesh_draw.c, scene1_emit_record.c |
| 0x4c8f74 | FUN_004c8f74 | 704 | ✓ |  | mesh_load.c |
| 0x4cdd9f | FUN_004cdd9f | 221 | ✓ |  | math3d.c, math3d.h |
| 0x4f8730 | FUN_004f8730 | 232 | ✓ |  | math3d.h |
| 0x5031e4 | FUN_005031e4 | 9 | ✓ |  | customer_service.c, scene1_particles_tick.c, scene1_records_b_spawn.c (+1) |
| 0x5036af | FUN_005036af | 47 | ✓ |  | storage.c, tables_news.h |
| 0x5038b0 | FUN_005038b0 | 19 | ✓ |  | scene1_dialogue_load.c, scene1_overlay_table.c, scene1_overlay_table.h (+1) |
| 0x5038ff | FUN_005038ff | 82 | ✓ |  | audio.c, customer_service.c, news_daily.c (+8) |
| 0x503954 | __ftol | 39 | ✓ |  | scene1_chr_prepass.c, scene1_chr_walker.c, scene1_chr_walker.h (+2) |
| 0x503994 | FUN_00503994 | 9 | ✓ |  | audio_fade.c, audio_fade.h, render_quad.c (+5) |
| 0x503a44 | FUN_00503a44 | 9 | ✓ |  | render_quad.c, scene1_dialogue_run.c, scene1_maplight.c (+8) |
| 0x503c2b | FUN_00503c2b | 77 | ✓ |  | scene1_dialogue.c |
| 0x503d03 | FUN_00503d03 | 11 | ✓ |  | scene1_dialogue.c, scene1_overlay_table.c, tables_item.c |
| 0x503dd0 | FUN_00503dd0 | 10 | ✓ |  | scene1_overlay.h, scene1_overlay_helpers.c, scene1_records_b_spawn.c (+1) |
| 0x503de4 | FUN_00503de4 | 344 | ✓ |  | storage.c |
| 0x503f3c | FUN_00503f3c | 140 | ✓ |  | storage.c |
| 0x504076 | FUN_00504076 | 106 | ✓ |  | scene1_preload.c |
| 0x5041ec | FUN_005041ec | 10 | ✓ |  | main.c, rng.h |
| 0x5041f6 | FUN_005041f6 | 30 | ✓ |  | customer_haggle.h, customer_roster.c, customer_service.c (+12) |
| 0x5045eb | FUN_005045eb | 220 | ✓ |  | main.c, prewindow.h, rng.h |
| 0x50bcff | FUN_0050bcff | 194 | ✓ |  | rng.h |

## runtime-proven (0)

_None — no runtime evidence is bound (INVENTORY ≠ PARITY). The 2026-07 proof-bundle bindings were retired on 2026-10-13; re-verify under the in-process retail host._

