/*
 * test_input_poll.c — pure-C tests for the input-poll decoders.
 *
 * Covers FUN_0047b73c's three sub-stages without any DirectInput device:
 *   - joystick state → 20-bit pressed array (POV-hat + stick + buttons)
 *   - binding block → 14-bit output mask, with virtual_base offsetting
 *     across 4 joysticks
 *   - keyboard 256-byte state → 14-bit output mask via the DIK table
 *
 * Also covers `input_bindings_load`: the recet.ini struct is laid out
 * as `pad[2][9] + skill[2][5]` (per-class blocks), while the engine
 * keeps pad+skill interleaved per controller — the loader bridges that.
 */
#include "t.h"
#include "input.h"
#include "recet_ini.h"

#include <string.h>

/* ─── input_joystick_decode ─────────────────────────────────────────── */

int test_input_joy_decode_centered_pov_no_axes_no_buttons(void)
{
    uint8_t out[20];
    uint8_t buttons[16] = {0};
    /* POV "centered" = -1 (0xFFFFFFFF); both stick axes at 0 → all zero. */
    input_joystick_decode(-1, 0, 0, buttons, out);
    for (int i = 0; i < 20; i++) T_ASSERT_EQ_I(out[i], 0);
    return 0;
}

int test_input_joy_decode_pov_eight_directions(void)
{
    static const struct { int32_t pov; uint8_t exp[4]; } cases[] = {
        {     0, {1, 0, 0, 0} },   /* N  */
        {  4500, {1, 1, 0, 0} },   /* NE */
        {  9000, {0, 1, 0, 0} },   /* E  */
        { 13500, {0, 1, 1, 0} },   /* SE */
        { 18000, {0, 0, 1, 0} },   /* S  */
        { 22500, {0, 0, 1, 1} },   /* SW */
        { 27000, {0, 0, 0, 1} },   /* W  */
        { 31500, {1, 0, 0, 1} },   /* NW */
    };
    uint8_t buttons[16] = {0};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t out[20];
        input_joystick_decode(cases[i].pov, 0, 0, buttons, out);
        for (int b = 0; b < 4; b++) {
            if (out[b] != cases[i].exp[b]) {
                T_FAIL("pov=%d bit %d: got %u want %u",
                       cases[i].pov, b, out[b], cases[i].exp[b]);
            }
        }
        for (int b = 4; b < 20; b++) T_ASSERT_EQ_I(out[b], 0);
    }
    return 0;
}

int test_input_joy_decode_stick_axes_set_dpad(void)
{
    uint8_t out[20];
    uint8_t buttons[16] = {0};

    /* x > 500 → right */
    input_joystick_decode(-1, 1000, 0, buttons, out);
    T_ASSERT_EQ_I(out[0], 0); T_ASSERT_EQ_I(out[1], 1);
    T_ASSERT_EQ_I(out[2], 0); T_ASSERT_EQ_I(out[3], 0);

    /* y < -500 → up; x < -500 → left */
    input_joystick_decode(-1, -1000, -1000, buttons, out);
    T_ASSERT_EQ_I(out[0], 1); T_ASSERT_EQ_I(out[1], 0);
    T_ASSERT_EQ_I(out[2], 0); T_ASSERT_EQ_I(out[3], 1);

    /* Inside dead-zone: ±500 doesn't trigger. */
    input_joystick_decode(-1, 500, -500, buttons, out);
    for (int b = 0; b < 4; b++) T_ASSERT_EQ_I(out[b], 0);
    return 0;
}

int test_input_joy_decode_pov_axes_or_together(void)
{
    /* POV says N (bit 0). Stick says left (bit 3). Result: bits 0+3. */
    uint8_t out[20];
    uint8_t buttons[16] = {0};
    input_joystick_decode(0, -1000, 0, buttons, out);
    T_ASSERT_EQ_I(out[0], 1);   /* from POV N */
    T_ASSERT_EQ_I(out[1], 0);
    T_ASSERT_EQ_I(out[2], 0);
    T_ASSERT_EQ_I(out[3], 1);   /* from stick left */
    return 0;
}

