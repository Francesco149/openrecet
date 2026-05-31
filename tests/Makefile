# tests/Makefile — Linux unit tests for the portable decoders.
#
# Built with HOST gcc (not mingw) + ASan + UBSan. Run with
#   `make -C tests run`
# from inside the nix dev shell.  Skip vendor-dependent tests gracefully
# when vendor/original/ is absent.
#
# The decoders under test are pure C with no Win32 surface, so they can
# compile and execute under the sanitizer without any wine/WSLInterop
# dance. The Win32 layer (main.c, sprite.c, layers.c, input.c) is not
# tested here.

CC      ?= gcc
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I../src \
           -DOPENRECET_ROOT=\"$(abspath ..)\" \
           $(SAN) \
           -MMD -MP
LDFLAGS := $(SAN)

SRCS_DECODERS := ../src/bmp.c ../src/tga.c ../src/bmp_lzw.c ../src/lnkdatas_hash.c ../src/lnk_lzss.c \
                 ../src/recet_ini.c ../src/input.c ../src/input_trace.c \
                 ../src/input_segtrace.c \
                 ../src/anchor_trace.c \
                 ../src/call_trace.c \
                 ../src/rng.c ../src/math3d.c ../src/prewindow.c ../src/tick.c \
                 ../src/render_quad.c ../src/scene.c ../src/scene_title.c ../src/scene_buy.c ../src/scene_floor.c ../src/scene_jutan.c ../src/scene_pause.c ../src/scene_walls.c ../src/scene_worldmap.c ../src/sim.c ../src/music.c ../src/audio.c ../src/audio_mci.c ../src/audio_fade.c ../src/audio_se_names.c ../src/save_bank.c ../src/save_io.c ../src/settings.c ../src/sysassets.c ../src/nowloading.c ../src/worker_load.c ../src/fade.c ../src/font.c ../src/font_atlas.c ../src/font_alloc.c ../src/font_upload.c \
                 ../src/tables_buysell.c ../src/tables_chara.c ../src/tables_config.c \
                 ../src/tables_enemy.c ../src/tables_enemylist.c ../src/tables_event.c \
                 ../src/tables_gousei.c ../src/tables_item.c \
                 ../src/tables_kyaku.c ../src/tables_news.c ../src/tables_oder.c ../src/tables_model.c \
                 ../src/tables_snews.c ../src/tables_stage.c ../src/tables_tuto.c \
                 ../src/xfile.c ../src/mesh.c ../src/mesh_load.c ../src/mesh_draw.c \
                 ../src/scene_table.c ../src/scene_map_meshes.c ../src/scene_sc1.c ../src/stage_state.c \
                 ../src/stage_palette.c \
                 ../src/scene1_records.c ../src/scene1_spawn.c ../src/scene1_particles_tick.c \
                 ../src/scene1_records_c_tick.c ../src/scene1_records_c_spawn.c \
                 ../src/scene1_records_b_spawn.c ../src/scene1_records_b_tick.c \
                 ../src/scene1_combat_sm.c \
                 ../src/scene1_overlay.c ../src/scene1_overlay_helpers.c \
                 ../src/scene1_overlay_table.c \
                 ../src/scene1_dungeon_clear_banner.c \
                 ../src/scene1_fx_overlays.c \
                 ../src/scene1_postload.c ../src/scene1_preload.c ../src/scene1_sim.c ../src/scene1_camera.c \
                 ../src/scene1_per_frame_open.c \
                 ../src/scene1_shop_walker_helpers.c \
                 ../src/scene1_walker_pass_init.c \
                 ../src/scene1_maplight.c \
                 ../src/scene1_wide_followup_helpers.c \
                 ../src/stage_load_pulse.c \
                 ../src/debug_param_tick.c \
                 ../src/title_save_dialog.c \
                 ../src/scene_new_game.c \
                 ../src/stage_gate.c \
                 ../src/xp_curve.c \
                 ../src/stage_post_load.c \
                 ../src/d3d_pool.c \
                 ../src/chara_equip.c \
                 ../src/chara_skills.c \
                 ../src/npc_schedule.c \
                 ../src/scene1_hud.c \
                 ../src/chr_sprite_meta.c \
                 ../src/scene1_chr_sprite.c \
                 ../src/scene1_chr_walker.c \
                 ../src/scene1_chr_prepass.c \
                 ../src/scene1_player_ctrl.c \
                 ../src/scene1_intro_events.c \
                 ../src/collision_mesh.c ../src/collision_query.c ../src/collision_resolve.c \
                 ../src/sha256.c ../src/se_pack.c
