/*
 * retail_image.c — see retail_image.h.
 */
#include "retail_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t va, vsize, raw_off, raw_size; } section_t;

static FILE      *s_fp;
static int        s_tried;
static uint32_t   s_base;
static section_t  s_sec[32];
static int        s_nsec;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int open_image(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    uint8_t h[0x400];
    if (fread(h, 1, sizeof h, fp) != sizeof h || h[0] != 'M' || h[1] != 'Z') { fclose(fp); return 0; }
    const uint32_t pe = rd32(h + 0x3c);
    if (pe + 0xf8 > sizeof h || memcmp(h + pe, "PE\0\0", 4) != 0) { fclose(fp); return 0; }
    const int nsec = rd16(h + pe + 6);
    const uint32_t opt = rd16(h + pe + 20);
    s_base = rd32(h + pe + 24 + 28);                         /* ImageBase (PE32) */
    const uint32_t tab = pe + 24 + opt;
    s_nsec = 0;
    for (int i = 0; i < nsec && s_nsec < (int)(sizeof s_sec / sizeof s_sec[0]); i++) {
        const uint8_t *e = h + tab + 40u * (uint32_t)i;
        if (tab + 40u * (uint32_t)(i + 1) > sizeof h) break;
        s_sec[s_nsec++] = (section_t){ rd32(e + 12), rd32(e + 8), rd32(e + 20), rd32(e + 16) };
    }
    s_fp = fp;
    return 1;
}

int retail_image_byte(uint32_t va, uint8_t *out)
{
    if (!s_tried) {
        s_tried = 1;
        const char *env = getenv("OPENRECET_RETAIL_EXE");
        if (!(env != NULL && *env != 0 && open_image(env)) &&
            !open_image("recettear.exe") && !open_image("recettear.unpacked.exe"))
            fprintf(stderr, "retail_image: no retail exe readable (OPENRECET_RETAIL_EXE / recettear.exe) — "
                            "reads past the engine's tables see 0\n");
    }
    if (s_fp == NULL) return 0;
    for (int i = 0; i < s_nsec; i++) {
        const section_t *s = &s_sec[i];
        const uint32_t lo = s_base + s->va;
        if (va < lo || va - lo >= ((s->vsize + 0xfffu) & ~0xfffu))
            continue;
        const uint32_t off = va - lo;
        *out = 0;
        if (off < s->raw_size && off < s->vsize &&
            fseek(s_fp, (long)(s->raw_off + off), SEEK_SET) == 0 && fread(out, 1, 1, s_fp) != 1)
            *out = 0;
        return 1;
    }
    return 0;
}

int retail_image_u32(uint32_t va, uint32_t *out)
{
    uint8_t b[4];
    for (int i = 0; i < 4; i++)
        if (!retail_image_byte(va + (uint32_t)i, &b[i])) return 0;
    *out = rd32(b);
    return 1;
}

int retail_image_cstr(uint32_t va, char *buf, size_t cap)
{
    if (cap == 0) return 0;
    size_t n = 0;
    for (; n + 1 < cap; n++) {
        uint8_t c;
        if (!retail_image_byte(va + (uint32_t)n, &c)) { buf[0] = 0; return 0; }
        if (c == 0) break;
        buf[n] = (char)c;
    }
    buf[n] = 0;
    return 1;
}