int test_input_joy_decode_buttons_only_high_bit_matters(void)
{
    uint8_t out[20];
    uint8_t buttons[16] = {
        0x80, 0x7f, 0x81, 0x00, 0xff, 0x40, 0xc0, 0x01,
        0x80, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80,
    };
    input_joystick_decode(-1, 0, 0, buttons, out);
    static const uint8_t exp[16] = {
        1, 0, 1, 0, 1, 0, 1, 0,
        1, 1, 0, 0, 1, 0, 0, 1,
    };
    for (int i = 0; i < 16; i++) T_ASSERT_EQ_I(out[4 + i], exp[i]);
    return 0;
}

/* ─── input_apply_joystick_block ────────────────────────────────────── */

int test_input_apply_joystick_block_default_pad_one(void)
{
    /* Default joystick bindings (pad[1] from recet_ini_pad_defaults). */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        40, 41, 42, 43, 44, 45, 46, 47, 48,    /* pad[1][0..8] */
        0, 0, 0, 0, 0,                         /* skill[1][0..4] */
    };

    /* "Up" pressed on joystick 0 (D-pad bit 0 → button index 0). */
    uint8_t pressed[20] = {0};
    pressed[0] = 1;

    uint16_t out = 0;
    input_apply_joystick_block(bindings, pressed, /*virtual_base=*/0x27, &out);
    /* binding[0]=40 → vcode 39 == 0x27, matches → mask 0x04. */
    T_ASSERT_EQ_U(out, 0x0004u);

    /* "Button 0" pressed → button index 4 in `pressed`, vcode 0x2b
     * == binding[4]=44-1. mask 0x10. */
    memset(pressed, 0, sizeof pressed);
    pressed[4] = 1;
    out = 0;
    input_apply_joystick_block(bindings, pressed, 0x27, &out);
    T_ASSERT_EQ_U(out, 0x0010u);
    return 0;
}

int test_input_apply_joystick_block_virtual_base_offsets_per_joy(void)
{
    /* Same default bindings — only joystick 0 should match these
     * vcodes; joysticks 1..3 occupy 0x3b/0x4f/0x63 base and miss. */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        40, 41, 42, 43, 44, 45, 46, 47, 48, 0, 0, 0, 0, 0,
    };
    uint8_t pressed[20] = {0};
    pressed[0] = 1;        /* D-pad up */

    uint16_t out = 0;
    input_apply_joystick_block(bindings, pressed, 0x3b, &out);   /* joy 1 */
    T_ASSERT_EQ_U(out, 0u);
    input_apply_joystick_block(bindings, pressed, 0x4f, &out);   /* joy 2 */
    T_ASSERT_EQ_U(out, 0u);
    input_apply_joystick_block(bindings, pressed, 0x63, &out);   /* joy 3 */
    T_ASSERT_EQ_U(out, 0u);
    input_apply_joystick_block(bindings, pressed, 0x27, &out);   /* joy 0 */
    T_ASSERT_EQ_U(out, 0x0004u);
    return 0;
}

int test_input_apply_joystick_block_skill_slots_set_byte_plus_one(void)
{
    /* Bind skill0 → vcode 0x2c (joystick 0's button 5), skill4 → 0x30. */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,    /* pad */
        45, 0, 0, 0, 49,              /* skill[0]=45, skill[4]=49 */
    };
    uint8_t pressed[20] = {0};
    pressed[5] = 1;     /* vcode 0x27 + 5 = 0x2c, matches binding[9]=45 → mask 0x0200 */
    pressed[9] = 1;     /* vcode 0x27 + 9 = 0x30, matches binding[13]=49 → mask 0x2000 */

    uint16_t out = 0;
    input_apply_joystick_block(bindings, pressed, 0x27, &out);
    T_ASSERT_EQ_U(out, 0x0200u | 0x2000u);
    return 0;
}

int test_input_apply_joystick_block_zero_binding_never_matches(void)
{
    int16_t bindings[INPUT_BINDING_SLOTS] = {0};
    uint8_t pressed[20];
    memset(pressed, 1, sizeof pressed);   /* all "pressed" */
    uint16_t out = 0;
    input_apply_joystick_block(bindings, pressed, 0x27, &out);
    T_ASSERT_EQ_U(out, 0u);
    /* And again with non-default vcode bases — engine quirk #41 (dead
     * blocks 2..3 read BSS zero bindings, must always be no-ops). */
    input_apply_joystick_block(bindings, pressed, 0x63, &out);
    T_ASSERT_EQ_U(out, 0u);
    return 0;
}

