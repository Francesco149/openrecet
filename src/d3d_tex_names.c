/* Stable texture source-name registry — see d3d_tex_names.h. */

#include "d3d_tex_names.h"

#include <stdint.h>
#include <string.h>

/* Open-addressing (linear-probe) hash table.  Sized generously: the engine
 * keeps on the order of a few hundred live textures (UI atlases, chr sheets,
 * mesh-cache slots), so a 2048-slot table stays well under half full and
 * probe chains stay short.  Power-of-two for the mask. */
#define TEX_NAME_SLOTS 2048u
#define TEX_NAME_MAX   96   /* longest asset path is ~24 chars; ample */

/* TOMBSTONE marks a forgotten slot: lookups/inserts must probe PAST it
 * (an empty NULL slot terminates a probe chain; a tombstone does not). */
#define TEX_TOMBSTONE ((const void *)(uintptr_t)1)

struct tex_name_entry {
    const void *key;            /* NULL = empty, TEX_TOMBSTONE = deleted */
    char        name[TEX_NAME_MAX];
};

static struct tex_name_entry g_tab[TEX_NAME_SLOTS];

static unsigned tex_hash(const void *p)
{
    /* Fibonacci-style mix of the pointer bits — pointers from the same
     * allocator cluster otherwise collide on the low bits. */
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 16;
    x *= 0x9E3779B1u;
    x ^= x >> 13;
    return (unsigned)(x & (TEX_NAME_SLOTS - 1u));
}

void d3d_tex_name_register(const void *tex, const char *name)
{
    if (!tex || tex == TEX_TOMBSTONE || !name) return;

    unsigned h = tex_hash(tex);
    int ins = -1;                       /* first reusable slot in the chain */
    for (unsigned i = 0; i < TEX_NAME_SLOTS; i++) {
        unsigned s = (h + i) & (TEX_NAME_SLOTS - 1u);
        const void *k = g_tab[s].key;
        if (k == tex) { ins = (int)s; break; }            /* overwrite */
        if (k == TEX_TOMBSTONE) { if (ins < 0) ins = (int)s; continue; }
        if (k == NULL) { if (ins < 0) ins = (int)s; break; } /* chain end */
    }
    if (ins < 0) return;                /* table full — drop (best-effort) */

    g_tab[ins].key = tex;
    size_t n = strlen(name);
    if (n >= TEX_NAME_MAX) n = TEX_NAME_MAX - 1;
    memcpy(g_tab[ins].name, name, n);
    g_tab[ins].name[n] = '\0';
}

const char *d3d_tex_name_lookup(const void *tex)
{
    if (!tex || tex == TEX_TOMBSTONE) return NULL;

    unsigned h = tex_hash(tex);
    for (unsigned i = 0; i < TEX_NAME_SLOTS; i++) {
        unsigned s = (h + i) & (TEX_NAME_SLOTS - 1u);
        const void *k = g_tab[s].key;
        if (k == tex) return g_tab[s].name;
        if (k == NULL) return NULL;     /* chain end — not present */
    }
    return NULL;
}

void d3d_tex_name_forget(const void *tex)
{
    if (!tex || tex == TEX_TOMBSTONE) return;

    unsigned h = tex_hash(tex);
    for (unsigned i = 0; i < TEX_NAME_SLOTS; i++) {
        unsigned s = (h + i) & (TEX_NAME_SLOTS - 1u);
        const void *k = g_tab[s].key;
        if (k == tex) { g_tab[s].key = TEX_TOMBSTONE; g_tab[s].name[0] = '\0'; return; }
        if (k == NULL) return;
    }
}

void d3d_tex_name_reset(void)
{
    memset(g_tab, 0, sizeof g_tab);
}
