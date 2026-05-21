/*
 * test_audio_se_table.c — pin the SE resource ID table.
 *
 * The table at &DAT_005d1584 in the unpacked engine is extracted into
 * src/audio_se_names.c via tools/extract/se-wavs.py. These tests
 * catch any drift between the C copy and the engine's truth.
 */
#define _GNU_SOURCE
#include "t.h"
#include "audio.h"
#include "audio_se_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int test_audio_se_table_has_110_entries(void)
{
    /* Source: 880 bytes / 8-byte stride at &DAT_005d1584. */
    T_ASSERT_EQ_I(AUDIO_SE_COUNT, 110);
    return 0;
}

int test_audio_se_table_first_and_last_ids(void)
{
    /* Bracket the table — these are the IDs the engine starts and
     * ends with. If either drifts, the extractor or the engine
     * binary changed. */
    T_ASSERT_EQ_I(audio_se_resource_ids[0],   0x013d);
    T_ASSERT_EQ_I(audio_se_resource_ids[1],   0x013e);
    /* Slot 2 is 0x0135 — out-of-order, and the ID is missing from
     * .rsrc in vendor data (see engine-quirks.md). */
    T_ASSERT_EQ_I(audio_se_resource_ids[2],   0x0135);
    T_ASSERT_EQ_I(audio_se_resource_ids[109], 0x02c6);
    return 0;
}

int test_audio_se_table_out_of_order_swap_at_39_40(void)
{
    /* Engine quirk: 0x0166 and 0x0165 are swapped at slots 39/40
     * (everything else in the 0x13d..0x182 range is monotone). */
    T_ASSERT_EQ_I(audio_se_resource_ids[38], 0x0163);
    T_ASSERT_EQ_I(audio_se_resource_ids[39], 0x0166);
    T_ASSERT_EQ_I(audio_se_resource_ids[40], 0x0165);
    T_ASSERT_EQ_I(audio_se_resource_ids[41], 0x0167);
    return 0;
}

int test_audio_se_table_jumps_to_high_range_at_slot_69(void)
{
    /* The first range stops at 0x0182 (slot 68); the second starts
     * at 0x029d (slot 69). */
    T_ASSERT_EQ_I(audio_se_resource_ids[68], 0x0182);
    T_ASSERT_EQ_I(audio_se_resource_ids[69], 0x029d);
    return 0;
}

int test_audio_se_table_skips_2c3(void)
{
    /* Near the end: 0x02c2, 0x02c4 — note the missing 0x02c3. */
    int found_c3 = 0;
    for (int i = 0; i < AUDIO_SE_COUNT; i++) {
        if (audio_se_resource_ids[i] == 0x02c3) found_c3 = 1;
    }
    T_ASSERT_EQ_I(found_c3, 0);
    return 0;
}

int test_audio_se_resource_id_bounds(void)
{
    T_ASSERT_EQ_I(audio_se_resource_id(-1), 0);
    T_ASSERT_EQ_I(audio_se_resource_id(AUDIO_SE_COUNT), 0);
    T_ASSERT_EQ_I(audio_se_resource_id(0), audio_se_resource_ids[0]);
    T_ASSERT_EQ_I(audio_se_resource_id(109), audio_se_resource_ids[109]);
    return 0;
}

int test_audio_se_table_resource_type_is_custom(void)
{
    /* The engine FindResourceA's against a *named* (custom) resource
     * type "WAVE", not the standard RT_WAVE (25). Pin the literal so
     * later refactors don't accidentally swap to RT_RCDATA or
     * RT_WAVE. */
    T_ASSERT(strcmp(AUDIO_SE_RESOURCE_TYPE, "WAVE") == 0);
    return 0;
}

/* ─── audio_play_se trace integration ───────────────────────────────── */

static int temp_path(char *buf, size_t cap)
{
    snprintf(buf, cap, "/tmp/openrecet_audio_se_XXXXXX");
    int fd = mkstemp(buf);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

int test_audio_play_se_rejects_out_of_range(void)
{
    T_ASSERT_EQ_I(audio_play_se(-1), 0);
    T_ASSERT_EQ_I(audio_play_se(AUDIO_SE_COUNT), 0);
    T_ASSERT_EQ_I(audio_play_se(1000), 0);
    return 0;
}

int test_audio_play_se_emits_trace_event(void)
{
    char path[80];
    if (!temp_path(path, sizeof path)) T_SKIP("mkstemp failed");

    T_ASSERT(audio_trace_open(path) == 1);
    /* Slot 12 = ID 0x0148 per the table. */
    T_ASSERT_EQ_I(audio_play_se(12), 1);
    audio_trace_close();

    FILE *fp = fopen(path, "r");
    if (!fp) { unlink(path); T_FAIL("could not reopen %s", path); }
    char line[512];
    char *got = fgets(line, sizeof line, fp);
    fclose(fp);
    unlink(path);
    T_ASSERT(got != NULL);

    T_ASSERT(strstr(line, "\"kind\":\"se_play\"") != NULL);
    T_ASSERT(strstr(line, "\"slot\":12") != NULL);
    T_ASSERT(strstr(line, "\"name\":\"se_012_id0148\"") != NULL);
    return 0;
}

int test_audio_play_se_no_trace_when_closed(void)
{
    /* No trace → still returns 1, just doesn't write anywhere. */
    T_ASSERT(!audio_trace_is_open());
    T_ASSERT_EQ_I(audio_play_se(0), 1);
    return 0;
}

/* ─── vendor cross-check ─────────────────────────────────────────────
 *
 * Re-reads the engine's ID table directly out of the unpacked exe
 * and matches it byte-for-byte against the C copy. Skips if the
 * vendor file isn't present. This is the test the autonomous-session
 * brief calls "verifies the name table + resource ID list against
 * extracted truth".
 */

#define SE_ID_TABLE_OFFSET   0x001cfd84  /* file offset for VA 0x005d1584 (pe.py va2off). */
#define SE_ID_TABLE_BYTES    (8 * AUDIO_SE_COUNT)

int test_audio_se_table_matches_vendor_bytes(void)
{
    /* OPENRECET_ROOT is injected by tests/Makefile. */
    char path[1024];
    snprintf(path, sizeof path,
             "%s/vendor/unpacked/recettear.unpacked.exe", OPENRECET_ROOT);
    FILE *fp = fopen(path, "rb");
    if (!fp) T_SKIP("vendor/unpacked/ not present");

    if (fseek(fp, SE_ID_TABLE_OFFSET, SEEK_SET) != 0) {
        fclose(fp);
        T_FAIL("seek to SE table failed");
    }
    unsigned char buf[SE_ID_TABLE_BYTES];
    size_t got = fread(buf, 1, sizeof buf, fp);
    fclose(fp);
    if (got != sizeof buf) {
        T_FAIL("short read: got %zu want %zu", got, (size_t)sizeof buf);
    }

    for (int i = 0; i < AUDIO_SE_COUNT; i++) {
        unsigned id_lo = buf[i * 8 + 0];
        unsigned id_hi = buf[i * 8 + 1];
        unsigned pad0  = buf[i * 8 + 2];
        unsigned pad1  = buf[i * 8 + 3];
        uint16_t id    = (uint16_t)(id_lo | (id_hi << 8));
        if (pad0 != 0 || pad1 != 0) {
            T_FAIL("slot %d: high bytes nonzero (0x%02x 0x%02x)",
                   i, pad0, pad1);
        }
        if (audio_se_resource_ids[i] != id) {
            T_FAIL("slot %d drift: header has 0x%04x, vendor has 0x%04x",
                   i, audio_se_resource_ids[i], id);
        }
    }
    return 0;
}
