/*
 * se_pack.c — runtime SE cache (see se_pack.h, docs/formats/se-pack.md).
 *
 * The pure serialize/parse layer is platform-independent and unit-tested
 * (tests/test_se_pack.c). The Win32 layer extracts the 110 `WAVE`
 * resources from the user's retail recettear.exe — SteamStub leaves
 * .rsrc unencrypted, so LoadLibraryEx(...AS_DATAFILE) + FindResource on
 * the packed exe Just Works (docs/reference/vendor-exe.md).
 */
#include "se_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── pure format layer ─────────────────────────────────────────────── */

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t *se_pack_serialize(const se_blob_t *blobs, uint32_t count,
                           const uint8_t sha[SHA256_DIGEST_LEN],
                           size_t *out_len)
{
    size_t table_len = (size_t)count * 8;
    size_t blob_region = 0;
    for (uint32_t i = 0; i < count; i++)
        blob_region += blobs[i].size;

    size_t total = SE_PACK_HEADER_LEN + table_len + blob_region;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return NULL;

    memcpy(buf, SE_PACK_MAGIC, SE_PACK_MAGIC_LEN);
    put_u32(buf + 8, SE_PACK_VERSION);
    put_u32(buf + 12, count);
    memcpy(buf + 16, sha, SHA256_DIGEST_LEN);

    size_t entry_off = SE_PACK_HEADER_LEN;
    size_t blob_off = SE_PACK_HEADER_LEN + table_len;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size = blobs[i].size;
        if (size == 0) {
            put_u32(buf + entry_off, 0);
            put_u32(buf + entry_off + 4, 0);
        } else {
            put_u32(buf + entry_off, (uint32_t)blob_off);
            put_u32(buf + entry_off + 4, size);
            memcpy(buf + blob_off, blobs[i].data, size);
            blob_off += size;
        }
        entry_off += 8;
    }

    if (out_len) *out_len = total;
    return buf;
}

int se_pack_parse(const uint8_t *data, size_t len, uint32_t expect_count,
                  se_blob_t *out_blobs, uint8_t sha_out[SHA256_DIGEST_LEN])
{
    if (len < SE_PACK_HEADER_LEN) return 1;
    if (memcmp(data, SE_PACK_MAGIC, SE_PACK_MAGIC_LEN) != 0) return 1;
    if (get_u32(data + 8) != SE_PACK_VERSION) return 1;
    if (get_u32(data + 12) != expect_count) return 1;

    size_t table_len = (size_t)expect_count * 8;
    if (len < SE_PACK_HEADER_LEN + table_len) return 1;

    size_t entry_off = SE_PACK_HEADER_LEN;
    for (uint32_t i = 0; i < expect_count; i++) {
        uint32_t off = get_u32(data + entry_off);
        uint32_t size = get_u32(data + entry_off + 4);
        entry_off += 8;
        if (size == 0) {
            out_blobs[i].data = NULL;
            out_blobs[i].size = 0;
            continue;
        }
        /* bounds: blob must sit wholly inside the file and past the
         * header+table (no overlap with metadata). */
        if (off < SE_PACK_HEADER_LEN + table_len) return 1;
        if ((size_t)off + size > len) return 1;
        out_blobs[i].data = data + off;
        out_blobs[i].size = size;
    }

    if (sha_out) memcpy(sha_out, data + 16, SHA256_DIGEST_LEN);
    return 0;
}

/* ─── Win32 runtime orchestration ──────────────────────────────────── */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio_se_names.h"   /* AUDIO_SE_COUNT, audio_se_resource_ids,
                                 AUDIO_SE_RESOURCE_TYPE */

static uint8_t  *g_pack_image = NULL;
static size_t    g_pack_len   = 0;
static se_blob_t g_blobs[AUDIO_SE_COUNT];
static int       g_acquired = 0;

static int locate_retail_exe(char *out, DWORD cap)
{
    DWORD n = GetEnvironmentVariableA("OPENRECET_RETAIL_EXE", out, cap);
    if (n > 0 && n < cap) return 1;
    /* default: recettear.exe in the working directory (the game dir,
     * where assets resolve from). */
    if (cap < sizeof "recettear.exe") return 0;
    memcpy(out, "recettear.exe", sizeof "recettear.exe");
    return 1;
}

/* Read the whole file at `path` into a malloc'd buffer. Returns the
 * buffer (caller frees) and sets *out_len, or NULL on failure. */
static uint8_t *read_whole_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

static int hash_file(const char *path, uint8_t out[SHA256_DIGEST_LEN])
{
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    sha256_ctx c;
    sha256_init(&c);
    uint8_t chunk[64 * 1024];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
        sha256_update(&c, chunk, got);
    int err = ferror(f);
    fclose(f);
    if (err) return 1;
    sha256_final(&c, out);
    return 0;
}

/* Build %LOCALAPPDATA%\openrecet\se.pack into `out`, creating the
 * openrecet dir if needed. Returns 1 on success. */
