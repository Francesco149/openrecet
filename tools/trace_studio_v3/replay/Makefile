# Trace Studio v3 replayer — build via `nix develop --command make`.
CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -g -std=c11 -Wall -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DDIRECT3D_VERSION=0x0800
LIBS    := -ld3d8 -ldxguid -luuid -lgdi32 -luser32 -static-libgcc

all: replay.exe
replay.exe: replay.c replay_core.c replay_core.h ../format/orv3_format.h
	$(CC) $(CFLAGS) -o $@ replay.c replay_core.c $(LIBS)

# GX-05 acceptance: the reader fails safely on truncated/corrupt containers. #includes
# replay_core.c and drives step() headless (do_res=do_calls=0 ⇒ no device). Built on
# demand by test_corrupt_reader.py.
corrupt_fuzz.exe: corrupt_fuzz.c replay_core.c replay_core.h ../format/orv3_format.h
	$(CC) $(CFLAGS) -o $@ corrupt_fuzz.c $(LIBS)

clean:
	rm -f replay.exe corrupt_fuzz.exe
.PHONY: all clean
