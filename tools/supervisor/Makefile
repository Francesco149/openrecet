# Win32 launcher that owns the openrecet child via a Job Object.
# Build target: ../../build/openrecet-supervisor.exe.
# Run from inside `nix develop` so the mingw cross-compiler is on PATH.
# See run-supervised.c for the design rationale.

CC      = i686-w64-mingw32-gcc
CFLAGS ?= -std=c11 -O2 -g \
          -Wall -Wextra \
          -DUNICODE=0 -D_UNICODE=0 \
          -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 \
          -MMD -MP
LDFLAGS ?= -static-libgcc

OUT    ?= ../../build/openrecet-supervisor.exe
SRCS   := run-supervised.c
OBJS   := $(SRCS:.c=.o)
DEPS   := $(OBJS:.o=.d)

.PHONY: all clean
all: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(OUT)

-include $(DEPS)
