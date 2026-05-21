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
           $(SAN)
LDFLAGS := $(SAN)

SRCS_DECODERS := ../src/bmp.c ../src/tga.c ../src/bmp_lzw.c ../src/lnkdatas_hash.c ../src/lnk_lzss.c \
                 ../src/recet_ini.c ../src/input.c \
                 ../src/rng.c ../src/math3d.c ../src/prewindow.c ../src/tick.c \
                 ../src/render_quad.c \
                 ../src/tables_buysell.c ../src/tables_chara.c ../src/tables_config.c \
                 ../src/tables_enemy.c ../src/tables_enemylist.c ../src/tables_event.c \
                 ../src/tables_gousei.c ../src/tables_item.c \
                 ../src/tables_kyaku.c ../src/tables_news.c ../src/tables_oder.c ../src/tables_model.c \
                 ../src/tables_snews.c ../src/tables_stage.c ../src/tables_tuto.c
SRCS_TESTS    := test_main.c test_bmp.c test_tga.c test_bmp_lzw.c test_lnkdatas_hash.c test_lnk_lzss.c \
                 test_recet_ini.c test_input_poll.c \
                 test_rng.c test_math3d.c test_prewindow.c test_tick.c \
                 test_render_quad.c \
                 test_tables_buysell.c test_tables_chara.c test_tables_config.c \
                 test_tables_enemy.c test_tables_enemylist.c test_tables_event.c \
                 test_tables_gousei.c test_tables_item.c \
                 test_tables_kyaku.c test_tables_news.c test_tables_oder.c test_tables_model.c \
                 test_tables_snews.c test_tables_stage.c test_tables_tuto.c

OBJS := $(addprefix build/,$(notdir $(SRCS_DECODERS:.c=.o)) $(SRCS_TESTS:.c=.o))
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
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

build:
	@mkdir -p $@

clean:
	rm -rf build/
