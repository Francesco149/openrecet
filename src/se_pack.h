/*
 * se_pack.h — runtime SE cache: extract sound-effect WAVs from the
 * user's own retail recettear.exe and cache them, instead of embedding
 * proprietary audio in our binary.
 *
 * Format + rationale: docs/formats/se-pack.md.
 *
 * Two layers:
 *   - pure format (serialize/parse) — host-testable, no Win32.
 *   - Win32 orchestration (acquire/blob/release) — locate the retail
 *     exe, hash it, load-or-extract %LOCALAPPDATA%\openrecet\se.pack.
 */
#ifndef OPENRECET_SE_PACK_H
#define OPENRECET_SE_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

#define SE_PACK_MAGIC      "OPRSEPK1"   /* 8 bytes, no NUL */
#define SE_PACK_MAGIC_LEN  8
#define SE_PACK_VERSION    1u
#define SE_PACK_HEADER_LEN 48           /* magic(8)+ver(4)+count(4)+sha(32) */

/* One SE blob. data may be NULL iff size == 0 (slot absent, e.g. the
 * engine's slot 2 / id 0x0135 which has no WAVE resource). */
typedef struct {
    const uint8_t *data;
    uint32_t       size;
} se_blob_t;

/* ─── pure format layer (compiles everywhere; covered by tests) ────── */

/* Build a se.pack image from `count` blobs and the 32-byte retail-exe
 * hash. Returns a freshly malloc'd buffer (caller frees) and writes its
 * length to *out_len, or NULL on allocation failure. */
uint8_t *se_pack_serialize(const se_blob_t *blobs, uint32_t count,
                           const uint8_t sha[SHA256_DIGEST_LEN],
                           size_t *out_len);

/* Validate the pack image in data[0..len). On success returns 0 and:
 *   - fills out_blobs[0..expect_count) with pointers INTO `data`
 *     (so `data` must outlive any use of out_blobs),
 *   - if sha_out != NULL, copies the stored 32-byte retail hash.
 * Returns nonzero (and leaves outputs untouched) on bad magic, version,
 * count != expect_count, truncation, or an out-of-range blob extent. */
int se_pack_parse(const uint8_t *data, size_t len, uint32_t expect_count,
                  se_blob_t *out_blobs, uint8_t sha_out[SHA256_DIGEST_LEN]);

/* ─── Win32 runtime orchestration ──────────────────────────────────── */
#ifdef _WIN32
/* Make the SE cache available in memory. Locates the retail exe
 * (OPENRECET_RETAIL_EXE env, else ./recettear.exe), hashes it, and
 * either loads a matching %LOCALAPPDATA%\openrecet\se.pack or extracts
 * the 110 WAVE resources from the exe and writes the cache.
 *
 * Returns 0 on success (se_pack_blob valid afterwards), nonzero if the
 * retail exe can't be found/read (audio init should then skip SE, as it
 * does today for missing resources). Idempotent: a second call is a
 * no-op once acquired. */
int se_pack_acquire(void);

/* Blob for `slot` in [0, AUDIO_SE_COUNT), or NULL if not acquired or out
 * of range. The returned se_blob_t (and its ->data) stay valid until
 * se_pack_release(). A present-but-empty slot has size == 0. */
const se_blob_t *se_pack_blob(int slot);

/* Free the loaded pack image. */
void se_pack_release(void);
#endif /* _WIN32 */

#endif /* OPENRECET_SE_PACK_H */
