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
                 ../src/rng.c ../src/math3d.c ../src/prewindow.c ../src/tick.c \
                 ../src/render_quad.c ../src/scene.c ../src/scene_title.c ../src/scene_buy.c ../src/scene_floor.c ../src/scene_jutan.c ../src/scene_pause.c ../src/scene_walls.c ../src/scene_worldmap.c ../src/sim.c ../src/music.c ../src/audio.c ../src/audio_mci.c ../src/audio_fade.c ../src/audio_se_names.c ../src/save_bank.c ../src/save_io.c ../src/settings.c ../src/sysassets.c ../src/nowloading.c ../src/worker_load.c ../src/fade.c ../src/font.c ../src/font_atlas.c ../src/font_alloc.c ../src/font_upload.c \
                 ../src/tables_buysell.c ../src/tables_chara.c ../src/tables_config.c \
                 ../src/tables_enemy.c ../src/tables_enemylist.c ../src/tables_event.c \
                 ../src/tables_gousei.c ../src/tables_item.c \
                 ../src/tables_kyaku.c ../src/tables_news.c ../src/tables_oder.c ../src/tables_model.c \
                 ../src/tables_snews.c ../src/tables_stage.c ../src/tables_tuto.c \
                 ../src/xfile.c
SRCS_TESTS    := test_main.c test_bmp.c test_tga.c test_bmp_lzw.c test_lnkdatas_hash.c test_lnk_lzss.c \
                 test_recet_ini.c test_input_poll.c test_input_trace.c \
                 test_rng.c test_math3d.c test_prewindow.c test_tick.c \
                 test_render_quad.c test_scene.c test_scene_title.c test_scene_title_sim.c test_scene_title_settings.c test_scene_buy.c test_scene_floor.c test_scene_jutan.c test_scene_pause.c test_scene_walls.c test_scene_worldmap.c test_sim.c test_music.c test_audio.c test_audio_mci.c test_audio_fade.c test_audio_trace.c test_audio_se_table.c test_save_bank.c test_save_io.c test_settings.c test_sysassets.c test_nowloading.c test_worker_load.c test_fade.c test_font.c test_font_atlas.c test_font_alloc.c test_font_upload.c \
                 test_tables_buysell.c test_tables_chara.c test_tables_config.c \
                 test_tables_enemy.c test_tables_enemylist.c test_tables_event.c \
                 test_tables_gousei.c test_tables_item.c \
                 test_tables_kyaku.c test_tables_news.c test_tables_oder.c test_tables_model.c \
                 test_tables_snews.c test_tables_stage.c test_tables_tuto.c \
                 test_xfile.c

OBJS := $(addprefix build/,$(notdir $(SRCS_DECODERS:.c=.o)) $(SRCS_TESTS:.c=.o))
DEPS := $(OBJS:.o=.d)
OUT  := build/run_tests

.PHONY: all run clean
all: $(OUT)

run: $(OUT)
	./$(OUT)

build/%.o: ../src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lm

build:
	@mkdir -p $@

clean:
	rm -rf build/

-include $(DEPS)
