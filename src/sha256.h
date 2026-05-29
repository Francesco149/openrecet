/*
 * sha256.h — minimal SHA-256 (FIPS 180-4).
 *
 * Self-contained, pure C, no platform dependency, so it compiles and
 * runs under the host test sanitizer as well as in the Win32 build.
 * Used to key the runtime SE cache (`se.pack`) on the retail exe's hash
 * — see src/se_pack.c and docs/formats/se-pack.md.
 */
#ifndef OPENRECET_SHA256_H
#define OPENRECET_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_LEN 32

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, uint8_t out[SHA256_DIGEST_LEN]);

/* One-shot convenience: hash `len` bytes of `data` into `out`. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

#endif /* OPENRECET_SHA256_H */