int test_input_apply_joystick_block_ors_into_existing_mask(void)
{
    /* Caller's out_mask is preserved (engine uses |=). */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t pressed[20] = {0};
    pressed[0] = 1;

    uint16_t out = 0x1234;
    input_apply_joystick_block(bindings, pressed, 0x27, &out);
    T_ASSERT_EQ_U(out, 0x1234u | 0x0004u);
    return 0;
}

/* ─── input_apply_keyboard_block ────────────────────────────────────── */

int test_input_apply_keyboard_block_default_pad_zero(void)
{
    /* Default kbd bindings (pad[0]): 1,2,3,4 (=UP/RIGHT/DOWN/LEFT in the
     * 1-based DIK table), then 39,37,16,35,36 (=Z/X/C/V/W). */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        1, 2, 3, 4, 39, 37, 16, 35, 36,
        0, 0, 0, 0, 0,
    };
    uint8_t kbd[256] = {0};
    kbd[0xc8] = 0x80;     /* DIK_UP — binding[0]=1, mask 0x04 */
    kbd[0xcd] = 0x80;     /* DIK_RIGHT — binding[1]=2, mask 0x01 */

    uint16_t out = 0;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0x0004u | 0x0001u);
    return 0;
}

int test_input_apply_keyboard_block_z_key_for_button_a(void)
{
    /* Default pad[0][4] = 39 = Z (DIK 0x2c) → mask 0x10 (A). */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        0, 0, 0, 0, 39, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t kbd[256] = {0};
    kbd[0x2c] = 0x80;
    uint16_t out = 0;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0x0010u);
    return 0;
}

int test_input_apply_keyboard_block_low_bit_set_does_not_count(void)
{
    /* DI's "pressed" flag is the high bit (0x80); low bits are not used
     * by the engine and we mirror that. */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t kbd[256] = {0};
    kbd[0xc8] = 0x7f;     /* every low bit set, high bit clear */
    uint16_t out = 0;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0u);
    /* And confirm the high bit alone triggers. */
    kbd[0xc8] = 0x80;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0x0004u);
    return 0;
}

int test_input_apply_keyboard_block_out_of_range_binding_skipped(void)
{
    /* Binding values outside [1, 40] are silently ignored — engine's
     * `0 < sVar1 && sVar1 < 0x29` guard. */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        0, -5, 41, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    uint8_t kbd[256];
    memset(kbd, 0x80, sizeof kbd);    /* all keys "pressed" */
    uint16_t out = 0;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0u);
    return 0;
}

int test_input_apply_keyboard_block_skill_slots(void)
{
    /* skill[N][0..4] occupy binding slots 9..13 with masks 0x0200..0x2000. */
    int16_t bindings[INPUT_BINDING_SLOTS] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        14, 0, 0, 0, 22,        /* DIK_A=0x1e, DIK_I=0x17 */
    };
    uint8_t kbd[256] = {0};
    kbd[0x1e] = 0x80;
    kbd[0x17] = 0x80;
    uint16_t out = 0;
    input_apply_keyboard_block(bindings, kbd, &out);
    T_ASSERT_EQ_U(out, 0x0200u | 0x2000u);
    return 0;
}

/* ─── input_bindings_load ───────────────────────────────────────────── */

int test_input_bindings_load_default_ini_matches_engine_layout(void)
{
    struct recet_ini ini;
    recet_ini_set_defaults(&ini);
    memset(g_input_bindings, 0xff, sizeof g_input_bindings);
    input_bindings_load(&ini);

    /* Controller 0: pad[0..8] = 1,2,3,4,39,37,16,35,36; skill all zero. */
    static const int16_t expected_c0[INPUT_BINDING_SLOTS] = {
        1, 2, 3, 4, 39, 37, 16, 35, 36,
        0, 0, 0, 0, 0,
    };
    for (int i = 0; i < INPUT_BINDING_SLOTS; i++) {
        T_ASSERT_EQ_I(g_input_bindings[0][i], expected_c0[i]);
    }

    /* Controller 1: pad[0..8] = 40..48; skill all zero. */
    static const int16_t expected_c1[INPUT_BINDING_SLOTS] = {
        40, 41, 42, 43, 44, 45, 46, 47, 48,
        0, 0, 0, 0, 0,
    };
    for (int i = 0; i < INPUT_BINDING_SLOTS; i++) {
        T_ASSERT_EQ_I(g_input_bindings[1][i], expected_c1[i]);
    }

    /* Blocks 2 and 3 stay zero (engine reads BSS zeros — quirk #41). */
    for (int b = 2; b < INPUT_BINDINGS_BLOCKS; b++) {
        for (int i = 0; i < INPUT_BINDING_SLOTS; i++) {
            T_ASSERT_EQ_I(g_input_bindings[b][i], 0);
        }
    }
    return 0;
}

