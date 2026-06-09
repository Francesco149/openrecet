/* memsnap.c — see memsnap.h. Walks our own in-memory PE header; no toolchain
 * metadata needed at runtime. The differ (tools/phase_census.py) reconstructs
 * link-time VAs as ImageBase + section RVA + offset, which is the address
 * space `nm build/openrecet.exe` reports — so census hits resolve to symbol
 * names without a map file. */
#include "memsnap.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>

int memsnap_dump(const char *dir, uint32_t frame)
{
    const uint8_t *base = (const uint8_t *)GetModuleHandleA(NULL);
    if (!base) return 0;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const IMAGE_NT_HEADERS32 *nt =
        (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);

    char path[512];
    snprintf(path, sizeof path, "%s\\memsnap_%05u.json", dir, (unsigned)frame);
    FILE *ix = fopen(path, "wb");
    if (!ix) return 0;
    fprintf(ix, "{\"frame\":%u,\"link_base\":%lu,\"sections\":[",
            (unsigned)frame, (unsigned long)nt->OptionalHeader.ImageBase);

    int n = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        char name[9] = {0};
        memcpy(name, sec[i].Name, 8);
        for (char *c = name; *c; c++)          /* filename-safe section name */
            if (*c == '\\' || *c == '/' || *c == ' ') *c = '_';
        uint32_t vsz = sec[i].Misc.VirtualSize;
        if (!vsz) continue;
        snprintf(path, sizeof path, "%s\\memsnap_%05u_%s.bin",
                 dir, (unsigned)frame, name);
        FILE *f = fopen(path, "wb");
        if (!f) continue;
        size_t wrote = fwrite(base + sec[i].VirtualAddress, 1, vsz, f);
        fclose(f);
        if (wrote != vsz) continue;
        fprintf(ix, "%s{\"name\":\"%s\",\"rva\":%lu,\"vsize\":%lu,"
                "\"file\":\"memsnap_%05u_%s.bin\"}",
                n ? "," : "", name,
                (unsigned long)sec[i].VirtualAddress,
                (unsigned long)vsz, (unsigned)frame, name);
        n++;
    }
    fprintf(ix, "]}\n");
    fclose(ix);
    return n;
}

#else /* !_WIN32 — host stub; the op itself is host-tested in input_segtrace */

int memsnap_dump(const char *dir, uint32_t frame)
{
    (void)dir; (void)frame;
    return 0;
}

#endif
