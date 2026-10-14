# Shop counter menu ("Open Store" / "Cancel") — RE + port

Retail ground truth: rhost `buy-40pct-accept` (slot-9 save, Day 5), `--watch` on the
globals below. Port: `src/scene1_counter_menu.c` + the cc08 0/10 arm in
`src/scene1_player_ctrl.c` (2026-10-14).

## Why it mattered
Every native shop recording diverged at shop entry: the port's `CUSTOMER_SERVICE_ENTER`
came 58–68 frames early (day2-shop −63, day2-buy-tutorial −68, buy-40pct-accept −58).
The port entered customer service on the first Z at the counter. Retail opens a menu,
waits for a second Z, then runs a 15-frame close. −58 = (Z₂ − Z₁ = 43) + 15. With the
menu ported, the first `CUSTOMER_SERVICE_ENTER` is frame-exact on all 7 shop scenarios.

## Counter Z (FUN_0048670f, asm 0x488bb6-0x488c18)
Gate `v132` (all.c:97319-97333):
- counter rect: tier<3 → X∈(−5,0), Z>6.9; tier≥3 → X∈(5.7,10.7), Z>15;
- `DAT_0450f3fd == 1` (all stands stocked);
- facing ∈ (−1.8849558, −1.2566371).

Branch on `DAT_0450f3ff`:
- `f3ff != 0`: the tutorial's scripted sale. f3ff is set together with f3fe when iv1_6
  fires and cleared at the customer-service leave. Sequence: `f404=1`,
  `FUN_00461bf6(0)`, `cc08=4`, `FUN_0045edaa`.
- `f3ff == 0` (every ordinary day): `cc08=0`, `b754=cc0c=cc10=0`, `FUN_004850fe`,
  SE `00re_sys06`, `FUN_00499519(0x174)`.

Both arms then do `db000=0` and `goto LAB_004893ff`. That skips the player anim tick
and FUN_0048a833 on the entry frame.

`f404` stays 0 on the menu path, so FUN_0045edaa takes the general roster scan. The
port's old shortcut set f404=1 and ran the scripted-sell path on ordinary days.
`DAT_005c6bb0` (fileidx) is untouched on this path: retail's .data init is **1**, the
port's mirror starts at 0 (only read on the f404 path).

## FUN_004850fe — the entry lists
List A, `DAT_074b2d98[]`, count `DAT_005cd0bc`. Labels `off_0052974c`: Open Store,
Change Flooring, Change Walls, Change Carpet, Change Tables, Move Tables, Cancel.

```
A = [0]; level≥5 → +2; ≥7 → +1; ≥8 → +3; ≥9 → +5; ≥13 → +4; then 6 (Cancel)
```

`level` = the merchant-level dword `DAT_0450fb98` (0-based; the HUD shows +1).

List B, `DAT_074b28b0[]`, count `DAT_005cd0c0`, is the cc08 15 Tear menu (unported
arms):

```
[0,5,7]; day≥9 → +8; level≥6 → +9; DAT_0450f423 → +6; then 4
```

## Per-frame
- **Slide ramp** `DAT_0438b74c`, before the cc08 switch: +1 (max 12) while
  cc08∈{0,10,15,16,30}, else −1. `DAT_0438b750` does the same for cc08 30.
- **case 0:**
  - player anim → 3 if `DAT_056daafc != 3`;
  - `b74c < 10` → no input;
  - else, on edge mask `DAT_073dddd4`:
    - 0x20 → cc08 1 + SE id 0x13d;
    - Z → cc08 10 + SE id 0x143;
  - else, on repeat mask `DAT_073dddd6`: 0x4/0x8 → cursor ∓1 mod n + SE id 0x146;
  - exit via LABEL_132 = FUN_004897c6(player) + FUN_0048a833 + tail.
- **case 10:**
  - `++b754 == 15` → dispatch `A[cc0c]`:
    - 6 → cc08 1;
    - 0 → cc08 4 + FUN_0045edaa + `rand()%5` voice (00re_sys11a/b/c, 01ti_sys11a/b);
    - 1..5 → cc08 0x17 + the decor camera (FUN_00452dc1/d85/dfd/e39), SE 00re_sys04c.
      This is `PORT-DEBT(counter-menu-decor)`.
  - exit via LABEL_133 = FUN_0048a833 + tail (no player anim tick).
- db054 does not advance in cc08 0/10 (FUN_0048b850 doesn't run).

Measured: Z@793 → cc08 0, b74c 1..12 from f794 (input live from f803). Z@836 → cc08 10,
b754 1..15. cc08 4 at f851.

## Render (FUN_00409925 cc08 0/10 block, all.c:6860-6960)
- COLOROP ADDSIGNED, texture item_win.tga.
- Per entry i, x = `30·b74c − (16i+256)`, clamped to 0 for the cursor entry and −40
  otherwise. Non-cursor entries also move by `−20·b754`.
- Button: src (448,944)-(704,1008), dst (x+10, 48(7−n)+48i+128, 256, 64). Tint
  0xff7f7f7f; the cursor entry pulses to gray `127−(int)(sin(b754·π/15)·−128)`.
- Label at (x+154, 48(7−n)+48i+152), scale 0.8. Color 0xff464646; the cursor entry uses
  `u | (u+127)<<16 | (u+127)<<8`, u = `(int)(sin·128)`.

## Help box (FUN_00406584 mode-1 block, all.c:4655-4664)
- In the shop stage (`*DAT_068dd2f0 == 0`):
  - band `DAT_00529708 = 2` ("Opening the shop will take 1 period of time") while
    cc08∈{0,10} and cc0c==0, with counter `DAT_00529704` +1 up to 15;
  - else −1 down to 0.
- FUN_004060ff (HOUSE init FUN_00436f97, before FUN_004850ec) sets both to −1, which is
  also the .data init.
- Retail order: FUN_00406584 runs after the HOUSE tick, so the Z frame already counts
  (f793 = 0).
