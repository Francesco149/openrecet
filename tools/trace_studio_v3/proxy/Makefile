# Trace Studio v3 proxy d3d8.dll — build via `nix develop --command make`.
CC        := i686-w64-mingw32-gcc
# Device methods we intercept (everything else auto-forwards). Grows as P0b
# adds capture; keep QI/AddRef/Release/Present (lifetime + frame boundary).
DEVCUSTOM := ["QueryInterface","AddRef","Release","Present","BeginScene","EndScene","Clear","SetRenderState","SetTextureStageState","SetTransform","SetMaterial","SetTexture","SetStreamSource","SetIndices","SetVertexShader","DrawPrimitive","DrawIndexedPrimitive","DrawPrimitiveUP","DrawIndexedPrimitiveUP","SetLight","LightEnable","GetBackBuffer","CreateTexture","SetRenderTarget","CopyRects","CreateVertexBuffer","CreateIndexBuffer"]

CFLAGS    := -O2 -g -std=c11 -Wall -Wno-unused-function \
             -DWINVER=0x0500 -D_WIN32_WINNT=0x0500 -DDIRECT3D_VERSION=0x0800
LDFLAGS   := -shared -static-libgcc -Wl,--kill-at
LIBS      := -ldxguid -luuid

all: d3d8.dll

proxy_generated.h: gen_forwarders.py Makefile
	@echo "  GEN  $@"
	python3 gen_forwarders.py auto '$(DEVCUSTOM)' > $@

d3d8.dll: d3d8_proxy.c proxy_generated.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ d3d8_proxy.c $(LIBS)
	@echo "  built $@:"; i686-w64-mingw32-objdump -p $@ | grep -A3 "Export Table" | head -6

# GX-04 acceptance fixture: a standalone D3D8 exe that drives a same-frame VB mutation
# (binds A,B,A). Links the real d3d8 import lib; at runtime loads the proxy d3d8.dll the
# host test stages next to it. Not part of `all` (built on demand by test_gx04_fixture.py).
gx04_fixture.exe: gx04_fixture.c
	$(CC) $(CFLAGS) -o $@ gx04_fixture.c -ld3d8 -ldxguid -luuid -lgdi32 -luser32 -static-libgcc

# GX-05 acceptance fixture: drives A,B,A,C binds under the forced-hash-collision seam so the
# proxy's byte-compare dedup must keep the 3 distinct contents distinct. Built on demand by
# test_gx05_fixture.py (uses the SAME proxy d3d8.dll — the seam is env-gated at runtime).
gx05_fixture.exe: gx05_fixture.c
	$(CC) $(CFLAGS) -o $@ gx05_fixture.c -ld3d8 -ldxguid -luuid -lgdi32 -luser32 -static-libgcc

clean:
	rm -f d3d8.dll proxy_generated.h gx04_fixture.exe gx05_fixture.exe

.PHONY: all clean
