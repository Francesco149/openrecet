/*
 * scene1_chr_walker.c — Cchr.2d: HOUSE character-sprite walker.
 * See scene1_chr_walker.h for the chip writeup.  Engine FUN_00456f56.
 */

#include "scene1_chr_walker.h"

#include <stddef.h>
#include <stdint.h>

/* ── engine float constants (decoded from .rdata @ 0x519xxx 2026-05-29) ──
 *   0x519634 = 30.0   0x519900 = 0.03   0x519364 = 1.0    0x519320 = 0.0
 *   0x519520 = 20.0   0x5194f0 = 10.0   0x5198dc = 0.02
 *   0x519d04 = -75.0  0x519d0c = -70.0  0x519cb4 = 70.0
 *   0x519a50 = 50.0   0x519630 = 255.0  0x5198f8 = 0.05
 * FUN_00503954 == __ftol (truncate toward zero, i.e. the C (int) cast).   */
#define CHR_W_FADE_DIV     30.0f
#define CHR_W_FADE_SCALE   0.03f
#define CHR_W_EASE_W_DIV   20.0f
#define CHR_W_EASE_H_DIV   10.0f
#define CHR_W_NPC_OFF_HARD (-75.0f)
#define CHR_W_NPC_OFF_SOFT (-70.0f)
#define CHR_W_NPC_RAMP_OFF 70.0f
#define CHR_W_NPC_RAMP_MUL 50.0f
#define CHR_W_NPC_RAMP_MAX 255.0f

/* ── pure per-actor math (host-tested) ──────────────────────────────────── */

float chr_walker_fadein(int counter)
{
    float f = (float)(0x5a - counter) / CHR_W_FADE_DIV;
    if (f > 1.0f)               /* engine: fcomps 1.0 ; jbe skips the fld1 */
        f = 1.0f;
    return f;
}

void chr_walker_spawn_ease(int age, float *sx, float *sz)
{
    if (age < 0x14) {           /* engine: cmp 0x14 ; jge skips the ease */
        *sx = ((float)age / CHR_W_EASE_W_DIV) * *sx;
        *sz = ((float)(0x14 - age) / CHR_W_EASE_H_DIV + 1.0f) * *sz;
    }
}

int chr_walker_actor_alpha(int age, int is_party, int prio_base, int daae0)
{
    /* (0x254 - age) * 8, with the negative-skip + 0x9b clamp the engine
     * applies for BOTH sweeps before the party override. */
    int v = (0x254 - age) * 8;
    if (v < 0)                  /* engine: js -> skip the record */
        return -1;
    if (v > 0x9b)
        v = 0x9b;

    if (is_party) {             /* outer i==1 override (asm @ 0x45731d) */
        v = prio_base;
        if (daae0 < 0xa) {
            v = (daae0 - 10) * 0xf + prio_base;
            if (v < 0)          /* engine: js -> skip */
                return -1;
        }
    }
    return v;
}

int chr_walker_npc_alpha(float pos, float mult)
{
    if (pos < CHR_W_NPC_OFF_HARD)        /* fully off-screen: skip */
        return 0;
    int a = 0xff;
    if (pos < CHR_W_NPC_OFF_SOFT)        /* ramp 5..255 over [-75,-70) */
        a = (int)((pos + CHR_W_NPC_RAMP_OFF) * CHR_W_NPC_RAMP_MUL
                  + CHR_W_NPC_RAMP_MAX);  /* __ftol */
    a = (int)((float)a * mult);          /* __ftol; per-record alpha mult */
    return a;                            /* caller skips when <= 0 */
}

/* ── render-bank slot layout (the 0x44-byte actor struct) ────────────────
 * The walker reads its two after-image banks (sweep 0 = DAT_056dab6c trail,
 * sweep 1 = DAT_056dacc0 burst) through the player-controller accessors; the
 * banks are owned and written by FUN_0048b850's tail (Chip 2, see
 * scene1_player_ctrl.c / engine-quirks §76).  Each slot's fields: */
#include "scene1_chr_sprite.h"     /* CHR_ACTOR_* dword indices */
#include "scene1_player_ctrl.h"    /* player_ctrl_render_bank_slot/burst_count/actor_* */

#define CHR_W_ACTOR_POS_X   0xb    /* dword: +0x2c */
#define CHR_W_ACTOR_POS_Y   0xc    /* dword: +0x30 */
#define CHR_W_ACTOR_POS_Z   0xd    /* dword: +0x34 (engine adds +0.02) */
#define CHR_W_ACTOR_ALIVE   0xe    /* dword: +0x38 (>0 gate; == spawn age) */
#define CHR_W_ACTOR_SLOTS   5      /* (&DAT_056dae14 - &DAT_056dacc0)/0x44 */