static int cache_path(char *out, DWORD cap)
{
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
    if (n == 0 || n >= sizeof base) return 0;

    char dir[MAX_PATH];
    if (_snprintf(dir, sizeof dir, "%s\\openrecet", base) < 0) return 0;
    dir[sizeof dir - 1] = '\0';
    /* best-effort mkdir; ignore ERROR_ALREADY_EXISTS */
    CreateDirectoryA(dir, NULL);

    if (_snprintf(out, cap, "%s\\se.pack", dir) < 0) return 0;
    out[cap - 1] = '\0';
    return 1;
}

/* Extract the WAVE resources from the retail exe and serialize a pack
 * image keyed on `sha`. Returns the image (caller frees) + *out_len, or
 * NULL on failure. */
static uint8_t *extract_pack(const char *exe_path,
                             const uint8_t sha[SHA256_DIGEST_LEN],
                             size_t *out_len)
{
    HMODULE h = LoadLibraryExA(exe_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!h) {
        fprintf(stderr,
                "se_pack: LoadLibraryEx(%s) failed (err=%lu)\n",
                exe_path, (unsigned long)GetLastError());
        return NULL;
    }

    se_blob_t blobs[AUDIO_SE_COUNT];
    int found = 0;
    for (int slot = 0; slot < AUDIO_SE_COUNT; slot++) {
        uint16_t rid = audio_se_resource_ids[slot];
        blobs[slot].data = NULL;
        blobs[slot].size = 0;
        HRSRC r = FindResourceA(h, MAKEINTRESOURCEA(rid),
                                AUDIO_SE_RESOURCE_TYPE);
        if (!r) continue;                         /* absent slot (e.g. 0x0135) */
        HGLOBAL g = LoadResource(h, r);
        const void *p = g ? LockResource(g) : NULL;
        DWORD sz = SizeofResource(h, r);
        if (!p || !sz) continue;
        blobs[slot].data = (const uint8_t *)p;    /* valid until FreeLibrary */
        blobs[slot].size = sz;
        found++;
    }

    uint8_t *image = se_pack_serialize(blobs, AUDIO_SE_COUNT, sha, out_len);
    FreeLibrary(h);

    if (!image) {
        fprintf(stderr, "se_pack: serialize failed (out of memory)\n");
        return NULL;
    }
    fprintf(stderr, "se_pack: extracted %d/%d SE from %s\n",
            found, AUDIO_SE_COUNT, exe_path);
    return image;
}

static int write_cache(const char *path, const uint8_t *image, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "se_pack: cannot write cache %s\n", path);
        return 1;
    }
    size_t put = fwrite(image, 1, len, f);
    int err = (put != len) || fclose(f);
    if (err) {
        fprintf(stderr, "se_pack: short write to cache %s\n", path);
        return 1;
    }
    return 0;
}

int se_pack_acquire(void)
{
    if (g_acquired) return 0;

    char exe[MAX_PATH];
    if (!locate_retail_exe(exe, sizeof exe)) {
        fprintf(stderr, "se_pack: could not determine retail exe path\n");
        return 1;
    }

    uint8_t retail_sha[SHA256_DIGEST_LEN];
    if (hash_file(exe, retail_sha) != 0) {
        fprintf(stderr,
                "se_pack: cannot read retail exe '%s' — SE will be silent. "
                "Set OPENRECET_RETAIL_EXE or run from your Recettear dir.\n",
                exe);
        return 1;
    }

    char cache[MAX_PATH];
    int have_cache_path = cache_path(cache, sizeof cache);

    /* Try the cache first: load + parse + hash-match. */
    if (have_cache_path) {
        size_t clen = 0;
        uint8_t *cimg = read_whole_file(cache, &clen);
        if (cimg) {
            uint8_t stored_sha[SHA256_DIGEST_LEN];
            if (se_pack_parse(cimg, clen, AUDIO_SE_COUNT, g_blobs,
                              stored_sha) == 0 &&
                memcmp(stored_sha, retail_sha, SHA256_DIGEST_LEN) == 0) {
                g_pack_image = cimg;
                g_pack_len = clen;
                g_acquired = 1;
                fprintf(stderr, "se_pack: loaded cache %s\n", cache);
                return 0;
            }
            free(cimg);   /* stale/corrupt — fall through to re-extract */
        }
    }

    /* Extract fresh from the retail exe. */
    size_t ilen = 0;
    uint8_t *image = extract_pack(exe, retail_sha, &ilen);
    if (!image) return 1;

    if (se_pack_parse(image, ilen, AUDIO_SE_COUNT, g_blobs, NULL) != 0) {
        fprintf(stderr, "se_pack: self-parse of fresh extract failed\n");
        free(image);
        return 1;
    }
    g_pack_image = image;
    g_pack_len = ilen;
    g_acquired = 1;

    if (have_cache_path)
        write_cache(cache, image, ilen);   /* non-fatal if it fails */

    return 0;
}

const se_blob_t *se_pack_blob(int slot)
{
    if (!g_acquired || slot < 0 || slot >= AUDIO_SE_COUNT) return NULL;
    return &g_blobs[slot];
}

void se_pack_release(void)
{
    free(g_pack_image);
    g_pack_image = NULL;
    g_pack_len = 0;
    g_acquired = 0;
    memset(g_blobs, 0, sizeof g_blobs);
}

#endif /* _WIN32 */
