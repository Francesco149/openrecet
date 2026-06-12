# Trace Studio v3 native viewer — build via `nix develop --command make`.
# Dear ImGui source comes from $IMGUI_SRC (pinned in flake.nix). 32-bit (i686) so
# the process matches the real 32-bit d3d8.dll the replay core loads.
CXX    := i686-w64-mingw32-g++
CC     := i686-w64-mingw32-gcc
IMGUI  ?= $(IMGUI_SRC)

CXXFLAGS := -O2 -g -std=c++17 -Wall -Wno-unused-variable \
            -I. -I$(IMGUI) -I$(IMGUI)/backends \
            -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
# d3d9 hosts the UI; d3d8 is the replay core's device. static libstdc++/libgcc so the
# exe runs without the mingw runtime DLLs on PATH.
LIBS := -ld3d9 -ld3d8 -ldxguid -luuid -lgdi32 -ldwmapi -limm32 -luser32 -lole32 -loleaut32 \
        -static -static-libgcc -static-libstdc++
# replay core (C, real d3d8) — compiled with gcc, linked into the C++ viewer.
REPLAY_DIR := ../replay
CCFLAGS := -O2 -g -std=c11 -Wall -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DDIRECT3D_VERSION=0x0800

IMGUI_SRCS := $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
              $(IMGUI)/imgui_widgets.cpp \
              $(IMGUI)/backends/imgui_impl_dx9.cpp $(IMGUI)/backends/imgui_impl_win32.cpp
IMGUI_OBJ  := $(notdir $(IMGUI_SRCS:.cpp=.o))

VPATH := $(IMGUI):$(IMGUI)/backends

all: viewer.exe

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

replay_core.o: $(REPLAY_DIR)/replay_core.c $(REPLAY_DIR)/replay_core.h
	$(CC) $(CCFLAGS) -c -o $@ $<

viewer.exe: viewer.cpp $(IMGUI_OBJ) replay_core.o
	$(CXX) $(CXXFLAGS) -I$(REPLAY_DIR) -o $@ $^ $(LIBS)

clean:
	rm -f viewer.exe $(IMGUI_OBJ) replay_core.o
.PHONY: all clean
