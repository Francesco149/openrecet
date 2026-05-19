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
             -DWINVER=0x0500 -D_WIN32_WINNT=0x0500
LDFLAGS   ?= -mwindows -static-libgcc
LIBS      ?= -ld3d8 -ldinput8 -ldxguid -ldsound -lwinmm \
             -lgdi32 -luser32 -lkernel32 -lole32 -ladvapi32 -lshell32

SRCS      := $(wildcard *.c)
OBJS      := $(SRCS:.c=.o)
OUT       ?= ../build/openrecet.exe

.PHONY: all clean
all: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)
