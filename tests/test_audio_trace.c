/*
 * test_audio_trace.c — JSONL audio-trace serializer.
 *
 * Covers:
 *   - audio_trace_json_escape() — quote, backslash, newlines, NUL handling,
 *     non-ASCII bytes → \uXXXX, cap truncation.
 *   - audio_trace_open / audio_trace_close — round-trip a real file.
 *   - audio_trace_emit_bgm_swap — emits a well-formed JSON line.
 */
#define _GNU_SOURCE
#include "t.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Pull a single line out of a file via mkstemp-style temp path. The
 * tests run under the host Linux toolchain; mkstemp + unlink work. */
static const char *temp_trace_path(char *buf, size_t cap)
{
    snprintf(buf, cap, "/tmp/openrecet_audio_trace_XXXXXX");
    int fd = mkstemp(buf);
    if (fd < 0) return NULL;
    close(fd);   /* audio_trace_open will fopen this path itself. */
    return buf;
}

int test_audio_trace_json_escape_passthrough_ascii(void)
{
    char dst[64];
    size_t n = audio_trace_json_escape("bgm/town.wav", dst, sizeof dst);
    T_ASSERT_EQ_U(n, 12);
    T_ASSERT(strcmp(dst, "bgm/town.wav") == 0);
    return 0;
}

int test_audio_trace_json_escape_quote(void)
{
    char dst[64];
    size_t n = audio_trace_json_escape("she said \"hi\"", dst, sizeof dst);
    T_ASSERT(strcmp(dst, "she said \\\"hi\\\"") == 0);
    T_ASSERT_EQ_U(n, strlen(dst));
    return 0;
}

int test_audio_trace_json_escape_backslash(void)
{
    char dst[64];
    audio_trace_json_escape("path\\to\\file", dst, sizeof dst);
    T_ASSERT(strcmp(dst, "path\\\\to\\\\file") == 0);
    return 0;
}

int test_audio_trace_json_escape_newlines_and_tabs(void)
{
    char dst[64];
    audio_trace_json_escape("a\nb\rc\td", dst, sizeof dst);
    T_ASSERT(strcmp(dst, "a\\nb\\rc\\td") == 0);
    return 0;
}

int test_audio_trace_json_escape_non_ascii_to_u_form(void)
{
    /* High-bit byte → \u00xx. Useful for SJIS filenames; vendor BGM
     * names happen to all be ASCII but the SE table may include JP
     * names once we get to task #5. */
    const char src[] = { (char)0xe3, (char)0x82, 'x', 0 };
    char dst[64];
    audio_trace_json_escape(src, dst, sizeof dst);
    T_ASSERT(strcmp(dst, "\\u00e3\\u0082x") == 0);
    return 0;
}

int test_audio_trace_json_escape_truncates_safely(void)
{
    /* cap=5 → at most 4 escaped chars before NUL. A long source with
     * tons of quotes (\") would overflow if unprotected. */
    char dst[5];
    audio_trace_json_escape("\"\"\"\"\"\"", dst, sizeof dst);
    /* Each " becomes \", so 2 escapes = 4 chars + NUL = 5. */
    T_ASSERT(strcmp(dst, "\\\"\\\"") == 0);
    /* And dst[4] is the NUL. */
    T_ASSERT(dst[4] == 0);
    return 0;
}

int test_audio_trace_json_escape_null_safe(void)
{
    char dst[8];
    size_t n = audio_trace_json_escape(NULL, dst, sizeof dst);
    T_ASSERT_EQ_U(n, 0);
    T_ASSERT(dst[0] == 0);
    return 0;
}

int test_audio_trace_open_close_idempotent(void)
{
    T_ASSERT(!audio_trace_is_open());
    char path[80];
    if (!temp_trace_path(path, sizeof path)) T_SKIP("mkstemp failed");

    T_ASSERT(audio_trace_open(path) == 1);
    T_ASSERT(audio_trace_is_open());

    /* Reopen — old FILE* should close cleanly, new one takes over. */
    char path2[80];
    if (!temp_trace_path(path2, sizeof path2)) {
        audio_trace_close();
        unlink(path);
        T_SKIP("mkstemp failed");
    }
    T_ASSERT(audio_trace_open(path2) == 1);
    T_ASSERT(audio_trace_is_open());

    audio_trace_close();
    T_ASSERT(!audio_trace_is_open());

    /* Closing twice is a no-op. */
    audio_trace_close();

    unlink(path);
    unlink(path2);
    return 0;
}

int test_audio_trace_open_rejects_null(void)
{
    T_ASSERT(audio_trace_open(NULL) == 0);
    T_ASSERT(!audio_trace_is_open());
    return 0;
}

int test_audio_trace_emit_bgm_swap_writes_one_line(void)
{
    char path[80];
    if (!temp_trace_path(path, sizeof path)) T_SKIP("mkstemp failed");

    T_ASSERT(audio_trace_open(path) == 1);
    audio_trace_emit_bgm_swap(0, "bgm/retitle2010.wav");
    audio_trace_close();

    FILE *fp = fopen(path, "r");
    if (!fp) { unlink(path); T_FAIL("could not reopen %s", path); }
    char line[512];
    char *got = fgets(line, sizeof line, fp);
    fclose(fp);
    unlink(path);
    T_ASSERT(got != NULL);

    /* On Linux test build, t_ms is 0; on Win32 it's tiny but non-zero.
     * Check the stable parts of the line. */
    T_ASSERT(strstr(line, "\"kind\":\"bgm_swap\"") != NULL);
    T_ASSERT(strstr(line, "\"track\":0") != NULL);
    T_ASSERT(strstr(line, "\"name\":\"bgm/retitle2010.wav\"") != NULL);
    T_ASSERT(strstr(line, "\"t_ms\":") != NULL);
    /* Must end with a newline so the file is line-parseable. */
    size_t len = strlen(line);
    T_ASSERT(len > 0 && line[len - 1] == '\n');
    return 0;
}

int test_audio_trace_emit_when_closed_is_noop(void)
{
    T_ASSERT(!audio_trace_is_open());
    /* Must not crash, must not segfault. */
    audio_trace_emit_bgm_swap(7, "bgm/over.wav");
    return 0;
}
