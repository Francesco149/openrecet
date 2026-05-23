# tools/dump-our-mesh/Makefile — host Linux tool that writes our
# parser's mesh output in retail-dump format. Build with `make` from
# inside `nix develop`; output is `build/dump-our-mesh`.
#
# Same shape as tests/Makefile: ASan + UBSan + -I../../src + a small
# subset of openrecet sources (xfile + mesh + math3d, no Win32 layer).
# The dumper has no _WIN32 surface so it compiles cleanly under host
# gcc and runs on Linux.

CC      ?= gcc
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I../../src \
           $(SAN) -MMD -MP
LDFLAGS := $(SAN)

SRCS := main.c ../../src/xfile.c ../../src/mesh.c ../../src/math3d.c
OBJS := $(addprefix build/,$(notdir $(SRCS:.c=.o)))
DEPS := $(OBJS:.o=.d)
OUT  := build/dump-our-mesh

.PHONY: all clean
all: $(OUT)

build/main.o: main.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: ../../src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lm

build:
	@mkdir -p $@

clean:
	rm -rf build/

-include $(DEPS)