SRCS_TESTS    := test_main.c test_bmp.c test_tga.c test_bmp_lzw.c test_lnkdatas_hash.c test_lnk_lzss.c \
                 test_recet_ini.c test_input_poll.c test_input_trace.c test_input_segtrace.c test_anchor_trace.c \
                 test_rng.c test_math3d.c test_prewindow.c test_tick.c \
                 test_render_quad.c test_scene.c test_scene_title.c test_scene_title_sim.c test_scene_title_settings.c test_scene_buy.c test_scene_floor.c test_scene_jutan.c test_scene_pause.c test_scene_walls.c test_scene_worldmap.c test_sim.c test_music.c test_audio.c test_audio_mci.c test_audio_fade.c test_audio_trace.c test_audio_se_table.c test_save_bank.c test_save_io.c test_settings.c test_sysassets.c test_nowloading.c test_worker_load.c test_fade.c test_font.c test_font_atlas.c test_font_alloc.c test_font_upload.c \
                 test_tables_buysell.c test_tables_chara.c test_tables_config.c \
                 test_tables_enemy.c test_tables_enemylist.c test_tables_event.c \
                 test_tables_gousei.c test_tables_item.c \
                 test_tables_kyaku.c test_tables_news.c test_tables_oder.c test_tables_model.c \
                 test_tables_snews.c test_tables_stage.c test_tables_tuto.c \
                 test_xfile.c test_mesh.c test_mesh_load.c test_mesh_draw.c \
                 test_scene_table.c test_scene_map_meshes.c test_scene_sc1.c test_stage_state.c \
                 test_stage_palette.c \
                 test_scene1_spawn.c test_scene1_particles_tick.c test_scene1_postload.c test_scene1_preload.c test_scene1_sim.c \
                 test_scene1_records_c_tick.c test_scene1_records_c_spawn.c \
                 test_scene1_records_b_spawn.c test_scene1_records_b_tick.c \
                 test_scene1_combat_sm.c \
                 test_scene1_overlay.c test_scene1_overlay_table.c \
                 test_scene1_camera.c test_scene1_shop_walker.c \
                 test_scene1_per_frame_open.c \
                 test_scene1_walker_pass_init.c \
                 test_scene1_maplight.c \
                 test_scene1_wide_followup_helpers.c \
                 test_scene1_dungeon_clear_banner.c \
                 test_scene1_fx_overlays.c \
                 test_stage_load_pulse.c \
                 test_debug_param_tick.c \
                 test_title_save_dialog.c \
                 test_scene_new_game.c \
                 test_stage_gate.c \
                 test_xp_curve.c \
                 test_stage_post_load.c \
                 test_d3d_pool.c \
                 test_chara_equip.c \
                 test_chara_skills.c \
                 test_npc_schedule.c \
                 test_scene1_hud.c \
                 test_chr_sprite_meta.c \
                 test_scene1_chr_sprite.c \
                 test_scene1_chr_walker.c \
                 test_scene1_chr_prepass.c \
                 test_scene1_player_ctrl.c \
                 test_collision_mesh.c test_collision_query.c test_collision_resolve.c \
                 test_se_pack.c

OBJS := $(addprefix build/,$(notdir $(SRCS_DECODERS:.c=.o)) $(SRCS_TESTS:.c=.o))
DEPS := $(OBJS:.o=.d)
OUT  := build/run_tests

# ── diff harness (tools/diff_test.py) ───────────────────────────────────
#
# Builds a host-loadable shared library that exports a stable C ABI for
# our ported pure-math functions, so the Phase D diff orchestrator can
# call them via ctypes and compare their output against the same call
# dispatched into retail via Frida.
#
# Separate object dir + no sanitizers — ASan-instrumented .so would
# require LD_PRELOADing the runtime, complicating the Python launcher
# for zero benefit (the unit-test suite already exercises the same
# code under ASan/UBSan, so the diff path doesn't need it).
#
# Extend DIFF_SRCS as new diff targets land.

DIFF_SRCS    := ../src/diff_entry.c ../src/rng.c ../src/audio_fade.c \
                ../src/stage_gate.c diff_stubs.c
DIFF_OBJS    := $(addprefix build/diff/,$(notdir $(DIFF_SRCS:.c=.o)))
DIFF_DEPS    := $(DIFF_OBJS:.o=.d)
DIFF_OUT     := build/libengine_diff.so
DIFF_CFLAGS  := -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
                -fPIC -I../src -MMD -MP

.PHONY: all run diff clean
all: $(OUT)

run: $(OUT)
	./$(OUT)

diff: $(DIFF_OUT)

build/%.o: ../src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lm

build/diff/%.o: ../src/%.c | build/diff
	$(CC) $(DIFF_CFLAGS) -c -o $@ $<

build/diff/%.o: %.c | build/diff
	$(CC) $(DIFF_CFLAGS) -c -o $@ $<

$(DIFF_OUT): $(DIFF_OBJS)
	$(CC) -shared -o $@ $(DIFF_OBJS) -lm

build:
	@mkdir -p $@

build/diff:
	@mkdir -p $@

clean:
	rm -rf build/

-include $(DEPS)
-include $(DIFF_DEPS)