int test_input_bindings_load_round_trips_modified_ini(void)
{
    struct recet_ini ini;
    recet_ini_set_defaults(&ini);
    ini.pad[0][5] = 99;
    ini.skill[1][2] = 33;
    input_bindings_load(&ini);
    T_ASSERT_EQ_I(g_input_bindings[0][5], 99);
    /* skill[1][2] sits at slot 9 + 2 = 11 within controller-1 block. */
    T_ASSERT_EQ_I(g_input_bindings[1][9 + 2], 33);
    return 0;
}

/* ─── dik table sanity ──────────────────────────────────────────────── */

int test_input_dik_table_matches_vendor_dump(void)
{
    /* Spot-check the entries that the default bindings depend on — if
     * these drift the keyboard map silently breaks. */
    T_ASSERT_EQ_I(input_dik_table[1 - 1], 0xc8);   /* DIK_UP    */
    T_ASSERT_EQ_I(input_dik_table[2 - 1], 0xcd);   /* DIK_RIGHT */
    T_ASSERT_EQ_I(input_dik_table[3 - 1], 0xd0);   /* DIK_DOWN  */
    T_ASSERT_EQ_I(input_dik_table[4 - 1], 0xcb);   /* DIK_LEFT  */
    T_ASSERT_EQ_I(input_dik_table[39 - 1], 0x2c);  /* DIK_Z     */
    T_ASSERT_EQ_I(input_dik_table[37 - 1], 0x2d);  /* DIK_X     */
    T_ASSERT_EQ_I(input_dik_table[16 - 1], 0x2e);  /* DIK_C     */
    T_ASSERT_EQ_I(input_dik_table[35 - 1], 0x2f);  /* DIK_V     */
    T_ASSERT_EQ_I(input_dik_table[36 - 1], 0x11);  /* DIK_W     */
    return 0;
}

int test_input_binding_mask_dpad_face_buttons_layout(void)
{
    /* The exact mask layout is load-bearing for everything downstream
     * that reads DAT_073dddd0 (camera cursor at lines 50410..50420
     * etc.). If any of these change we've miscounted bits in the
     * poll. */
    T_ASSERT_EQ_U(input_binding_mask[0],  0x0004u);   /* UP    */
    T_ASSERT_EQ_U(input_binding_mask[1],  0x0001u);   /* RIGHT */
    T_ASSERT_EQ_U(input_binding_mask[2],  0x0008u);   /* DOWN  */
    T_ASSERT_EQ_U(input_binding_mask[3],  0x0002u);   /* LEFT  */
    T_ASSERT_EQ_U(input_binding_mask[4],  0x0010u);   /* A     */
    T_ASSERT_EQ_U(input_binding_mask[5],  0x0020u);   /* B     */
    T_ASSERT_EQ_U(input_binding_mask[6],  0x0040u);   /* C     */
    T_ASSERT_EQ_U(input_binding_mask[7],  0x0080u);   /* D     */
    T_ASSERT_EQ_U(input_binding_mask[8],  0x0100u);   /* E     */
    T_ASSERT_EQ_U(input_binding_mask[13], 0x2000u);   /* skill4 */
    return 0;
}

/* ─── input_state struct layout ─────────────────────────────────────── */

int test_input_state_stride_matches_engine(void)
{
    /* DAT_073dddfa - DAT_073dddd0 = 0x2a; the (local_8/2)*0x2a player
     * stride is load-bearing in FUN_0047b73c. */
    T_ASSERT_EQ_U(sizeof(struct input_state), INPUT_PLAYER_STRIDE);
    T_ASSERT_EQ_U(INPUT_PLAYER_STRIDE, 0x2au);
    /* `buttons` must be at offset 0 — engine treats DAT_073dddd0 as a
     * byte/short directly at that address. */
    T_ASSERT_EQ_U(offsetof(struct input_state, buttons), 0u);
    return 0;
}
