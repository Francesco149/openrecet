/*
 * scene1_dialogue_load.c — engine-side loader for the dialogue interpreter
 * (the FUN_0046c295 loader core). Split from scene1_dialogue.c because it
 * pulls in the Win32 storage layer (src/storage.h), which is not host-
 * compilable; the parser itself stays pure + host-tested.
 *
 * Engine reference (FUN_0046ddea head, all.c:49-65): build "iv/iv%d_%d.ivt"
 * from the scene/sub selector (DAT_005c7a2c/30), try a loose disk file first
 * (FUN_005038b0), else read from the lnkdatas pack (FUN_004346bf) into a
 * 64 KiB scratch buffer, NUL-terminate, and parse.
 */
#include "scene1_dialogue.h"

#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int scene1_dialogue_load(int scene, int sub, struct ive_program *prog)
{
    char path[64];
    snprintf(path, sizeof path, "iv/iv%d_%d.ivt", scene, sub);

    size_t sz = storage_get_size(path);
    if (sz == 0) {
        memset(prog, 0, sizeof *prog);
        return 0;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { memset(prog, 0, sizeof *prog); return 0; }

    size_t got = storage_read(path, buf);
    buf[got] = '\0';

    int ok = scene1_dialogue_parse(buf, prog);
    free(buf);
    return ok;
}
