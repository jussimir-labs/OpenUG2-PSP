TARGET = nfsu2-viewer
OBJS = main.o allocator_diag.o ending.o zones.o lang.o world.o roads.o lanes.o tdensity.o gps.o outrun.o vehicle.o physics_ug2.o steering.o race.o audio.o orca.o catchup.o career.o mycars.o shopzones.o cars.o garage.o rides.o perf.o bars.o boost.o moments.o modes.o fx.o flares.o rain.o pmap.o envsound.o fog.o hud.o music_card.o fe.o controls.o savedata.o people.o intro.o nisscene.o shopnis.o tvcam.o probe.o rewind.o gudraw.o tristrip.o build_id.o movie.o
OBJS += drag_park.o
OBJS += fpu_guard.o   # the FPU's exceptions off in every thread, the invalid operations counted (beta issues 103 and 108)
OBJS += terrain.o skids.o smokeable.o worldanim.o   # the animated world; the ground's kinds, the tyre marks
OBJS += engsnd.o   # last: the engines' sound (L3) leaves the rest of the code where it was
OBJS += memory_profile.o   # AUTO capacity policy; no PSP imports in the decision module
OBJS += stream_predict.o stream_smoke.o
OBJS += impacts.o   # the impacts' sound, after it for the same reason
OBJS += driveby.o   # the wooshes, the same
OBJS += tyres.o   # the tyres and the road, the same
OBJS += rival_tyres.o   # one spatial tyre emitter per racing/free-roam rival
OBJS += nos_bank.o nos_sound.o nos_mixctl.o audio_nos.o   # immutable native NOS/PURGE samples and mixer-owned graphs
OBJS += volmem.o   # the volatile 4 MB (the PSP-1000's city geometry), the same
OBJS += powerlog.o   # the power log (POWER_LOG=1: suspend and resume, on the card), empty without
OBJS += loading_progress.o   # truthful phase/batch display, no I/O or PSP imports
OBJS += trigfx.o   # the street's event markers, the same
# Fixture-free original human Drift path. Source sidecars are produced by the
# user's generator; immutable configuration is outside the rewind POD.
OBJS += original_core.o ground_penalty.o og_branch_helpers.o og_jump.o og_shift_math.o
OBJS += body_adapter.o og_classification.o og_pipeline.o og_profile.o og_world_bridge.o
OBJS += n2og_asset.o nitro_finalize.o package_composer.o pedal_mapper.o road_adapter.o original_player.o
OBJS += div0_guard.o   # integer division by zero without the trap, the 64-bit ones counted (beta issues 44 and 148)
# make ALLOC_SITES=1: a diagnostic build, the game's heap blocks counted by
# file, line and boot step (alloc_sites.h, written to alloc-sites.txt)
ifdef ALLOC_SITES
OBJS += alloc_sites.o
ALLOC_FLAGS = -DALLOC_SITES -include alloc_sites.h
endif
# -MMD -MP: rebuild every object whose headers changed (a stale struct
# layout in one object corrupts memory at run time).
CFLAGS = -O2 -G0 -Wall -Wextra -std=c11 -MMD -MP -include fastmath.h   # fminf/fmaxf inline, the same results (fastmath.h)
# Loading diagnosis only; native reads unchanged, native writes refused.
PERF_AB_DIAG ?= 0
ifeq ($(PERF_AB_DIAG),1)
CFLAGS += -DPERF_AB_DIAG=1
endif
NFSU2_DIAG_NATIVE_READONLY ?= 0
ifeq ($(NFSU2_DIAG_NATIVE_READONLY),1)
CFLAGS += -DNFSU2_DIAG_NATIVE_READONLY=1
endif
# Test builds only: the saves under another name (savedata.c), the
# player's NFSU2VIEW* left as they are.
ifneq ($(SAVE_GAME_NAME),)
CFLAGS += -DNFSU2_SAVE_GAME_NAME=\"$(SAVE_GAME_NAME)\"
endif
# Bounded observation only; OFF emits no code/storage/export.
WORLD_DISPATCH_DIAG ?= 0
ifeq ($(WORLD_DISPATCH_DIAG),1)
CFLAGS += -DWORLD_DISPATCH_DIAG=1
endif
# Perturbative V4 experiment; never enabled in conventional builds.
WORLD_COOP_RESUME_DIAG ?= 0
ifeq ($(WORLD_COOP_RESUME_DIAG),1)
CFLAGS += -DWORLD_COOP_RESUME_DIAG=1
endif

