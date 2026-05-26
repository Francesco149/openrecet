/*
 * audio_mci.c — see audio_mci.h.
 *
 * Faithful port of FUN_00451874 + FUN_00451863. Both fire only when
 * the engine's debug flag DAT_0438ccb4 is non-zero (always zero in
 * shipped data) — so this module is dormant in normal play and only
 * comes alive when the debug-overlay render path is wired in.
 */

#include "audio_mci.h"
#include "call_trace.h"

char g_audio_mci_buffer[AUDIO_MCI_BUFFER_SIZE];

void audio_mci_record_command(int channel, int row_index, const char *cmd)
{
    /* E.2 probe — FUN_00451874 @ 0x451874. */
    CALL_TRACE_ENTER(0x451874u);

    /* Engine body verbatim:
     *
     *   iVar2 = 0;
     *   do {
     *     pcVar1 = cmd + iVar2;
     *     if (*pcVar1 == '\0') return;
     *     iVar3 = row_index * 0x50 + iVar2;
     *     iVar2 += 1;
     *     buffer[channel + iVar3] = *pcVar1;
     *   } while (iVar2 != 0x50);
     *
     * The increment-then-write order means the buffer index for byte
     * `i` is channel + row_index*0x50 + i, and we exit either on
     * src[i]==NUL or after writing exactly 0x50 bytes.
     */
    int i = 0;
    do {
        const char *p = cmd + i;
        if (*p == '\0') {
            return;
        }
        int row_base = row_index * AUDIO_MCI_ROW_BYTES + i;
        i += 1;
        g_audio_mci_buffer[channel + row_base] = *p;
    } while (i != AUDIO_MCI_ROW_BYTES);
}

void audio_mci_clear(void)
{
    /* FUN_00451863: dword-zero loop, 0x4b0 dwords = 4800 bytes. */
    for (size_t i = 0; i < AUDIO_MCI_BUFFER_SIZE; i++) {
        g_audio_mci_buffer[i] = 0;
    }
}
