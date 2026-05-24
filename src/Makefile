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
             -MMD -MP
LDFLAGS   ?= -static-libgcc
LIBS      ?= -ld3d8 -ldinput8 -ldxguid -ldsound -lwinmm \
             -lgdi32 -luser32 -lkernel32 -lole32 -ladvapi32 -lshell32 \
             -lpsapi

SRCS      := $(wildcard *.c)
OBJS      := $(SRCS:.c=.o)
DEPS      := $(OBJS:.o=.d)

# SE WAVs are embedded as a custom-named ("WAVE") PE resource type so
# audio_init's FindResourceA path finds them at runtime — same shape as
# the original engine. The .rc is auto-generated from
# vendor/unpacked/se-extracted/ by tools/extract/se-rc.py (which expects
# that directory to be populated by tools/extract/se-wavs.py first).
# The .rc and the .res.o are gitignored; both regenerate as needed.
SE_RC     := se.rc
SE_RES_O  := se.res.o
RCFLAGS   ?= -O coff -c 65001
OBJS      += $(SE_RES_O)

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

# SE WAV embedding: regenerate .rc when the generator changes; rebuild
# .res.o when the .rc or any source WAV changes (windres tracks file
# mtimes on the LHS but not the WAVs it pulls in, so list one as a
# proxy dep to force re-link when the extract dir is updated).
$(SE_RC): ../tools/extract/se-rc.py
	python3 $< $@

$(SE_RES_O): $(SE_RC) ../tools/extract/se-rc.py
	$(WINDRES) $(RCFLAGS) -i $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(SE_RC) $(SE_RES_O) $(OUT) $(OUT_DEBUG)

-include $(DEPS)
