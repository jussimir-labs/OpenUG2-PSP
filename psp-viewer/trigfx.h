/* The street's event markers, as the original's (GameCube Speed.elf:
 * UpdateTriggerEffects, TriggerEffectEmitters, rewritten): out in free roam
 * each kind of place the career offers has its particle effect, at the
 * nearest place of that kind within 200 m of the car; a new place starts it
 * over. Each effect is the game's emitter group (trigfx.n2te,
 * tools/convert_trigfx.py: XEE_EVENT_CIRCUIT...): its icon, a camera-facing
 * sprite 3 m up that grows in, holds and shrinks away (two at a time), a
 * ring of light turning on the ground, and for some kinds a burst of smoke.
 * The particles follow the game's rules (AcidEffectSystem::TriggerEmitter,
 * DoTimeStep, RenderEffects): so many a second, each with its life, speed,
 * gravity and drag, its colour and size by the cubic of its age, drawn
 * within 300 m with its texture's blend.
 *
 * The shops' doors have theirs placed in the world (the level's emitters in
 * the free roam's scenery groups: the shop's icon, a ring, smoke along the
 * roof's beam and a puff on the ground): each emits on its own while its
 * group is on and the car is within its distance (the game's auto
 * emitters, AcidEffectSystem::DoTimeStep).
 *
 * The menus' street (showroom.n2sc) has its own: the steam rising from the
 * manhole left of the turntable (SHOWROOM.BIN's XEM_SHOWROOM_SMOKE_01, beta
 * issue 128), always on while the menus show that street, stepped by the
 * menus' clock (GarageMainScreen::HandleTick) and drawn in the scene's
 * frame. */
#ifndef NFSU2_TRIGFX_H
#define NFSU2_TRIGFX_H

/* TheTriggerEffectMap's order (the zones' types 0-5, 6, 15, 7, 8; the tips'
 * list, the cash's). No zone has the DVD's type: the game never shows it. */
enum { TFX_CIRCUIT, TFX_DRAG, TFX_STREET, TFX_DRIFT, TFX_SPRINT, TFX_URL, TFX_SHOWCASE, TFX_CHALLENGE, TFX_SUV, TFX_DVD,
       TFX_TIP, TFX_BANK, TFX_KINDS };
enum { TFX_RANGE=200 };   /* m: the place's distance to the car (ConsiderTriggerEffectZone) */

/* trigfx.n2te; 0 without it (no markers). */
int trigfx_load(const char *path);
/* This frame's place of a kind (GU, on the ground), NULL for none: the
 * effect stops at once (its particles go), and a place other than the
 * last starts it over there. */
void trigfx_place(int kind,const float *p);
/* The shops' emitters: the scenery groups on (TFX_ROAM...; 0 none) and the
 * car (GU), this frame. */
enum { TFX_ROAM=1, TFX_ROAM_STAGE0=2, TFX_ROAM_STAGE0A=4, TFX_ROOM=8 };   /* TFX_ROOM: the showroom's (trigfx_room) */
void trigfx_world(int groups,const float *car);
/* Every effect off (a race, a closed track, the photo shoot's camera). */
void trigfx_off(void);
void trigfx_update(float dt);
/* ro: the render origin, eye, the camera's right, up and forward (GU). */
void trigfx_draw(const float ro[3],const float eye[3],const float right[3],const float up[3],const float fwd[3]);
/* The showroom's emitters on (the menus in its street) or off (its
 * particles go); their step, apart from the street's (trigfx_update). */
void trigfx_room(int on);
void trigfx_room_update(float dt);
/* Its particles, in the scene's frame: m its model matrix, the eye and the
 * camera's right, up and forward in that frame. */
struct ScePspFMatrix4;
void trigfx_room_draw(const struct ScePspFMatrix4 *m,const float eye[3],const float right[3],const float up[3],const float fwd[3]);
int trigfx_live(void);   /* particles alive */
int trigfx_kinds(void);  /* the kinds on (bits, 1 << TFX_) */
int trigfx_sites(void);  /* the shops' emitters emitting */
int trigfx_room_live(void);   /* the showroom's particles alive */

#endif
