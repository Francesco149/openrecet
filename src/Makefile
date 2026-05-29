# OpenRecet — 32-bit Win32 PE build via i686-w64-mingw32-gcc.
# Invoke from inside `nix develop`. The mingw32 toolchain is on PATH.

# The nix dev shell exports CC=gcc (the host compiler). We force the
# mingw cross-compiler here so `make` does the right thing automatically.
# Still overridable on the command line: `make CC=foo`.
CC        = i686-w64-mingw32-gcc
WINDRES    = i686-w64-mingw32-windres
STRIP      = i686-w64-mingw32-strip

CFLAGS    ?= -std=c11 -O2 -g \
             -Wall -Wextra -Wno-unused-parameter \
             -DUNICODE=0 -D_UNICODE=0 \
             -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 \
             -include d3d_trace_macros.h \
             -MMD -MP
LDFLAGS   ?= -static-libgcc
LIBS      ?= -ld3d8 -ldinput8 -ldxguid -ldsound -lwinmm \
             -lgdi32 -luser32 -lkernel32 -lole32 -ladvapi32 -lshell32 \
             -lpsapi

SRCS      := $(wildcard *.c)
OBJS      := $(SRCS:.c=.o)
DEPS      := $(OBJS:.o=.d)

# NOTE: SE sound-effect WAVs are NOT embedded in the binary. The shipped
# exe carries no proprietary audio; src/se_pack.c extracts the SE from
# the user's own retail recettear.exe at runtime and caches them
# (docs/formats/se-pack.md). The old build-time embedding (se.rc →
# se.res.o via tools/extract/se-rc.py) was retired in the public-release
# detour; tools/extract/se-wavs.py is kept as a dev/reference extractor.

# Two PE outputs from the same .o set:
#   openrecet.exe       — GUI subsystem (linker flag -mwindows). The
#                         distributable binary. No console window pops up
#                         on launch; stdio is unwired to anything visible.
#   openrecet-debug.exe — Console subsystem (no -mwindows). Same code,
#                         different PE header subsystem field. Windows
#                         wires stdin/out/err to the launching shell's
#                         TTY (cmd, fish-via-WSL-interop, etc.), so any
#                         fprintf actually shows up. Use this for
#                         interactive dev runs when you want to see the
#                         log lines.
OUT       ?= ../build/openrecet.exe
OUT_DEBUG ?= ../build/openrecet-debug.exe

.PHONY: all clean
all: $(OUT) $(OUT_DEBUG)

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) -mwindows $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(OUT_DEBUG): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) se.rc se.res.o $(OUT) $(OUT_DEBUG)

-include $(DEPS)
