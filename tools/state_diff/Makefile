# tools/state_diff/Makefile — build the local-oracle binary used by
# the state-forcing differential tests.
#
#   make           # builds build/oracle
#   make clean
#
# Links rng.c + audio_fade.c from the main src tree, plus oracle.c
# itself. Host gcc, no sanitizers (oracle is invoked by lcg_fade.py
# under harness control, not part of the unit-test suite). Stays in
# its own build dir so the parent tests/build isn't polluted.

CC      ?= gcc
CFLAGS  := -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter \
           -I../../src \
           -MMD -MP
LDFLAGS :=

SRCS := oracle.c ../../src/rng.c ../../src/audio_fade.c
OBJS := $(addprefix build/, oracle.o rng.o audio_fade.o)
DEPS := $(OBJS:.o=.d)

OUT  := build/oracle

.PHONY: all clean
all: $(OUT)

build:
	@mkdir -p $@

build/oracle.o: oracle.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/rng.o: ../../src/rng.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/audio_fade.o: ../../src/audio_fade.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lm

clean:
	rm -rf build/

-include $(DEPS)
