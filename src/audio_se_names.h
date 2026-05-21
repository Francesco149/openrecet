/*
 * audio_se_names.h — 110-entry SE (sound-effect) resource ID table.
 *
 * Extracted byte-for-byte from .data at &DAT_005d1584..&DAT_005d18f4
 * in the unpacked engine binary (vendor/unpacked/recettear.unpacked.exe)
 * via tools/extract/se-wavs.py. The table is *not* the standard
 * RT_WAVE / RT_RCDATA layout — Recettear ships its SE WAVs as a
 * custom-named resource type "WAVE" (custom string type name, not
 * the WinAPI RT_WAVE / 25 numeric), keyed by the IDs in this list.
 *
 * Entry layout in the engine: each row is 8 bytes (u32 id + u32 zero
 * padding); we collapse to a flat u16 array since the ID always fits.
 *
 * Engine quirk #45 (filed alongside this header): the table is mostly
 * 0x13d..0x182 then a jump to 0x29d..0x2c6 with a few out-of-order
 * IDs in the first range (0x0135 at slot 2, 0x0166 + 0x0165 swapped
 * at slots 39-40, 0x02c3 missing between 0x02c2 and 0x02c4). The
 * engine's SE init loop FindResourceA's each ID; misses are silently
 * skipped. In vendor data, ID 0x0135 (slot 2) is in the table but
 * not present in .rsrc — the engine treats this slot as "no SE
 * loaded" without complaining.
 *
 * The names: we don't have a debug-symbol mapping from slot →
 * mnemonic name (DataIdent etc.). Slots are addressed by index in
 * the engine's call sites (e.g. FUN_00499c63(channel)), so a flat
 * index is the canonical name. Once we cross-reference all the
 * `audio_play_se(N)` call sites with their surrounding code we can
 * promote a few well-known indices to `#define SE_CLICK 12` style
 * mnemonics; for now slot index is the source of truth.
 */
#ifndef OPENRECET_AUDIO_SE_NAMES_H
#define OPENRECET_AUDIO_SE_NAMES_H

#include <stdint.h>

#define AUDIO_SE_COUNT 110

/* PE custom resource type the SE WAVs are stored under. Sits at
 * &DAT_005d1ac8 in the engine. NOT a standard RT_*; we have to pass
 * this as a *named* type to FindResourceA. */
#define AUDIO_SE_RESOURCE_TYPE  "WAVE"

extern const uint16_t audio_se_resource_ids[AUDIO_SE_COUNT];

/* Returns the resource ID for slot in [0, AUDIO_SE_COUNT), or 0 if
 * out of range. (No valid SE has ID 0 — the table starts at 0x13d.) */
uint16_t audio_se_resource_id(int slot);

#endif /* OPENRECET_AUDIO_SE_NAMES_H */
