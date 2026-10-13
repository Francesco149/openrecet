/* rh_shot.c — screenshot the game window at exact frames (--shot-at F[,F..]).
 * PrintWindow(PW_RENDERFULLCONTENT) reads the DWM redirection surface, so it works
 * for the off-screen --quiet window too. Written as PNG (stored deflate blocks, no
 * zlib dependency) to <rundir>\shot_fNNNNNN.png. */
#include "rhost.h"

static uint32_t crc_tab[256];
static uint32_t crc32_upd(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32_upd(0xffffffffu, (const uint8_t *)type, 4); c = crc32_upd(c, d, n) ^ 0xffffffffu;
    uint8_t t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    size_t raw_n = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) { raw[y * (1 + 3 * w)] = 0; memcpy(raw + y * (1 + 3 * w) + 1, rgb + (size_t)y * 3 * w, 3 * w); }
    size_t nblk = (raw_n + 65534) / 65535, z_n = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(z_n), *o = z;
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint32_t len = (uint32_t)(raw_n - off < 65535 ? raw_n - off : 65535);
        *o++ = off + len >= raw_n; *o++ = len & 0xff; *o++ = len >> 8; *o++ = ~len & 0xff; *o++ = (~len >> 8) & 0xff;
        memcpy(o, raw + off, len); o += len;
    }
    be32(o, (b << 16) | a); o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ih[13]; be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)(o - z)); chunk(f, "IEND", NULL, 0);
    fclose(f); free(raw); free(z);
    return 0;
}

static BOOL CALLBACK find_wnd(HWND h, LPARAM lp)
{ DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(h)) { *(HWND *)lp = h; return FALSE; } return TRUE; }

void rh_shot(const char *tag)
{
    HWND hw = NULL; EnumWindows(find_wnd, (LPARAM)&hw);
    if (!hw) { rh_log("shot %s: no window\n", tag); return; }
    RECT rc; GetClientRect(hw, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    HDC wdc = GetDC(hw), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB } };
    void *bits; HBITMAP bm = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bm);
    if (!PrintWindow(hw, mdc, 0x1 /*PW_CLIENTONLY*/ | 0x2 /*PW_RENDERFULLCONTENT*/)) BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
    GdiFlush();
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++) { const uint8_t *p = (const uint8_t *)bits + 4 * i; rgb[3 * i] = p[2]; rgb[3 * i + 1] = p[1]; rgb[3 * i + 2] = p[0]; }
    char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\shot_%s.png", O.rundir, tag);
    write_png(path, rgb, w, h);
    free(rgb); SelectObject(mdc, old); DeleteObject(bm); DeleteDC(mdc); ReleaseDC(hw, wdc);
    rh_log("shot %s %dx%d -> %s\n", tag, w, h, path);
}