/* ── Win32 render path (engine FUN_00456f56, full) ──────────────────────── */
#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "math3d.h"
#include "scene1_camera.h"   /* g_scene1_camera_orient (= engine DAT_0438cdf8) */
#include "scene1_preload.h"  /* scene1_preload_chr_sheet — the chr sheet table; pulls sprite.h */
#include "scene1_particles_tick.h"  /* g_scene1_actor_pos[2] = companion pos (_DAT_056da1f0/f4/f8) */

/* Engine billboard base matrix DAT_0438cdf8 — the camera orientation matrix
 * scene1_camera_angle_compute() publishes to g_scene1_camera_orient each
 * frame (scene1_render.c L372).  The walker multiplies it onto every actor
 * world matrix (objdump @ 0x4573ae: `Multiply(world, 0x438cdf8, S×T)`). */
#define CHR_W_BASE_MATRIX  g_scene1_camera_orient

/* The billboard base matrix the walker multiplies onto each actor world. */
static const float *chr_walker_base_matrix(void)
{
    return CHR_W_BASE_MATRIX;
}

/* ── engine-state accessors ──────────────────────────────────────────────
 * Pass 2's after-image banks + scale bases now point at the REAL engine state
 * (the player controller, scene1_player_ctrl.c — the live FUN_0048b850-tail
 * writer), replacing the dead synthetic single-slot inject.  The banks are
 * empty in HOUSE free-roam, and the Pass-2 entry gate (chr_walker_player_char)
 * is held closed until the entry-fade counter ports (see its note), so Pass 2
 * still draws nothing — the correct steady-state.
 *
 * Pass 1 (the companion-glow billboard DAT_056dab40 / gate DAT_056da1d4) and
 * Passes 3/4 (the people record table) stay dormant too: their writers are
 * separate unported sub-chips (the companion-glow record and FUN_00436f97's
 * people table).  When those port, swap the two accessors below to the real
 * state — exactly the scene1_shop_walker count-stub pattern. */
static int            chr_walker_top_gate(void)        { return 0; }   /* DAT_0438b8bc == 0 → run passes */
static int            chr_walker_fade_counter(void)    { return 0; }   /* DAT_0438b4b4 (0 ≤ 0x5a → run) */
/* Companion wing-glow (Pass 1): the actor[2] anim-state record (engine
 * &DAT_056dab40, = the live companion model scene1_shop_walker draws the solid
 * body from) and the companion-present gate (engine DAT_056da1d4 != -1, = the
 * companion's char id 1).  Both come from the live player-controller state. */
static int            chr_walker_companion_char(void)  { return player_ctrl_actor_char(2); }
/* DAT_056da1cc.  The live player char is player_ctrl_actor_char(0), but Pass 2
 * stays DORMANT (return -1) for two reasons, both of which make opening it a
 * no-win until a later chip: (1) its two after-image banks are empty in
 * free-roam, so it would draw nothing; (2) the engine scopes the whole
 * companion+player block to the scene-entry fade window (DAT_0438b4b4 < 0x5b),
 * which this port stubs to "always in-window" (chr_walker_fade_counter == 0) —
 * so running Pass 2 every frame would toggle z-write state outside the window
 * retail does, diverging in steady-state walk.  Opening Pass 2 = source the
 * real fade counter + have bank content (the dash-spawn chip). */
static int            chr_walker_player_char(void)     { return -1; }
static const int32_t *chr_walker_companion_actor(void) { return player_ctrl_actor_record(2); }/* &DAT_056dab40 = actor[2] */
/* sweep 0 = DAT_056dab6c trail bank (always run), sweep 1 = DAT_056dacc0 burst
 * bank (gated on the burst count below); both empty in free-roam. */
static const int32_t *chr_walker_party_slot(int sweep, int idx)
{
    return player_ctrl_render_bank_slot(sweep, idx);
}
static int            chr_walker_party_daae0(void)     { return player_ctrl_burst_count(); } /* DAT_056daae0 */
static int            chr_walker_people_count(void)    { return 0; }   /* DAT_0076c464..DAT_007c9664 */
static int            chr_walker_tail_blend_gate(void) { return 0; }   /* DAT_0438ccc8 */

/* Scale bases DAT_056dae18 / DAT_056dae24 (actor 0) — the after-image
 * billboards scale by the player's scale base × scale_f (= fade × 0.03).  The
 * controller settles both to 1.0 at the standing pose. */
