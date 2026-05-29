/*
 * test_se_pack.c — sha256 vectors + se.pack serialize/parse round-trip.
 *
 * Covers the platform-independent format layer of src/se_pack.c (the
 * Win32 extraction/cache orchestration is #ifdef _WIN32 and not exercised
 * here). See docs/formats/se-pack.md.
 */
#include "t.h"
#include "sha256.h"
#include "se_pack.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int hexeq(const uint8_t *d, const char *hex)
{
    for (int i = 0; i < SHA256_DIGEST_LEN; i++) {
        char b[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        if ((uint8_t)strtol(b, NULL, 16) != d[i]) return 0;
    }
    return 1;
}

int test_sha256_empty_vector(void)
{
    uint8_t d[SHA256_DIGEST_LEN];
    sha256("", 0, d);
    T_ASSERT(hexeq(d,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    return 0;
}

int test_sha256_abc_vector(void)
{
    uint8_t d[SHA256_DIGEST_LEN];
    sha256("abc", 3, d);
    T_ASSERT(hexeq(d,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    return 0;
}

int test_sha256_two_block_vector(void)
{
    /* 56 bytes — crosses the pad-to-next-block boundary. */
    const char *msg =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t d[SHA256_DIGEST_LEN];
    sha256(msg, strlen(msg), d);
    T_ASSERT(hexeq(d,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    return 0;
}

int test_sha256_streaming_matches_oneshot(void)
{
    uint8_t big[5000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 31 + 7);

    uint8_t one[SHA256_DIGEST_LEN], stream[SHA256_DIGEST_LEN];
    sha256(big, sizeof big, one);

    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, big, 1);
    sha256_update(&c, big + 1, 63);
    sha256_update(&c, big + 64, 100);
    sha256_update(&c, big + 164, sizeof big - 164);
    sha256_final(&c, stream);

    T_ASSERT(memcmp(one, stream, SHA256_DIGEST_LEN) == 0);
    return 0;
}

/* Build a small pack of `n` slots and round-trip it. Slot 1 is left
 * empty to exercise the absent-slot path. */
int test_se_pack_roundtrip(void)
{
    enum { N = 4 };
    uint8_t a[] = { 'R', 'I', 'F', 'F', 1, 2, 3 };
    uint8_t c[] = { 9, 8, 7, 6, 5 };
    uint8_t d[] = { 42 };

    se_blob_t in[N] = {
        { a, sizeof a },
        { NULL, 0 },          /* absent */
        { c, sizeof c },
        { d, sizeof d },
    };
    uint8_t sha[SHA256_DIGEST_LEN];
    for (int i = 0; i < SHA256_DIGEST_LEN; i++) sha[i] = (uint8_t)(i + 1);

    size_t len = 0;
    uint8_t *img = se_pack_serialize(in, N, sha, &len);
    T_ASSERT(img != NULL);
    /* header + table + blob bytes (slot 1 contributes none) */
    T_ASSERT_EQ_U(len, (size_t)SE_PACK_HEADER_LEN + N * 8 +
                       sizeof a + sizeof c + sizeof d);

    se_blob_t out[N];
    uint8_t got_sha[SHA256_DIGEST_LEN];
    int rc = se_pack_parse(img, len, N, out, got_sha);
    T_ASSERT_EQ_I(rc, 0);
    T_ASSERT(memcmp(got_sha, sha, SHA256_DIGEST_LEN) == 0);

    T_ASSERT_EQ_U(out[0].size, sizeof a);
    T_ASSERT(memcmp(out[0].data, a, sizeof a) == 0);
    T_ASSERT(out[1].data == NULL);
    T_ASSERT_EQ_U(out[1].size, 0);
    T_ASSERT_EQ_U(out[2].size, sizeof c);
    T_ASSERT(memcmp(out[2].data, c, sizeof c) == 0);
    T_ASSERT_EQ_U(out[3].size, sizeof d);
    T_ASSERT(memcmp(out[3].data, d, sizeof d) == 0);

    free(img);
    return 0;
}

int test_se_pack_rejects_bad_magic(void)
{
    enum { N = 2 };
    uint8_t x[] = { 1, 2 };
    se_blob_t in[N] = { { x, sizeof x }, { NULL, 0 } };
    uint8_t sha[SHA256_DIGEST_LEN] = { 0 };
    size_t len = 0;
    uint8_t *img = se_pack_serialize(in, N, sha, &len);
    T_ASSERT(img != NULL);

    img[0] ^= 0xff;   /* corrupt magic */
    se_blob_t out[N];
    T_ASSERT(se_pack_parse(img, len, N, out, NULL) != 0);
    free(img);
    return 0;
}

int test_se_pack_rejects_count_mismatch(void)
{
    enum { N = 3 };
    uint8_t x[] = { 7 };
    se_blob_t in[N] = { { x, 1 }, { x, 1 }, { x, 1 } };
    uint8_t sha[SHA256_DIGEST_LEN] = { 0 };
    size_t len = 0;
    uint8_t *img = se_pack_serialize(in, N, sha, &len);
    T_ASSERT(img != NULL);

    se_blob_t out[N];
    /* caller expects a different slot count → reject */
    T_ASSERT(se_pack_parse(img, len, N + 1, out, NULL) != 0);
    free(img);
    return 0;
}

int test_se_pack_rejects_truncation(void)
{
    enum { N = 2 };
    uint8_t x[] = { 1, 2, 3, 4 };
    se_blob_t in[N] = { { x, sizeof x }, { x, sizeof x } };
    uint8_t sha[SHA256_DIGEST_LEN] = { 0 };
    size_t len = 0;
    uint8_t *img = se_pack_serialize(in, N, sha, &len);
    T_ASSERT(img != NULL);

    se_blob_t out[N];
    /* chop the last blob byte — the final entry now overruns */
    T_ASSERT(se_pack_parse(img, len - 1, N, out, NULL) != 0);
    /* and a length below the header is rejected too */
    T_ASSERT(se_pack_parse(img, 10, N, out, NULL) != 0);
    free(img);
    return 0;
}