# Private issue 95 trace: script 27 yaw 3. No trace code/strings in player builds.
SHOP_EXIT_DIAG ?= 0
ifeq ($(SHOP_EXIT_DIAG),1)
CFLAGS += -DSHOP_EXIT_DIAG=1
endif

# Private per-rival branch observation; conventional player has no storage/code.
# Experimental original DrivingChoice provider, default OFF until runtime proof.
ORIGINAL_PATH_CHOICE ?= 0
ifeq ($(ORIGINAL_PATH_CHOICE),1)
CFLAGS += -DORIGINAL_PATH_CHOICE=1
OBJS += original_path_provider.o original_drive_target.o
endif
RACER_DRIVE_DIAG ?= 0
ifeq ($(RACER_DRIVE_DIAG),1)
CFLAGS += -DRACER_DRIVE_DIAG=1
endif

# Profile-guided optimisation with link-time optimisation (tools/pgo.sh does
# it all; OTIMIZACAO.md, "Compilador"): PGO=gen builds the game counting its
# branches and calls, which writes pgo/*.gcda next to the EBOOT when it quits
# (main.c's __gcov_dump); PGO=use, the default once PGO_DIR has a profile
# (PGO=off without), builds with it. The .gcda are named after the source
# (main.gcda), not its folder on the Mac. Code changed since the profile only
# loses the profile of the functions changed (-Wno-coverage-mismatch);
# functions the run never reached keep -O2 (-fprofile-partial-training).
# The counters are bumped by every thread without locks (the car's strips
# built in a loading thread as on the main one): -fprofile-correction
# evens out the counts they lose, which gcc otherwise refuses ("corrupted
# value profile", the main menu's run, 21/10).
# LTO (the whole game optimised as one program; the link takes CFLAGS too)
# goes with the profile: alone it cost the PSP-3000 1.5% more CPU cycles a
# street frame, with the profile 6.9% fewer. LTO=0/1 overrides it for A/B.
# Integer division by zero (div0_guard.c; beta issues 44 and 148). GCC
# puts a trap (break 7) after every division whose divisor is not a
# constant; on a player's PSP it stops the console. DIV0=quiet (the
# default): -mno-check-zero-division, the Allegrex's own result as the
# PPSSPP gives it and as the GameCube's build goes on (the PS2's traps as
# ours did), the code generator's only (the PGO profile still matches),
# and libgcc's 64-bit divisions
# through wrappers that count a zero divisor. DIV0=diag: every division
# checked and counted by file and line (-fsanitize=integer-divide-by-zero,
# its handler in div0_guard.c), without the profile, whose control flow it
# changes: a test build. DIV0=trap: GCC's default, the traps back (an A/B).
DIV0 ?= quiet
ifeq ($(DIV0),diag)
PGO = off
CFLAGS += -DDIV0_MODE=2 -mno-check-zero-division -fsanitize=integer-divide-by-zero
else ifeq ($(DIV0),trap)
CFLAGS += -DDIV0_MODE=0
else
CFLAGS += -DDIV0_MODE=1 -mno-check-zero-division
endif
PGO_DIR ?= pgo
PGO ?= $(if $(wildcard $(PGO_DIR)/*.gcda),use,off)
LTO ?= $(if $(filter gen use,$(PGO)),1,0)
ifeq ($(LTO),1)
CFLAGS += -flto=auto
endif
ifeq ($(PGO),gen)
CFLAGS += -fprofile-generate=pgo -fprofile-prefix-path=$(CURDIR) -DPGO_DUMP
else ifeq ($(PGO),use)
$(info Using profile data from $(PGO_DIR); set PGO=off to build without it)
CFLAGS += -fprofile-use=$(abspath $(PGO_DIR)) -fprofile-prefix-path=$(CURDIR) -fprofile-partial-training -fprofile-correction \
	-Wno-coverage-mismatch -Wno-missing-profile
endif
# make RELEASE=1: the players' I/O policy (release.h: no HostCmd, no
# diagnostic files or telemetry from a player's run); the development
# overlay is controlled separately by developer_overlay in main.c,
# with the same optimisation as the default (the PGO profile and LTO above),
# and no extra globals kept solely for PSPLink (below). tools/ms_package.py
# make --release packs it.
RELEASE ?= 0
# The game's version (tools/version.py): VERSION at the root is the one
# source. A local make embeds X.Y.Z-dev; tools/release_candidate.py passes
# its candidate's X.Y.Z-rc.N. On the title screen, above the frame-time line,
# in bug-N.n2bug and viewer-stats.json (build_id.c), and in PARAM.SFO: TITLE,
# DISC_VERSION X.YZ and APP_VER 0X.YZ (the XMB's two digits after the point:
# minor and patch, 9 at most each; 0.1.0 is the 00.10 the alpha already had).
GAME_VERSION ?= $(or $(shell sed -n 1p ../VERSION 2>/dev/null),0.0.0)-dev
GAME_VERSION_XYZ := $(shell echo '$(GAME_VERSION)' | sed 's/[-+].*//')
SFO_VERSION = $(shell echo '$(GAME_VERSION_XYZ)' | awk -F. '{m=$$2>9?9:$$2;p=$$3>9?9:$$3;printf "$(1)",$$1,m,p}')
ifeq ($(RELEASE),1)
CFLAGS += -DRELEASE=1
endif
# make AUDIO_CUT=1 (no mixer or music threads) or 2 (the music alone): a
# measurement build of the mixer's cost (audio.c); rebuild audio.o clean
ifdef AUDIO_CUT
CFLAGS += -DAUDIO_CUT=$(AUDIO_CUT)
endif
# make NEAR_M=... RANGE_M=... (metres): a measurement build that draws near
# geometry and the street farther out (world.c world_near_m, main.c
# drive_range_m; on the PSP the same by poke); rebuild world.o and main.o clean
# make NEAR_DOWN=50 NEAR_UP=35: the adaptive swap's GE waits, 0.1 ms (main.c near_wait_tenths, near_up_tenths)
ifdef NEAR_DOWN
CFLAGS += -DNEAR_DOWN=$(NEAR_DOWN)
endif
ifdef NEAR_UP
CFLAGS += -DNEAR_UP=$(NEAR_UP)
endif
# make AI_CLOSE_M=0: racers decide every other 60 Hz step always (main.c ai_close_m), the A/B
ifdef AI_CLOSE_M
CFLAGS += -DAI_CLOSE_M=$(AI_CLOSE_M)
endif
# make NAN_CHECK=1: the test ends at the drivers' first NaN (vehicle.h car_input_guard), a diagnosis
ifdef NAN_CHECK
CFLAGS += -DNAN_CHECK=$(NAN_CHECK)
endif
# make DOOR_STENCIL=0: the screen door's matrix as before (world.c world_door_stencil), the A/B of its cost
ifdef DOOR_STENCIL
CFLAGS += -DDOOR_STENCIL=$(DOOR_STENCIL)
endif
# make SKY_METRIC=1: the sky's pass a key, sky-metric.json (main.c sky_metric_frame), a diagnosis for the PPSSPP
# make SKY_HAZE=0: the night fog in the region's colour, not the sky's (main.c sky_haze)
ifdef SKY_HAZE
CFLAGS += -DSKY_HAZE=$(SKY_HAZE)
endif
# make FREE_CHECK=1: free() checked against the heap's layout, the bad ones logged (free_check.c); a test build, the allocator's gate off
ifeq ($(FREE_CHECK),1)
OBJS += free_check.o
endif
# make MALLOC_POISON=1: malloc, memalign and realloc's growth filled with a pattern, as the PSP's RAM is (malloc_poison.c); a test build
ifeq ($(MALLOC_POISON),1)
OBJS += malloc_poison.o
endif
ifdef SKY_METRIC
CFLAGS += -DSKY_METRIC=$(SKY_METRIC)
endif
# make NEAR_WIDE=90: the near model's reach on the bigger heap (world.c world_near_wide_m)
# make PRECULL=0: without the original's preculler (world.c world_precull)
ifdef PRECULL
CFLAGS += -DPRECULL=$(PRECULL)
endif
ifdef NEAR_WIDE
CFLAGS += -DNEAR_WIDE=$(NEAR_WIDE)
endif
ifdef NEAR_M
CFLAGS += -DNEAR_M=$(NEAR_M)
endif
ifdef RANGE_M
CFLAGS += -DRANGE_M=$(RANGE_M)
endif
# make POPIN=1: the pop-in's diagnosis on from the boot (world_popin_log; viewer-stats popin_events)
ifdef POPIN
CFLAGS += -DPOPIN_LOG=$(POPIN)
endif
# make OVERLAY_M=0: the race overlay at the city's range (world.c world_overlay_m), the A/B
ifdef OVERLAY_M
CFLAGS += -DOVERLAY_M=$(OVERLAY_M)
endif
# make FADE_MS=0: cells at the range's edge pop in again (world.c world_fade_ms), the A/B
ifdef FADE_MS
CFLAGS += -DFADE_MS=$(FADE_MS)
endif
# make XFADE_MS=0: the near/far swap at once again (world.c world_xfade_ms), the A/B
ifdef XFADE_MS
CFLAGS += -DXFADE_MS=$(XFADE_MS)
endif
# make HORIZON_BAND=0: no haze band over the sky's horizon (main.c horizon_band)
ifdef HORIZON_BAND
CFLAGS += -DHORIZON_BAND=$(HORIZON_BAND)
endif
# make TRAFFIC_FADE_M=0 TRAFFIC_AHEAD_M=130: the traffic as before (main.c traffic_fade_m, traffic_ahead_m)
ifdef TRAFFIC_FADE_M
CFLAGS += -DTRAFFIC_FADE_M=$(TRAFFIC_FADE_M)
endif
ifdef TRAFFIC_AHEAD_M
CFLAGS += -DTRAFFIC_AHEAD_M=$(TRAFFIC_AHEAD_M)
endif
# make FAR_OVER_VISTA=0: the distant layer depth-tested against the panoramas (main.c far_over_vista)
ifdef FAR_OVER_VISTA
CFLAGS += -DFAR_OVER_VISTA=$(FAR_OVER_VISTA)
endif
# make FAR_DEBUG=1: the distant layer flat magenta (main.c draw_far_layer), a diagnosis
ifdef FAR_DEBUG
CFLAGS += -DFAR_DEBUG=$(FAR_DEBUG)
endif
# make LANDMARKS_ON=0: the distant layer off from the boot (world.c world_landmarks)
ifdef LANDMARKS_ON
CFLAGS += -DLANDMARKS_ON=$(LANDMARKS_ON)
endif
# make LANDMARK_M=800: the distant layer's reach (world.c world_landmark_m)
ifdef LANDMARK_M
CFLAGS += -DLANDMARK_M=$(LANDMARK_M)
endif
# make DISTANT_FADE=0: the distant layer's cells at once at its far edge (world.c world_distant_fade)
ifdef DISTANT_FADE
CFLAGS += -DDISTANT_FADE=$(DISTANT_FADE)
endif
# make FADE_BLUR=1: the screen door's other pixels drawn from the cell's smallest mip (off by default since 22/10) (world.c world_fade_blur)
ifdef FADE_BLUR
CFLAGS += -DFADE_BLUR=$(FADE_BLUR)
endif
# make GROUND_FILL=0: the screen door's other pixels left at the clear colour, nearly black, as before 22/10; 1: the band (world.c world_ground_fill)
# make GU_CACHED_LIST=0: the direct list through pspgu's uncached view, as before 22/10 (gudraw.c gu_cached_list)
ifdef GU_CACHED_LIST
CFLAGS += -DGU_CACHED_LIST=$(GU_CACHED_LIST)
endif
# make GU_LIST_AUDIT=1: a diagnosis, the list's bytes written after the GE was let past them (gudraw.c, gu-audit.log)
ifdef GU_LIST_AUDIT
CFLAGS += -DGU_LIST_AUDIT=$(GU_LIST_AUDIT)
endif
# make FRAME_SEAL=1: a diagnosis, each street frame's seal in the corner checked on the screen (main.c, viewer-stats "frame_seal")
ifdef FRAME_SEAL
CFLAGS += -DFRAME_SEAL=$(FRAME_SEAL)
endif
# make RACE_JOIN=0: a part's textures each a read of its own in the race, as before 23/10 (world.c world_race_join)
ifdef RACE_JOIN
CFLAGS += -DRACE_JOIN=$(RACE_JOIN)
endif
ifdef GROUND_FILL
CFLAGS += -DGROUND_FILL=$(GROUND_FILL)
endif
# make XFADE_IN=1: a cell's far model giving way to its near one through the screen door too, as before 22/10 (world.c world_xfade_in)
ifdef XFADE_IN
CFLAGS += -DXFADE_IN=$(XFADE_IN)
endif
# make FADE_DOOR=0: the range's fade-in through the fog, not the screen door (world.c world_fade_door)
ifdef FADE_DOOR
CFLAGS += -DFADE_DOOR=$(FADE_DOOR)
endif
# make CAR_LODS=0: the cars' far levels off from boot (main.c car_lods), the
# same code; for the real PSP's A/B, whose poke comes after the traffic's load
ifdef CAR_LODS
CFLAGS += -DCAR_LODS=$(CAR_LODS)
endif
# make CAR_MERGE=0: the racers' and traffic's meshes as packed from boot
# (main.c merge_plain), the same code; the PSP's A/B, as CAR_LODS (both in
# POKED by name: their declarations, in .data by an attribute, escape the
# sed below, and LTO folded the value into the code)
ifdef CAR_MERGE
CFLAGS += -DCAR_MERGE=$(CAR_MERGE)
endif
# make LOADER_COL_FIRST_MS=0: any queued collision loads first, as before (world.c loader_col_first_ms), the A/B
ifdef LOADER_COL_FIRST_MS
CFLAGS += -DLOADER_COL_FIRST_MS=$(LOADER_COL_FIRST_MS)
endif
# make HOLE_LOG=1: each hole in view and each load, hole-log.json at the test's end (world_hole_dump)
ifdef HOLE_LOG
CFLAGS += -DHOLE_LOG=$(HOLE_LOG)
endif
# make WORLD_IO_SEEK_US=4000 WORLD_IO_KB_US=90: the city's reads as slow as
# the user's microSD in the emulator (world.c read_into: a command each
# piece, then by the KB); a measurement build, rebuild world.o clean
# make MS_EMU=1: every read and open as slow as the user's Memory Stick,
# one at a time for all threads (main.c, the wrapped sceIoRead and
# sceIoOpen); a measurement build for the PPSSPP, rebuild main.o clean
# make POWER_LOG=1: each step of a suspend and its resume in power-log.txt beside the game (powerlog.c), a test build for the player's own PSP
ifdef MUS_AHEAD
CFLAGS += -DMUS_AHEAD=$(MUS_AHEAD)   # 0: the music's file in one buffer, no piece read ahead (audio.c; an A/B)
endif
# make SLEEP_RESERVE_KB=0: no sleep reserve (volmem.c; the suspend held off on every model, as before), for A/B
ifdef SLEEP_RESERVE_KB
CFLAGS += -DSLEEP_RESERVE_KB=$(SLEEP_RESERVE_KB)
endif
ifdef POWER_LOG
CFLAGS += -DPOWER_LOG=$(POWER_LOG)
endif
ifdef MS_EMU
CFLAGS += -DMS_EMU=$(MS_EMU)
endif
# make MS_DIR_SLACK_KB=1150: MS_EMU's folder with the deleted files' entries (main.c ms_dir_slack_kb)
ifdef MS_DIR_SLACK_KB
CFLAGS += -DMS_DIR_SLACK_KB=$(MS_DIR_SLACK_KB)
endif
# make PACK_ON=0: pack.n2pk not used (main.c pack_load): the A/B
ifdef PACK_ON
CFLAGS += -DPACK_ON=$(PACK_ON)
endif
# make CARD_DIR_ON=0: no table of the card's names (main.c card_dir_load), every open walks its folder: the A/B
ifdef CARD_DIR_ON
CFLAGS += -DCARD_DIR_ON=$(CARD_DIR_ON)
endif
# make WORLD_LEAD_MS=... WORLD_LEAD_M=...: the streaming's lead point farther (world.c), an A/B build
# make WORLD_CORRIDOR_M=450 WORLD_CORRIDOR_DRAW=0: the course ahead loaded farther, not drawn farther (world.c)
ifdef WORLD_CORRIDOR_M
CFLAGS += -DWORLD_CORRIDOR_M=$(WORLD_CORRIDOR_M)
endif
ifdef WORLD_CORRIDOR_DRAW
CFLAGS += -DWORLD_CORRIDOR_DRAW=$(WORLD_CORRIDOR_DRAW)
endif
ifdef WORLD_LEAD_MS
CFLAGS += -DWORLD_LEAD_MS=$(WORLD_LEAD_MS)
endif
ifdef WORLD_LEAD_M
CFLAGS += -DWORLD_LEAD_M=$(WORLD_LEAD_M)
endif
# make RACE_PREFETCH_NOW=...: m of the race's course loaded on its loading screen, the rest through the intro and countdown (main.c race_prefetch)
ifdef STREET_HOLD
CFLAGS += -DSTREET_HOLD=$(STREET_HOLD)
endif
ifdef FOPEN_WHOLE
CFLAGS += -DFOPEN_WHOLE=$(FOPEN_WHOLE)
endif
ifdef RACE_SETTLE_SHOWN
CFLAGS += -DRACE_SETTLE_SHOWN=$(RACE_SETTLE_SHOWN)
endif
ifdef RACE_PREFETCH_NOW
CFLAGS += -DRACE_PREFETCH_NOW=$(RACE_PREFETCH_NOW)
endif
ifdef WORLD_IO_SEEK_US
CFLAGS += -DWORLD_IO_SEEK_US=$(WORLD_IO_SEEK_US)
endif
ifdef WORLD_IO_KB_US
CFLAGS += -DWORLD_IO_KB_US=$(WORLD_IO_KB_US)
endif
# Each function in its own section, and the street frame's hot ones first in
# .text in a fixed order (hot.ld, written by tools/hot_order.py from a
# profile): code added anywhere else no longer moves them, so they keep their
# lines of the 16 KB instruction cache (OTIMIZACAO.md, "Ordem fixa do código
# quente"). LDFLAGS_ORDER= leaves the linker's file order (an A/B).
CFLAGS += -ffunction-sections
LDFLAGS_ORDER ?= -Wl,--section-ordering-file=hot.ld
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
# every sceGuDrawArray (gum's too), sceGuCallList and sceGuGetMemory through gudraw.c (fewer kernel calls);
# gum on the VFPU: its rotations ran libm's sinf/cosf (~0.7 us each); zlib: the vinyls' catalogue
# sceIoRead/Open/Lseek counted (main.c: telemetry "io", boot.txt), and fopen for reading with a 32 KB buffer
LDFLAGS = -Wl,--wrap=sceDisplayWaitVblankStart -Wl,--wrap=sceDisplayWaitVblankStartCB -Wl,--wrap=sceGuDrawArray -Wl,--wrap=sceGuCallList -Wl,--wrap=sceGuGetMemory -Wl,--wrap=sceGuStart -Wl,--wrap=sceGeListUpdateStallAddr -Wl,--wrap=sceIoRead -Wl,--wrap=sceIoOpen -Wl,--wrap=sceIoLseek -Wl,--wrap=sceIoLseek32 -Wl,--wrap=sceIoClose -Wl,--wrap=fopen -Wl,--wrap=fseek
# every thread started through fpu_guard.c: the FPU's exceptions off in each
LDFLAGS += -Wl,--wrap=sceKernelCreateThread
# libgcc's 64-bit divisions through div0_guard.c: a zero divisor counted, no trap
ifneq ($(DIV0),trap)
LDFLAGS += -Wl,--wrap=__divdi3 -Wl,--wrap=__udivdi3 -Wl,--wrap=__moddi3 -Wl,--wrap=__umoddi3
endif
# Globals that PSPLink pokes while the game runs (tools/psp_prof.py --poke,
# psp_hw.sh volume): kept as they are, or LTO folds the ones the code never
# writes into constants and drops them. Besides these, every "int x=...;"
# whose comment says "poked".
POKED = probe_ctl audio_out_volume wet_col car_lods car_merge $(shell sed -n 's/^int \([A-Za-z_0-9]*\)=.*poked.*/\1/p' $(OBJS:.o=.c))
ifeq ($(RELEASE),1)
POKED =   # (nothing pokes a player's game: LTO may fold them)
endif
comma := ,
LDFLAGS += $(addprefix -Wl$(comma)-u$(comma),$(POKED)) $(LDFLAGS_ORDER)
# the link's map (nfsu2-viewer.map): where each input section went, for tools/icache_order.py
LDFLAGS += -Wl,-Map=$(TARGET).map
ifeq ($(FREE_CHECK),1)
LDFLAGS += -Wl,--wrap=free
endif
ifeq ($(MALLOC_POISON),1)
LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=memalign -Wl,--wrap=realloc
endif
# make IO_FAULT=500 (a test build only): one read in 500 fails or comes back short (main.c __wrap_sceIoRead)
ifdef IO_FAULT
CFLAGS += -DIO_FAULT=$(IO_FAULT)
endif
CFLAGS += $(ALLOC_FLAGS)
# make FORCE_HEAP_KB=19611: the PSP-1000's heap (20.08 MB in the PPSSPP's
# PSPModel 0) on a later model, a block held from boot on: the compact
# profile measured on the PSP-3000 (main.c memory_profile_boot)
ifdef FORCE_HEAP_KB
CFLAGS += -DFORCE_HEAP_KB=$(FORCE_HEAP_KB)
endif
LIBS = -lz -lpspgum_vfpu -lpspvfpu -lpspgu -lpsppower -lpspaudio -lpspmp3 -lpsputility -lpspjpeg -lpspmpeg -lpspmpegbase -lm
ifdef POWER_LOG
LIBS += -lpspkubridge   # (POWER_LOG: the memory protection read through ARK's KUBridge)
endif
BUILD_PRX = 1
# One EBOOT requests extra USER RAM where the loader supports it. Runtime
# AUTO still falls back conservatively; MEMSIZE is not a capacity measurement.
PSP_LARGE_MEMORY ?= 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = NFSU2 PSP v$(GAME_VERSION)
SFOFLAGS = -s DISC_VERSION=$(call SFO_VERSION,%d.%d%d) -s APP_VER=$(call SFO_VERSION,%02d.%d%d)
# The XMB's icon and background (tools/convert_hud.py eboot_art, from the
# game's loading screen) once converted
ART = ../dist/PSP/GAME/NFSU2VIEW
ifneq ($(wildcard $(ART)/icon0.png),)
PSP_EBOOT_ICON = $(ART)/icon0.png
endif
ifneq ($(wildcard $(ART)/pic1.png),)
PSP_EBOOT_PIC1 = $(ART)/pic1.png
endif
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
-include $(OBJS:.o=.d)

LOCAL_HEADERS := fe_hydraulics_table.h og_drift_values.h original_tables.h original_misc.h
LOCAL_HEADERS += steering_tables.h career_camera.h garage_tables.h terrain_noise.h impact_tables.h
ifeq ($(ORIGINAL_PATH_CHOICE),1)
LOCAL_HEADERS += parkade4306_futurepoint_authoring.h parkade4306_line_identity.h
endif
$(OBJS): | $(addprefix local-data/,$(LOCAL_HEADERS))
local-data/%:
	@printf '%s\n' 'Missing $@. See docs/GAME_SOURCE.md for local build data.' >&2
	@exit 1
# Other flags (LTO=0, PGO=...) rebuild everything: .cflags holds the last
# ones (the objects removed, not dated: make 3.81 sees whole seconds)
# PSP_LARGE_MEMORY too: it is written in PARAM.SFO (MEMSIZE), and a kept
# PARAM.SFO left every "compact" EBOOT with MEMSIZE=1 (14/10)
FLAGS_NOW := $(CFLAGS) $(LDFLAGS) LARGE_MEMORY=$(PSP_LARGE_MEMORY)
ifneq ($(FLAGS_NOW),$(shell cat .cflags 2>/dev/null))
$(shell rm -f $(OBJS) $(TARGET).elf PARAM.SFO EBOOT.PBP; echo '$(FLAGS_NOW)' > .cflags)
endif
# Another version only remakes PARAM.SFO (and build_id.o, made every time)
SFO_NOW := $(PSP_EBOOT_TITLE) $(SFOFLAGS)
ifneq ($(SFO_NOW),$(shell cat .sfo 2>/dev/null))
$(shell rm -f PARAM.SFO EBOOT.PBP; echo '$(SFO_NOW)' > .sfo)
endif
# a new icon or background repacks the EBOOT
EBOOT.PBP: $(filter-out NULL,$(PSP_EBOOT_ICON) $(PSP_EBOOT_PIC1))
# The build marker identifies the public source checkout.
BUILD_ID ?= $(shell git -C .. rev-parse --short HEAD 2>/dev/null || printf '%s' nogit)
build_id.o: CFLAGS += -DBUILD_ID=\"$(BUILD_ID)\" -DGAME_VERSION=\"$(GAME_VERSION)\"
build_id.o: FORCE
FORCE:
.PHONY: FORCE

# Diagnostic pilot only: refuse packaging unless this exact ELF passes. The
# check holds the plain build it was made for: a profile's build (PGO=gen
# counts every branch, PGO=use and LTO lay the allocator's code out anew)
# and the test builds (FREE_CHECK, MALLOC_POISON) change that code by design and are not
# the diagnostic's; the release's PGO/LTO build was refused, 21/10.
ABI_CHECKED := $(if $(filter gen use,$(PGO))$(filter 1,$(LTO))$(filter 1,$(FREE_CHECK))$(filter 1,$(MALLOC_POISON))$(IO_FAULT),,1)
.PHONY: allocator-diag-abi-check
ifeq ($(ABI_CHECKED),1)
EBOOT.PBP: allocator-diag-abi-check
endif
allocator-diag-abi-check: $(TARGET).elf
	python3 ../tools/allocator_diag_abi.py --elf $(TARGET).elf --reference ../tools/allocator_diag_abi_reference.json --output allocator-diag-abi.json
# DIV0=diag: GCC links -lubsan with -fsanitize; the PSP has none, and its
# one handler is div0_guard.c's. An empty archive under that name.
ifeq ($(DIV0),diag)
LDFLAGS += -L$(CURDIR)/.div0diag
$(TARGET).elf: .div0diag/libubsan.a
.div0diag/libubsan.a:
	mkdir -p .div0diag && rm -f $@ && psp-ar rcs $@
endif
