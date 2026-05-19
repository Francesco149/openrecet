/*
 * Tiny assertion macros for the OpenRecet unit tests.
 *
 * Each test function has signature `int name(void)` and returns:
 *   0 on pass
 *   1 on fail (T_FAIL has already emitted a message)
 *   2 on skip (e.g. vendor data missing)
 *
 * The test driver in test_main.c iterates over a list of these
 * functions, runs them, and prints a summary.
 */
#ifndef OPENRECET_TESTS_T_H
#define OPENRECET_TESTS_T_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OPENRECET_ROOT
#define OPENRECET_ROOT "."
#endif

#define T_FAIL(...) do { \
    fprintf(stderr, "  fail %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 1; \
} while (0)

#define T_SKIP(...) do { \
    fprintf(stderr, "  skip: "); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 2; \
} while (0)

#define T_ASSERT(cond) do { \
    if (!(cond)) T_FAIL("assertion failed: %s", #cond); \
} while (0)

#define T_ASSERT_EQ_U(a, b) do { \
    unsigned long long _a = (unsigned long long)(a); \
    unsigned long long _b = (unsigned long long)(b); \
    if (_a != _b) T_FAIL("expected %s == %s (got %llu, want %llu)", \
                         #a, #b, _a, _b); \
} while (0)

#define T_ASSERT_EQ_I(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) T_FAIL("expected %s == %s (got %lld, want %lld)", \
                         #a, #b, _a, _b); \
} while (0)

#define T_ASSERT_MEM_EQ(a, b, n) do { \
    if (memcmp((a), (b), (n)) != 0) \
        T_FAIL("memory differs over %zu bytes", (size_t)(n)); \
} while (0)

/* Convenience byte writers used by tests that assemble raw image
 * formats. Unaligned-safe (works under UBSan). */
static inline void t_wr16_le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
}
static inline void t_wr32_le(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

#endif /* OPENRECET_TESTS_T_H */
