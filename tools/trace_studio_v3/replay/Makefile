# Trace Studio v3 replayer — build via `nix develop --command make`.
CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -g -std=c11 -Wall -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DDIRECT3D_VERSION=0x0800
LIBS    := -ld3d8 -ldxguid -luuid -lgdi32 -luser32 -static-libgcc

all: replay.exe
replay.exe: replay.c ../format/orv3_format.h
	$(CC) $(CFLAGS) -o $@ replay.c $(LIBS)
clean:
	rm -f replay.exe
.PHONY: all clean