static float chr_walker_scale_xz(void) { return player_ctrl_actor_scale_xz(0); }
static float chr_walker_scale_y(void)  { return player_ctrl_actor_scale_y(0); }

/* Reinterpret an actor dword as the float the engine stores there. */
static float chr_walker_actor_f(const int32_t *actor, int dword)
{
    float f;
    __builtin_memcpy(&f, &actor[dword], sizeof f);
    return f;
}

/* FUN_0047047b (296 B): a second small char-id-0x43 billboard sub-walker
 * over the DAT_073a6ea8 table (stride 0x24, count DAT_005c7dd0), gated on
 * the per-stage record at DAT_068dd2f8.  Its own (dormant) sub-chip — the
 * table is empty in HOUSE.  Stubbed here like the sibling inner walkers. */
static void chr_walker_pre_billboards_TODO(void) { /* dormant: count 0 */ }

void scene1_chr_walker_render(struct IDirect3DDevice8 *dev_in)
{
    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* ── preamble: the additive-billboard state envelope (always live) ──
     * COLOROP value 8 is verbatim from objdump @ 0x456f8d (D3DTOP_ADDSIGNED;
     * the 2b leaf re-sets COLOROP=7/8 per draw on its special-flag branch). */
    IDirect3DDevice8_SetVertexShader(dev, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1); /* 0x142 */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   8);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    chr_walker_pre_billboards_TODO();          /* FUN_0047047b */

    /* The companion + player passes only run while the top gate is 0 and
     * the scene-entry fade-in counter is within range (<= 0x5a). */
    if (chr_walker_top_gate() == 0) {
        int counter = chr_walker_fade_counter();
        if (counter <= 0x5a) {
            float fade = chr_walker_fadein(counter);
            float scale_f = fade * CHR_W_FADE_SCALE;

            /* ── Pass 1: the companion (Tear) wing-glow billboard ───────────
             * Engine FUN_00456f56 L49-69: an ADDITIVE billboard at the
             * companion position, drawn from char-descriptor 2 / sheet 2
             * (chr02.bmp = the fairy glow sheet, engine DAT_073a9b38), tinted
             * grey 0xff7f7f7f (the blue is the texture, like the trail
             * sparkles).  The actor record is actor[2] (the live companion
             * anim-state, = engine &DAT_056dab40); world = base × Scaling(cw,
             * ch, cw) × Translation(companion pos), cw/ch = _DAT_056dae20/2c ·
             * scale_f.  The glow scale cw/ch settles to 1.0 at the standing
             * pose (engine-quirks §71); the port does not separately model the
             * dae20/dae2c globals, so 1.0 is used. */
            const int32_t *comp = chr_walker_companion_actor();
            if (comp != NULL && chr_walker_companion_char() != -1) {
                const sprite_t *wing = scene1_preload_chr_sheet(2); /* DAT_073a9b38 */
                if (wing != NULL && wing->tex != NULL) {
                    float cw = 1.0f * scale_f;   /* _DAT_056dae20 · scale_f */
                    float ch = 1.0f * scale_f;   /* _DAT_056dae2c · scale_f */
                    float world[16], scale[16], tmp[16];
                    mat4_translation(tmp,
                                     g_scene1_actor_pos[2][0],
                                     g_scene1_actor_pos[2][1],
                                     g_scene1_actor_pos[2][2]);
                    mat4_scaling(scale, cw, ch, cw);
                    mat4_mul(tmp, scale, tmp);
                    mat4_mul(world, chr_walker_base_matrix(), tmp);
                    /* L67: SetTexture(0, DAT_073a9b38) then additive ONE/ONE. */
                    IDirect3DDevice8_SetTexture(dev, 0,
                        (IDirect3DBaseTexture8 *)wing->tex);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
                    scene1_chr_sprite_render(dev_in, comp, 2, world, 0xff7f7f7fu,
                                             (int)wing->width, (int)wing->height);
                }
            }

            /* ── Pass 2: the player + party billboards ─────────────────── */
            if (chr_walker_player_char() != -1) {
                int player_char = chr_walker_player_char();
                int blend_set = 0;
                /* The player sprite sheet (engine DAT_073a9b18[char*0x10]);
                 * NULL until scene1_preload_load_chr_sheet runs (under
                 * --force-chr-walker).  Bound once on the first live actor,
                 * mirroring the engine latch @ 0x456fe6 (local_24==0). */
                const sprite_t *sheet = scene1_preload_chr_sheet(player_char);
                for (int sweep = 0; sweep < 2; sweep++) {
                    if (sweep == 1 && chr_walker_party_daae0() == 0)
                        continue;
                    int prio = 0x9b;
                    for (int slot = 0; slot < CHR_W_ACTOR_SLOTS;
                         slot++, prio -= 0x14) {
                        const int32_t *actor = chr_walker_party_slot(sweep, slot);
                        if (actor == NULL)
                            continue;
                        int age = actor[CHR_W_ACTOR_ALIVE];
                        if (age <= 0)
                            continue;

                        int alpha = chr_walker_actor_alpha(
                            age, sweep == 1, prio, chr_walker_party_daae0());
                        if (alpha < 0)
                            continue;

                        if (!blend_set) {       /* bind on first live actor */
                            blend_set = 1;
                            /* engine @ 0x456ff0: SetTexture(0, sheet) then
                             * DEST=6, SRC=5.  When no sheet is loaded the
                             * stage keeps whatever was bound → diffuse-only
                             * white silhouette (geometry still validates). */
                            if (sheet && sheet->tex)
                                IDirect3DDevice8_SetTexture(dev, 0,
                                    (IDirect3DBaseTexture8 *)sheet->tex);
                            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,
                                                            D3DBLEND_INVSRCALPHA);
                            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,
                                                            D3DBLEND_SRCALPHA);
                        }

                        /* Scaling args (objdump @ 0x457369-0x45738b):
                         *   x = dae18×scale_f (eased),  y = dae24×scale_f
                         *   (eased),  z = dae18×scale_f (fresh, NOT eased). */
                        float sx = chr_walker_scale_xz() * scale_f;
                        float sz = chr_walker_scale_y()  * scale_f;
                        float z_scale = chr_walker_scale_xz() * scale_f;
                        chr_walker_spawn_ease(age, &sx, &sz);
                        float world[16], scale[16], tmp[16];
                        mat4_translation(tmp,
                            chr_walker_actor_f(actor, CHR_W_ACTOR_POS_X),
                            chr_walker_actor_f(actor, CHR_W_ACTOR_POS_Y),
                            chr_walker_actor_f(actor, CHR_W_ACTOR_POS_Z) + 0.02f);
                        mat4_scaling(scale, sx, sz, z_scale);
                        mat4_mul(tmp, scale, tmp);
                        mat4_mul(world, chr_walker_base_matrix(), tmp);
                        uint32_t color = ((uint32_t)alpha << 24) | 0x7f7fffu;
                        /* tex dims drive the leaf's atlas UVs (atlas_cols =
                         * tex_w/32, V denom = tex_h) — engine reads them from
                         * the sheet record's +4/+8 fields.  Source from the
                         * loaded sprite; fall back to the validated Recette
                         * 512×1024 when no sheet (diffuse-only, geometry only). */
                        int tex_w = (sheet && sheet->tex) ? (int)sheet->width  : 512;
                        int tex_h = (sheet && sheet->tex) ? (int)sheet->height : 1024;
                        scene1_chr_sprite_render(dev_in, actor, player_char,
                                                 world, color, tex_w, tex_h);
                    }
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
                }
            }
        }
    }

    /* ── mid state (always live) ────────────────────────────────────────
     * restore filters, drop light 0 + lighting, fog off. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* tail blend (engine asm @ 0x45763e): SRCBLEND = SRCALPHA in BOTH
     * branches; DAT_0438ccc8 == 1 → DESTBLEND = INVSRCALPHA(6), else ONE(2). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,
        chr_walker_tail_blend_gate() == 1 ? D3DBLEND_INVSRCALPHA : D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CW); /* 3 */

    /* ── Pass 4: the people sub-render loop (FUN_00456d48) ───────────────
     * Walks the people record table for ACTIVE records of type 1 with
     * STATUS_F != 0xff and emits via FUN_00456d48 (a no-op stub in the
     * port today, same as scene1_shop_walker Pass F).  Dormant: count 0. */
    for (int i = 0; i < chr_walker_people_count(); i++) {
        /* engine: FUN_00456d48(record - 0x109); stubbed (no-op) */
    }

    /* ── Pass 3 (NPC billboards, char id 0x43) is folded into the people
     * walk above when populated: per-record off-screen fade via
     * chr_walker_npc_alpha + a base×Scaling×Translation billboard.  Left as
     * the count-0 dormant body; its record-field offsets (-0x6a4 pos,
     * +4 mult, type-table scale @ &DAT_005c23f0[type*0x68]+0x44) are
     * documented in scene1-char-sprite-render.md "Cchr.2d". */

    /* tail: restore the base MODULATE alpha pipe; clear the people-pass
     * scratch flag DAT_06a49b20. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

#endif /* _WIN32 */
