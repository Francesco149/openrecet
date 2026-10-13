/* retail_map.h — map recettear.unpacked.exe at its fixed ImageBase inside a
 * host process WITHOUT running its entry point ("retail as a library").
 * Wine/Windows populate 0x3c0000..0x8b0000 early, so a plain VirtualAlloc at
 * 0x400000 fails. Instead the host links at 0x200000 WITHOUT ASLR
 * (-Wl,--disable-dynamicbase) and carries RETAIL_HOLE_DEFINE, a giant .bss
 * that the PE loader reserves for us; retail_map() then VirtualProtect()s it
 * RWX and copies retail's sections in. */
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host exes link at a low base (-Wl,--image-base,0x200000) and carry this
 * bss hole so the PE loader reserves the retail range for us before wine (or
 * Windows) puts heaps / mapped files there. */
#define RETAIL_HOLE_DEFINE \
    __attribute__((aligned(4096))) char g_retail_hole[0x9900000];

typedef struct {
    uint8_t *base;
    uint32_t size;
    uint32_t entry;
    int n_imports_resolved, n_imports_stubbed;
} retail_image_t;

/* Any import we could not resolve lands here: crash loudly with the slot. */
static void __cdecl retail_unresolved_import(void)
{
    fprintf(stderr, "[retail_map] call through UNRESOLVED import\n");
    ExitProcess(99);
}

static int retail_map(const char *path, retail_image_t *out, int resolve_imports)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "open %s failed\n", path); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *file = malloc(n);
    if (fread(file, 1, n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)file;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    uint32_t ib = nt->OptionalHeader.ImageBase, sz = nt->OptionalHeader.SizeOfImage;
    uint8_t *base = VirtualAlloc((void *)(uintptr_t)ib, sz, MEM_RESERVE | MEM_COMMIT,
                                 PAGE_EXECUTE_READWRITE);
    if (!base) {
        /* Range already owned by our own image (the RETAIL_HOLE bss): make it RWX. */
        DWORD old;
        if (VirtualProtect((void *)(uintptr_t)ib, sz, PAGE_EXECUTE_READWRITE, &old)) {
            base = (uint8_t *)(uintptr_t)ib;
            memset(base, 0, sz);
        }
    }
    if (base != (uint8_t *)(uintptr_t)ib) {
        fprintf(stderr, "VirtualAlloc @%08x size %08x failed (got %p, err %lu)\n",
                ib, sz, base, GetLastError());
        return -1;
    }
    memcpy(base, file, nt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t raw = sec[i].SizeOfRawData;
        if (raw > sec[i].Misc.VirtualSize && sec[i].Misc.VirtualSize) raw = sec[i].Misc.VirtualSize;
        if (raw) memcpy(base + sec[i].VirtualAddress, file + sec[i].PointerToRawData, raw);
    }
    out->n_imports_resolved = out->n_imports_stubbed = 0;
    IMAGE_DATA_DIRECTORY *idd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + idd->VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(base + d->Name);
        HMODULE h = resolve_imports ? LoadLibraryA(dll) : NULL;
        IMAGE_THUNK_DATA32 *ilt = (void *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *iat = (void *)(base + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++) {
            FARPROC p = NULL;
            if (h) {
                if (ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32)
                    p = GetProcAddress(h, (LPCSTR)(uintptr_t)(ilt->u1.Ordinal & 0xffff));
                else
                    p = GetProcAddress(h, (LPCSTR)((IMAGE_IMPORT_BY_NAME *)(base + ilt->u1.AddressOfData))->Name);
            }
            if (p) { iat->u1.Function = (uint32_t)(uintptr_t)p; out->n_imports_resolved++; }
            else   { iat->u1.Function = (uint32_t)(uintptr_t)retail_unresolved_import; out->n_imports_stubbed++; }
        }
    }
    out->base = base; out->size = sz;
    out->entry = ib + nt->OptionalHeader.AddressOfEntryPoint;
    free(file);
    return 0;
}

/* Call a retail function at `va` with no stack args (cdecl/stdcall-0 alike). */
static inline uint32_t retail_call0(uint32_t va) { return ((uint32_t (__cdecl *)(void))(uintptr_t)va)(); }
