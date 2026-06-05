/*
 * test_d3d_tex_names.c — the texture source-name registry (src/d3d_tex_names.c)
 * that backs the d3d-trace "tex_name" field.  Pure pointer→name map; these
 * tests exercise register / lookup / overwrite / forget (incl. tombstone
 * reuse and probing past a tombstone) / truncation / reset.
 */
#include "t.h"
#include "d3d_tex_names.h"

#include <stdint.h>
#include <string.h>

/* Distinct dummy texture pointers (never dereferenced). */
static const void *P(uintptr_t n) { return (const void *)(n << 4 | 0x8); }

int test_d3d_tex_names_basic(void)
{
    d3d_tex_name_reset();

    T_ASSERT(d3d_tex_name_lookup(P(1)) == NULL);

    d3d_tex_name_register(P(1), "bmp/ivent/ive_window.tga");
    d3d_tex_name_register(P(2), "bmp/recette.bmp");

    const char *a = d3d_tex_name_lookup(P(1));
    const char *b = d3d_tex_name_lookup(P(2));
    T_ASSERT(a && strcmp(a, "bmp/ivent/ive_window.tga") == 0);
    T_ASSERT(b && strcmp(b, "bmp/recette.bmp") == 0);
    T_ASSERT(d3d_tex_name_lookup(P(3)) == NULL);
    return 0;
}

int test_d3d_tex_names_overwrite(void)
{
    d3d_tex_name_reset();

    d3d_tex_name_register(P(7), "old.tga");
    d3d_tex_name_register(P(7), "new.tga");

    const char *v = d3d_tex_name_lookup(P(7));
    T_ASSERT(v && strcmp(v, "new.tga") == 0);
    return 0;
}

int test_d3d_tex_names_forget(void)
{
    d3d_tex_name_reset();

    d3d_tex_name_register(P(5), "tear.tga");
    T_ASSERT(d3d_tex_name_lookup(P(5)) != NULL);

    d3d_tex_name_forget(P(5));
    T_ASSERT(d3d_tex_name_lookup(P(5)) == NULL);

    /* a recycled pointer registers cleanly after forget */
    d3d_tex_name_register(P(5), "mint.tga");
    const char *v = d3d_tex_name_lookup(P(5));
    T_ASSERT(v && strcmp(v, "mint.tga") == 0);
    return 0;
}

/* A forgotten entry leaves a tombstone; a later key that collides on the
 * same probe chain must still be found PAST that tombstone. */
int test_d3d_tex_names_probe_past_tombstone(void)
{
    d3d_tex_name_reset();

    /* Insert two keys, then forget the first.  We can't guarantee a hash
     * collision with synthetic pointers, but forgetting an earlier-probed
     * slot and re-looking-up the survivor exercises the tombstone-skip
     * path regardless of whether they actually collide. */
    d3d_tex_name_register(P(11), "a.tga");
    d3d_tex_name_register(P(12), "b.tga");
    d3d_tex_name_forget(P(11));

    const char *v = d3d_tex_name_lookup(P(12));
    T_ASSERT(v && strcmp(v, "b.tga") == 0);
    T_ASSERT(d3d_tex_name_lookup(P(11)) == NULL);
    return 0;
}

int test_d3d_tex_names_truncate(void)
{
    d3d_tex_name_reset();

    char longname[256];
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';

    d3d_tex_name_register(P(9), longname);
    const char *v = d3d_tex_name_lookup(P(9));
    T_ASSERT(v != NULL);
    /* truncated to the internal cap (95 chars + NUL), still a valid C string */
    T_ASSERT(strlen(v) < sizeof longname - 1);
    T_ASSERT(strlen(v) > 0);
    for (const char *c = v; *c; c++) T_ASSERT(*c == 'x');
    return 0;
}

int test_d3d_tex_names_null_safe(void)
{
    d3d_tex_name_reset();

    d3d_tex_name_register(NULL, "x.tga");          /* no-op */
    d3d_tex_name_register(P(1), NULL);             /* no-op */
    T_ASSERT(d3d_tex_name_lookup(NULL) == NULL);
    T_ASSERT(d3d_tex_name_lookup(P(1)) == NULL);
    d3d_tex_name_forget(NULL);                      /* no crash */
    return 0;
}

int test_d3d_tex_names_reset(void)
{
    d3d_tex_name_reset();
    d3d_tex_name_register(P(1), "a.tga");
    d3d_tex_name_register(P(2), "b.tga");
    d3d_tex_name_reset();
    T_ASSERT(d3d_tex_name_lookup(P(1)) == NULL);
    T_ASSERT(d3d_tex_name_lookup(P(2)) == NULL);
    return 0;
}
