/* Car effects: tyre smoke, sparks off the walls, nitrous flames from the
 * exhaust, and the nitrous speed lines. Camera-facing sprites (the GE draws
 * a 3D sprite from two corners) with the game's own textures (fx.n2tx:
 * SFX_SMOKE_B_CS, SPARK, SFX_FIREB from GLOBAL\INGAMECOMMON.LZC), or
 * small ones made at start without it. */
#ifndef NFSU2_FX_H
#define NFSU2_FX_H
#include <stdint.h>

/* FX_EMBER and FX_FLARE: the flag girl's flares (their sparks and glow
 * falling off the torch, and the light flare at its tip, a frame long),
 * always made here (fx.n2tx holds the first three). */
/* FX_GLASS: a window's shards (damage.n2tx's). FX_PURGE: the Nitrous
 * Purge's jets (fx_emit_purge), in the smoke's texture. */
enum { FX_SMOKE, FX_SPARK, FX_FLAME, FX_EMBER, FX_FLARE, FX_GLASS, FX_PURGE, FX_KINDS, FX_FILE_KINDS=3 };

void fx_init(void);
/* A kind's art in place of the made one: 32 x 32, the PSP's ABGR (copied). */
void fx_texture(int kind,const uint32_t *rgba);
void fx_clear(void);
/* One particle at GU (x,y,z) moving at (vx,vy,vz) m/s. */
void fx_emit(int kind,float x,float y,float z,float vx,float vy,float vz);
/* The same tinted (BGR, multiplying the texture): dust off dirt and grass
 * is the smoke's puff in the ground's colour. */
void fx_emit_tint(int kind,uint32_t tint,float x,float y,float z,float vx,float vy,float vz);
/* A puff of the Nitrous Purge (the XES_NOSBO_* emitters): life (s), its
 * size from size0 to size1 (m across), slowed by drag (quadratic, per
 * axis) and rising at rise m/s2; its alpha the emitters' keys (40, 150,
 * 80, 0 over its life: the PS2's scale, 128 opaque), its colour tint (BGR, the
 * PS2's scale: 128 full, drawn doubled). */
void fx_emit_purge(uint32_t tint,float life,float size0,float size1,float drag,float rise,float x,float y,float z,float vx,float vy,float vz);
void fx_update(float dt);
/* Draw them; ro: the render origin (GU), right/up: the camera's axes. */
void fx_draw(const float ro[3],const float right[3],const float up[3]);
int fx_live(void);

#endif
