# Trace Studio v3 proxy d3d8.dll — build via `nix develop --command make`.
CC        := i686-w64-mingw32-gcc
# Device methods we intercept (everything else auto-forwards). Grows as P0b
# adds capture; keep QI/AddRef/Release/Present (lifetime + frame boundary).
DEVCUSTOM := ["QueryInterface","AddRef","Release","Present","BeginScene","EndScene","Clear","SetRenderState","SetTextureStageState","SetTransform","SetMaterial","SetTexture","SetStreamSource","SetIndices","SetVertexShader","DrawPrimitive","DrawIndexedPrimitive","DrawPrimitiveUP","DrawIndexedPrimitiveUP","SetLight","LightEnable","GetBackBuffer"]

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

clean:
	rm -f d3d8.dll proxy_generated.h

.PHONY: all clean
