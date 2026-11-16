/* The race's code first in .text, in a fixed order: the libraries the frame calls, then
 * the sections that run in a race, in the order an I-cache replay of the race's block
 * traces chose (tools/icache_order.py; 16 KB, 2-way, 64 B lines, as the calibrated
 * PPSSPP), each on the cache sets it had there. Generated: redo with the tool. */
.text : {
  *libpspgu.a:*(.text .text.*)
  *libpspgum_vfpu.a:*(.text .text.*)
  *libpspvfpu.a:*(.text .text.*)
  *libm.a:*(.text .text.*)
  *libgcc.a:*(.text .text.*)
  *libc.a:lib_a-memcpy.o(.text .text.*)
  *libc.a:lib_a-memset.o(.text .text.*)
  *libc.a:lib_a-memmove.o(.text .text.*)
  *libc.a:lib_a-memcmp.o(.text .text.*)
  *libc.a:lib_a-strlen.o(.text .text.*)
  *libc.a:lib_a-strcmp.o(.text .text.*)
  /* the race's sections */
  . = . + ((0x0f94 - .) & 0x1fff);
  *(.text.__wrap_fopen)
  . = . + ((0x130c - .) & 0x1fff);
  *(.text.hot.build_night_cube.lto_priv.0)
  . = . + ((0x1a80 - .) & 0x1fff);
  *libcglue.a:__set_errno.o(.text)
  . = . + ((0x1ac4 - .) & 0x1fff);
  *(.text.world_overlay_free)
  . = . + ((0x1d28 - .) & 0x1fff);
  *(.text.mus_close.lto_priv.0)
  . = . + ((0x1f58 - .) & 0x1fff);
  *(.text.fit.isra.0)
  . = . + ((0x00d4 - .) & 0x1fff);
  *(.text.car_phys)
  . = . + ((0x0164 - .) & 0x1fff);
  *(.text.moment_trigger)
  . = . + ((0x0330 - .) & 0x1fff);
  *libc.a:libc_a-syslseek.o(.text)
  . = . + ((0x034c - .) & 0x1fff);
  *(.text.hot.line_sm_y)
  . = . + ((0x041c - .) & 0x1fff);
  *(.text.lanes_load.constprop.0.isra.0)
  . = . + ((0x0a00 - .) & 0x1fff);
  *(.text.tip_preload.lto_priv.0)
  . = . + ((0x0ac8 - .) & 0x1fff);
  *(.text.set_mode.lto_priv.0)
  . = . + ((0x10a4 - .) & 0x1fff);
  *(.text.perf_profile)
  . = . + ((0x1200 - .) & 0x1fff);
  *libc.a:libc_a-getenv_r.o(.text)
  . = . + ((0x1354 - .) & 0x1fff);
  *(.text.input.lto_priv.0)
  . = . + ((0x148c - .) & 0x1fff);
  *(.text.fx_draw.part.0)
  . = . + ((0x0088 - .) & 0x1fff);
  *libcglue.a:_stat.o(.text)
  . = . + ((0x0278 - .) & 0x1fff);
  *libc.a:libc_a-rand.o(.text)
  . = . + ((0x02ec - .) & 0x1fff);
  *(.text.names_of)
  . = . + ((0x0648 - .) & 0x1fff);
  *(.text.zones_reset)
  . = . + ((0x073c - .) & 0x1fff);
  *(.text.career_owns_suv)
  . = . + ((0x07dc - .) & 0x1fff);
  *libc.a:libc_a-openr.o(.text)
  . = . + ((0x0848 - .) & 0x1fff);
  *(.text.look_of.lto_priv.0)
  . = . + ((0x0a60 - .) & 0x1fff);
  *(.text.draw_mesh_range.lto_priv.0)
  . = . + ((0x148c - .) & 0x1fff);
  *(.text.hot.col_info_ij)
  . = . + ((0x1788 - .) & 0x1fff);
  *(.text.career_new.lto_priv.0)
  . = . + ((0x188c - .) & 0x1fff);
  *libc.a:libc_a-qsort.o(.text)
  . = . + ((0x0248 - .) & 0x1fff);
  *(.text.bank.constprop.0)
  . = . + ((0x02a8 - .) & 0x1fff);
  *(.text.perf_load.constprop.0.isra.0)
  . = . + ((0x08c4 - .) & 0x1fff);
  *(.text.career_needs_sponsor)
  . = . + ((0x0910 - .) & 0x1fff);
  *(.text.hot.pmap_set)
  . = . + ((0x0bf8 - .) & 0x1fff);
  *(.text.hot.gps_update)
  . = . + ((0x11d4 - .) & 0x1fff);
  *(.text.skids_init)
  . = . + ((0x14a0 - .) & 0x1fff);
  *(.text.hot.collision_known_under.lto_priv.0)
  . = . + ((0x1644 - .) & 0x1fff);
  *(.text.people_free)
  . = . + ((0x1728 - .) & 0x1fff);
  *(.text.player_phys.lto_priv.0)
  . = . + ((0x1d54 - .) & 0x1fff);
  *libcglue.a:_lseek.o(.text)
  . = . + ((0x1ea4 - .) & 0x1fff);
  *(.text.traffic_load.lto_priv.0)
  . = . + ((0x0060 - .) & 0x1fff);
  *(.text.loading_progress_phase)
  . = . + ((0x00d0 - .) & 0x1fff);
  *(.text.boot_drive_pad.lto_priv.0)
  . = . + ((0x1fbc - .) & 0x1fff);
  *(.text.intro_cancel_prepared)
  . = . + ((0x1ffc - .) & 0x1fff);
  *(.text.gps_to_cat.lto_priv.0)
  . = . + ((0x0484 - .) & 0x1fff);
  *(.text.motion_blur.lto_priv.0)
  . = . + ((0x11d8 - .) & 0x1fff);
  *(.text.car_nos_push)
  . = . + ((0x12a8 - .) & 0x1fff);
  *(.text.hot.avg5.lto_priv.0)
  . = . + ((0x1338 - .) & 0x1fff);
  *(.text.fe_free)
  . = . + ((0x1350 - .) & 0x1fff);
  *(.text.hot.perf_apply)
  . = . + ((0x1cf4 - .) & 0x1fff);
  *(.text.loadctl_open.lto_priv.0)
  . = . + ((0x1f18 - .) & 0x1fff);
  *(.text.xing_clear.lto_priv.0)
  . = . + ((0x1f78 - .) & 0x1fff);
  *(.text.profile_transition_mark.lto_priv.0)
  . = . + ((0x02f4 - .) & 0x1fff);
  *(.text.sr_start.lto_priv.0)
  . = . + ((0x03c0 - .) & 0x1fff);
  *(.text.hot.edge_cmp)
  . = . + ((0x03dc - .) & 0x1fff);
  *(.text.lod_strips)
  . = . + ((0x06c4 - .) & 0x1fff);
  *(.text.hot.cell_ij.lto_priv.0)
  . = . + ((0x0868 - .) & 0x1fff);
  *(.text.original_owned.lto_priv.0)
  . = . + ((0x0898 - .) & 0x1fff);
  *(.text.loading_ready.lto_priv.0)
  . = . + ((0x094c - .) & 0x1fff);
  *(.text.car_class.lto_priv.0)
  . = . + ((0x0ce4 - .) & 0x1fff);
  *(.text.bars_load.constprop.0.isra.0)
  . = . + ((0x0ee8 - .) & 0x1fff);
  *(.text.roads_links)
  . = . + ((0x0f18 - .) & 0x1fff);
  *(.text.hot.gather_cars.lto_priv.0)
  . = . + ((0x1280 - .) & 0x1fff);
  *(.text.world_terrain)
  . = . + ((0x1454 - .) & 0x1fff);
  *(.text.load_player.lto_priv.0)
  . = . + ((0x1524 - .) & 0x1fff);
  *(.text.audio_aems_load)
  . = . + ((0x1f08 - .) & 0x1fff);
  *(.text.repro_step.lto_priv.0)
  . = . + ((0x0388 - .) & 0x1fff);
  *(.text.tach_style)
  . = . + ((0x0490 - .) & 0x1fff);
  *(.text.scene_input.lto_priv.0)
  . = . + ((0x04f4 - .) & 0x1fff);
  *(.text.hot.world_sphere_outside)
  . = . + ((0x0660 - .) & 0x1fff);
  *(.text.nis_play_car)
  . = . + ((0x085c - .) & 0x1fff);
  *(.text.race_place_at.lto_priv.0)
  . = . + ((0x0a18 - .) & 0x1fff);
  *(.text.unload_part.lto_priv.0)
  . = . + ((0x1544 - .) & 0x1fff);
  *(.text.lights.lto_priv.0)
  . = . + ((0x1704 - .) & 0x1fff);
  *(.text.hot.world_ground_kind)
  . = . + ((0x0034 - .) & 0x1fff);
  *(.text.hot.car_roll_wheels)
  . = . + ((0x01c0 - .) & 0x1fff);
  *(.text.sms_send.lto_priv.0)
  . = . + ((0x0248 - .) & 0x1fff);
  *(.text.hot.in_corr.lto_priv.0)
  . = . + ((0x02b4 - .) & 0x1fff);
  *(.text.hot.sr_step)
  . = . + ((0x05b0 - .) & 0x1fff);
  *(.text.hot.tat_time.lto_priv.0)
  . = . + ((0x0630 - .) & 0x1fff);
  *libz.a:uncompr.c.o(.text)
  . = . + ((0x080c - .) & 0x1fff);
  *(.text.race_timer_text.lto_priv.0)
  . = . + ((0x0994 - .) & 0x1fff);
  *(.text.world_draw_landmarks)
  . = . + ((0x0a48 - .) & 0x1fff);
  *(.text.psteer_step)
  . = . + ((0x11c4 - .) & 0x1fff);
  *(.text.hot.release_texture.isra.0)
  . = . + ((0x1334 - .) & 0x1fff);
  *(.text.hot.flat_screen.lto_priv.0)
  . = . + ((0x15d4 - .) & 0x1fff);
  *libcglue.a:_open.o(.text)
  . = . + ((0x1748 - .) & 0x1fff);
  *(.text.hot.nfsu2_floorf.lto_priv.0.lto_priv.0)
  . = . + ((0x17c0 - .) & 0x1fff);
  *(.text.world_preread_wait)
  . = . + ((0x1828 - .) & 0x1fff);
  *(.text.path_driver_start)
  . = . + ((0x18a0 - .) & 0x1fff);
  *(.text.line_edge)
  . = . + ((0x1948 - .) & 0x1fff);
  *(.text.hot.fe_stamp_turned.constprop.0)
  . = . + ((0x1cec - .) & 0x1fff);
  *(.text.glyph_adv.lto_priv.0)
  . = . + ((0x1f30 - .) & 0x1fff);
  *(.text.add_point.lto_priv.0)
  . = . + ((0x0198 - .) & 0x1fff);
  *(.text.hot.mesh_draw.lto_priv.0)
  . = . + ((0x01cc - .) & 0x1fff);
  *libc.a:libc_a-ssprint_r.o(.text)
  . = . + ((0x0294 - .) & 0x1fff);
  *(.text.presentation_flip_queue.lto_priv.0)
  . = . + ((0x0354 - .) & 0x1fff);
  *libc.a:libc_a-localeconv.o(.text)
  . = . + ((0x0374 - .) & 0x1fff);
  *(.text.probe_host)
  . = . + ((0x03fc - .) & 0x1fff);
  *(.text.world_prefetch)
  . = . + ((0x06e0 - .) & 0x1fff);
  *(.text.reverse_drive_motion_step.part.0.constprop.0)
  . = . + ((0x1550 - .) & 0x1fff);
  *libcglue.a:__libpthreadglue_init.o(.text)
  . = . + ((0x1558 - .) & 0x1fff);
  *(.text.opts_load.lto_priv.0)
  . = . + ((0x1a54 - .) & 0x1fff);
  *(.text.hot.seg_off.lto_priv.0)
  . = . + ((0x1c50 - .) & 0x1fff);
  *libc.a:libc_a-sscanf.o(.text)
  . = . + ((0x1d68 - .) & 0x1fff);
  *(.text.hot.gap_line.lto_priv.0)
  . = . + ((0x0410 - .) & 0x1fff);
  *(.text.fix_scaled.lto_priv.0)
  . = . + ((0x058c - .) & 0x1fff);
  *(.text.rd32)
  . = . + ((0x05ac - .) & 0x1fff);
  *(.text.hot.update_sphere.lto_priv.0)
  . = . + ((0x17fc - .) & 0x1fff);
  *(.text.hot.rival_sound.lto_priv.0)
  . = . + ((0x0b4c - .) & 0x1fff);
  *(.text.hot.line_probe)
  . = . + ((0x1334 - .) & 0x1fff);
  *libc.a:libc_a-mallinfor.o(.text)
  . = . + ((0x1440 - .) & 0x1fff);
  *(.text.hot.gap_push.lto_priv.0)
  . = . + ((0x1a20 - .) & 0x1fff);
  *(.text.hot.line_track.lto_priv.0)
  . = . + ((0x0b40 - .) & 0x1fff);
  *libc.a:libc_a-refill.o(.text)
  . = . + ((0x0d28 - .) & 0x1fff);
  *libcglue.a:__init_mutex.o(.text)
  . = . + ((0x0d78 - .) & 0x1fff);
  *(.text.garage_rating_ensure.isra.0)
  . = . + ((0x0db0 - .) & 0x1fff);
  *libz.a:zutil.c.o(.text)
  . = . + ((0x0e10 - .) & 0x1fff);
  *(.text.rewind_add_packed)
  . = . + ((0x0e90 - .) & 0x1fff);
  *(.text.hot.advance.part.0.lto_priv.0)
  . = . + ((0x1078 - .) & 0x1fff);
  *libc.a:libc_a-readr.o(.text)
  . = . + ((0x10e4 - .) & 0x1fff);
  *(.text.hot.sphere_publish)
  . = . + ((0x17d8 - .) & 0x1fff);
  *(.text.audio_music_diag)
  . = . + ((0x1a30 - .) & 0x1fff);
  *(.text.memory_profile_admit_optional)
  . = . + ((0x1aa4 - .) & 0x1fff);
  *(.text.hot.city_free.lto_priv.0)
  . = . + ((0x1c14 - .) & 0x1fff);
  *(.text.pthread_init)
  . = . + ((0x1e00 - .) & 0x1fff);
  *(.text.career_finish_result)
  . = . + ((0x033c - .) & 0x1fff);
  *(.text.graph.lto_priv.0)
  . = . + ((0x0458 - .) & 0x1fff);
  *(.text.hot.pmap_blend)
  . = . + ((0x06c0 - .) & 0x1fff);
  *(.text.hot.wheels_of.lto_priv.0)
  . = . + ((0x0c78 - .) & 0x1fff);
  *(.text.hot.worldanim_draw_far)
  . = . + ((0x1234 - .) & 0x1fff);
  *(.text.hot.draft_factor)
  . = . + ((0x1620 - .) & 0x1fff);
  *(.text.boot_mark.lto_priv.0)
  . = . + ((0x1748 - .) & 0x1fff);
  *(.text.mus_switch)
  . = . + ((0x17f4 - .) & 0x1fff);
  *libpthreadglue.a:__pteTlsAlloc.o(.text)
  . = . + ((0x18a4 - .) & 0x1fff);
  *(.text.street_hold.lto_priv.0)
  . = . + ((0x1d94 - .) & 0x1fff);
  *(.text.hot.car_shadow.lto_priv.0)
  . = . + ((0x04f0 - .) & 0x1fff);
  *(.text.career_must_pick_car)
  . = . + ((0x0768 - .) & 0x1fff);
  *(.text.hot.normalize.lto_priv.0)
  . = . + ((0x0804 - .) & 0x1fff);
  *(.text.race_start.lto_priv.0)
  . = . + ((0x11a0 - .) & 0x1fff);
  *(.text.hot.io_stale.lto_priv.0)
  . = . + ((0x1200 - .) & 0x1fff);
  *(.text.hot.line_point.lto_priv.0)
  . = . + ((0x1b08 - .) & 0x1fff);
  *(.text.hot.swap_buffers)
  . = . + ((0x1d70 - .) & 0x1fff);
  *(.text.hot.race_line_speed)
  . = . + ((0x01ec - .) & 0x1fff);
  *(.text.perf_stock)
  . = . + ((0x0218 - .) & 0x1fff);
  *(.text.hot.heap_up)
  . = . + ((0x034c - .) & 0x1fff);
  *(.text.blur_update.lto_priv.0)
  . = . + ((0x0658 - .) & 0x1fff);
  *(.text.audio_speech_match)
  . = . + ((0x0a18 - .) & 0x1fff);
  *(.text.overflow.lto_priv.0)
  . = . + ((0x0aa0 - .) & 0x1fff);
  *(.text.hot.defer_signal.lto_priv.0)
  . = . + ((0x0b04 - .) & 0x1fff);
  *(.text.body_extent.lto_priv.0)
  . = . + ((0x0d10 - .) & 0x1fff);
  *(.text.hot.course_track)
  . = . + ((0x16c0 - .) & 0x1fff);
  *(.text.hot.flares_course.constprop.0)
  . = . + ((0x1c38 - .) & 0x1fff);
  *(.text.each_marker.constprop.0)
  . = . + ((0x1da0 - .) & 0x1fff);
  *libc.a:libc_a-strnlen.o(.text)
  . = . + ((0x1dd8 - .) & 0x1fff);
  *(.text.asin_binary.lto_priv.0)
  . = . + ((0x0138 - .) & 0x1fff);
  *(.text.loading_start.lto_priv.0)
  . = . + ((0x03ec - .) & 0x1fff);
  *(.text.hot.car_state_sane)
  . = . + ((0x04e0 - .) & 0x1fff);
  *(.text.text_icons.part.0.lto_priv.0)
  . = . + ((0x0ca0 - .) & 0x1fff);
  *(.text.hot.course_pose)
  . = . + ((0x1094 - .) & 0x1fff);
  *(.text.hot.career_show_open)
  . = . + ((0x124c - .) & 0x1fff);
  *(.text.driveby_reset)
  . = . + ((0x1310 - .) & 0x1fff);
  *libc.a:libc_a-snprintf.o(.text)
  . = . + ((0x1478 - .) & 0x1fff);
  *libz.a:inftrees.c.o(.text)
  . = . + ((0x00d4 - .) & 0x1fff);
  *(.text.engsnd_real_rpm)
  . = . + ((0x0114 - .) & 0x1fff);
  *(.text.fe_load_room.lto_priv.0)
  . = . + ((0x0280 - .) & 0x1fff);
  *(.text.io_abs.constprop.0)
  . = . + ((0x02e0 - .) & 0x1fff);
  *(.text.car_set_phys)
  . = . + ((0x0314 - .) & 0x1fff);
  *(.text.hot.lit_walk.constprop.0)
  . = . + ((0x168c - .) & 0x1fff);
  *(.text.hot.line_driver.lto_priv.0)
  . = . + ((0x1ec8 - .) & 0x1fff);
  *(.text.flares_init.constprop.0.isra.0)
  . = . + ((0x0058 - .) & 0x1fff);
  *(.text.original_release.lto_priv.0)
  . = . + ((0x0128 - .) & 0x1fff);
  *(.text.race_debrief_ensure.lto_priv.0)
  . = . + ((0x0420 - .) & 0x1fff);
  *(.text.callback_thread.lto_priv.0)
  . = . + ((0x04ec - .) & 0x1fff);
  *(.text.style_update.lto_priv.0)
  . = . + ((0x1080 - .) & 0x1fff);
  *libc.a:libc_a-mstats.o(.text)
  . = . + ((0x1140 - .) & 0x1fff);
  *(.text.skids_clear)
  . = . + ((0x11cc - .) & 0x1fff);
  *(.text.hot.path_driver_control)
  . = . + ((0x1eb0 - .) & 0x1fff);
  *(.text.build.lto_priv.0)
  . = . + ((0x10f0 - .) & 0x1fff);
  *(.text.lamps_read.part.0.lto_priv.0)
  . = . + ((0x13a4 - .) & 0x1fff);
  *(.text.label_draw.constprop.0)
  . = . + ((0x149c - .) & 0x1fff);
  *(.text.drive.lto_priv.0)
  . = . + ((0x1724 - .) & 0x1fff);
  *(.text.unlink_free.lto_priv.0)
  . = . + ((0x175c - .) & 0x1fff);
  *(.text.moment_abort)
  . = . + ((0x17dc - .) & 0x1fff);
  *(.text.hot.node_dist.lto_priv.0)
  . = . + ((0x1844 - .) & 0x1fff);
  *(.text.hot.project.lto_priv.0)
  . = . + ((0x1b54 - .) & 0x1fff);
  *(.text.hot.path_driver_fork_progress)
  . = . + ((0x1c74 - .) & 0x1fff);
  *(.text.pan_of.lto_priv.0)
  . = . + ((0x1d44 - .) & 0x1fff);
  *(.text.headlight_uv.lto_priv.0)
  . = . + ((0x1e24 - .) & 0x1fff);
  *(.text.hot.gather.constprop.0)
  . = . + ((0x04e4 - .) & 0x1fff);
  *(.text.hot.predict_hit.constprop.0)
  . = . + ((0x0efc - .) & 0x1fff);
  *(.text.nis_play_update)
  . = . + ((0x11c0 - .) & 0x1fff);
  *(.text.state_on.lto_priv.0)
  . = . + ((0x1248 - .) & 0x1fff);
  *(.text.hot.cache_frame.lto_priv.0)
  . = . + ((0x15d8 - .) & 0x1fff);
  *(.text.zone_clear)
  . = . + ((0x161c - .) & 0x1fff);
  *(.text.hot.scroll_of.lto_priv.0)
  . = . + ((0x1784 - .) & 0x1fff);
  *libcglue.a:__fdman_get_new_descriptor.o(.text)
  . = . + ((0x1858 - .) & 0x1fff);
  *(.text.hot.body_gap.lto_priv.0)
  . = . + ((0x1a0c - .) & 0x1fff);
  *libcglue.a:_write.o(.text)
  . = . + ((0x1adc - .) & 0x1fff);
  *(.text.engine_update.lto_priv.0)
  . = . + ((0x181c - .) & 0x1fff);
  *(.text.loading_stop.lto_priv.0)
  . = . + ((0x1aa0 - .) & 0x1fff);
  *(.text.td_load)
  . = . + ((0x1e70 - .) & 0x1fff);
  *(.text.stage_won.lto_priv.0)
  . = . + ((0x1fc0 - .) & 0x1fff);
  *(.text.audio_sfx)
  . = . + ((0x0040 - .) & 0x1fff);
  *(.text.sr_peek.lto_priv.0)
  . = . + ((0x0148 - .) & 0x1fff);
  *(.text.opts_apply.lto_priv.0)
  . = . + ((0x02b8 - .) & 0x1fff);
  *(.text.hot.world_solid_kind_bit)
  . = . + ((0x0a28 - .) & 0x1fff);
  *(.text.hot.stream_predict_road)
  . = . + ((0x0f68 - .) & 0x1fff);
  *(.text.repro_marker.lto_priv.0)
  . = . + ((0x1304 - .) & 0x1fff);
  *(.text.race_intro_finish_loading.lto_priv.0)
  . = . + ((0x13f8 - .) & 0x1fff);
  *(.text.hot.budget.lto_priv.0)
  . = . + ((0x143c - .) & 0x1fff);
  *(.text.io_pending_begin)
  . = . + ((0x1554 - .) & 0x1fff);
  *(.text.world_lit_stats_write)
  . = . + ((0x1760 - .) & 0x1fff);
  *(.text.hot.traffic_sound.lto_priv.0)
  . = . + ((0x0b88 - .) & 0x1fff);
  *(.text.rewind_frame.lto_priv.0)
  . = . + ((0x19e8 - .) & 0x1fff);
  *libpthreadglue.a:pte_osTlsAlloc.o(.text)
  . = . + ((0x1a18 - .) & 0x1fff);
  *(.text.hot.presentation_swap_runahead.lto_priv.0)
  . = . + ((0x1b40 - .) & 0x1fff);
  *(.text.hot.claim.lto_priv.0)
  . = . + ((0x1dd0 - .) & 0x1fff);
  *(.text.set_state.lto_priv.0)
  . = . + ((0x1e20 - .) & 0x1fff);
  *(.text.roads_free)
  . = . + ((0x1ed4 - .) & 0x1fff);
  *(.text.hot.ima_block.lto_priv.0)
  . = . + ((0x003c - .) & 0x1fff);
  *libz.a:adler32.c.o(.text)
  . = . + ((0x07f0 - .) & 0x1fff);
  *(.text.table_at.lto_priv.0)
  . = . + ((0x0890 - .) & 0x1fff);
  *(.text.hot.frames_now.lto_priv.0)
  . = . + ((0x0a30 - .) & 0x1fff);
  *libc.a:libc_a-calloc.o(.text)
  . = . + ((0x0a48 - .) & 0x1fff);
  *(.text.trax_card.lto_priv.0)
  . = . + ((0x0a68 - .) & 0x1fff);
  *(.text.loading_phase_end.lto_priv.0)
  . = . + ((0x0ac8 - .) & 0x1fff);
  *(.text.hot.__wrap_sceIoLseek32)
  . = . + ((0x0b90 - .) & 0x1fff);
  *(.text.free_state.lto_priv.0)
  . = . + ((0x0c5c - .) & 0x1fff);
  *(.text.hot.arch_evict.lto_priv.0)
  . = . + ((0x0cec - .) & 0x1fff);
  *(.text.zone_end.lto_priv.0)
  . = . + ((0x0dc0 - .) & 0x1fff);
  *(.text.hot.path_project.lto_priv.0)
  . = . + ((0x1010 - .) & 0x1fff);
  *libc.a:libc_a-fclose.o(.text)
  . = . + ((0x11b0 - .) & 0x1fff);
  *(.text.frame_halve)
  . = . + ((0x1494 - .) & 0x1fff);
  *(.text.traffic_set_model.lto_priv.0)
  . = . + ((0x1560 - .) & 0x1fff);
  *(.text.model_lamps.lto_priv.0)
  . = . + ((0x16d0 - .) & 0x1fff);
  *(.text.vol_frame.lto_priv.0)
  . = . + ((0x18f0 - .) & 0x1fff);
  *(.text.hot.draw_shine.lto_priv.0)
  . = . + ((0x1d04 - .) & 0x1fff);
  *(.text.hot.shade_paint.lto_priv.0)
  . = . + ((0x0c04 - .) & 0x1fff);
  *libpthreadglue.a:pteTlsThreadInit.o(.text)
  . = . + ((0x0c50 - .) & 0x1fff);
  *(.text.audio_nis_play.isra.0)
  . = . + ((0x0d8c - .) & 0x1fff);
  *(.text.hot.car_transform.lto_priv.0)
  . = . + ((0x0f80 - .) & 0x1fff);
  *(.text.hot.col_info.lto_priv.0)
  . = . + ((0x10ec - .) & 0x1fff);
  *(.text.zone_update.lto_priv.0)
  . = . + ((0x1724 - .) & 0x1fff);
  *(.text.hot.wet_matrix.isra.0)
  . = . + ((0x182c - .) & 0x1fff);
  *(.text.peak)
  . = . + ((0x1c2c - .) & 0x1fff);
  *(.text.audio_loop)
  . = . + ((0x1c3c - .) & 0x1fff);
  *libz.a:inflate.c.o(.text)
  . = . + ((0x1724 - .) & 0x1fff);
  *(.text.paint_environment.lto_priv.0)
  . = . + ((0x0124 - .) & 0x1fff);
  *(.text.world_far_view)
  . = . + ((0x01a0 - .) & 0x1fff);
  *(.text.fe_load)
  . = . + ((0x0348 - .) & 0x1fff);
  *(.text.capture_frame_as.lto_priv.0)
  . = . + ((0x0d8c - .) & 0x1fff);
  *(.text.label_of.constprop.0)
  . = . + ((0x0e90 - .) & 0x1fff);
  *(.text.movies_after.isra.0)
  . = . + ((0x0fc4 - .) & 0x1fff);
  *(.text.career_started)
  . = . + ((0x103c - .) & 0x1fff);
  *(.text.car_accel_body)
  . = . + ((0x11f4 - .) & 0x1fff);
  *(.text.hot.ledge_at.lto_priv.0)
  . = . + ((0x14bc - .) & 0x1fff);
  *(.text.hud_use.lto_priv.0)
  . = . + ((0x1610 - .) & 0x1fff);
  *(.text.hot.world_corridor.constprop.0)
  . = . + ((0x19ac - .) & 0x1fff);
  *(.text.lit_alpha_pass.lto_priv.0)
  . = . + ((0x1a84 - .) & 0x1fff);
  *(.text.hot.draw_model.lto_priv.0)
  . = . + ((0x1d6c - .) & 0x1fff);
  *libz.a:inffast.c.o(.text)
  . = . + ((0x0598 - .) & 0x1fff);
  *(.text.player_box.lto_priv.0)
  . = . + ((0x0730 - .) & 0x1fff);
  *libc.a:libc_a-fseek.o(.text)
  . = . + ((0x0754 - .) & 0x1fff);
  *libpthreadglue.a:__getTlsStructFromThread.o(.text)
  . = . + ((0x07d0 - .) & 0x1fff);
  *(.text.hot.draw_model_range.constprop.0)
  . = . + ((0x1380 - .) & 0x1fff);
  *(.text.hot.find.lto_priv.2)
  . = . + ((0x15b8 - .) & 0x1fff);
  *(.text.test_mark.lto_priv.0)
  . = . + ((0x1824 - .) & 0x1fff);
  *(.text.hot.fe_playing)
  . = . + ((0x1908 - .) & 0x1fff);
  *(.text.speed_db.isra.0)
  . = . + ((0x19b8 - .) & 0x1fff);
  *(.text.flare_effects.lto_priv.0)
  . = . + ((0x1e0c - .) & 0x1fff);
  *(.text.ui_rect.lto_priv.0)
  . = . + ((0x1ec8 - .) & 0x1fff);
  *(.text.career_finished_in)
  . = . + ((0x1f28 - .) & 0x1fff);
  *(.text.race_teleport_settle.lto_priv.0)
  . = . + ((0x1fd0 - .) & 0x1fff);
  *(.text.paint_transfer_prepare.constprop.0.isra.0)
  . = . + ((0x0494 - .) & 0x1fff);
  *(.text.engsnd_rivals)
  . = . + ((0x0870 - .) & 0x1fff);
  *(.text.hot.reach_d2.lto_priv.0)
  . = . + ((0x0b64 - .) & 0x1fff);
  *(.text.near_update.lto_priv.0)
  . = . + ((0x0ec0 - .) & 0x1fff);
  *(.text.hot.cam_levels.lto_priv.0)
  . = . + ((0x0f84 - .) & 0x1fff);
  *(.text.hot.textures_update.lto_priv.0)
  . = . + ((0x1478 - .) & 0x1fff);
  *(.text.fe_play_all)
  . = . + ((0x157c - .) & 0x1fff);
  *libcglue.a:__retarget_lock_acquire.o(.text)
  . = . + ((0x1588 - .) & 0x1fff);
  *libc.a:libc_a-strtoul.o(.text)
  . = . + ((0x17c8 - .) & 0x1fff);
  *(.text.gap_cars.lto_priv.0)
  . = . + ((0x1e60 - .) & 0x1fff);
  *(.text.race_screens_sync.lto_priv.0)
  . = . + ((0x00b8 - .) & 0x1fff);
  *(.text.rain_update)
  . = . + ((0x0438 - .) & 0x1fff);
  *(.text.trigfx_load.constprop.0.isra.0)
  . = . + ((0x0b08 - .) & 0x1fff);
  *(.text.hot.compose)
  . = . + ((0x0ba4 - .) & 0x1fff);
  *(.text.hot.flares_end)
  . = . + ((0x1464 - .) & 0x1fff);
  *(.text.diagram.constprop.0)
  . = . + ((0x00ec - .) & 0x1fff);
  *libc.a:libc_a-sccl.o(.text)
  . = . + ((0x01d4 - .) & 0x1fff);
  *(.text.hot.world_cell)
  . = . + ((0x0314 - .) & 0x1fff);
  *(.text.draw_rival.lto_priv.0)
  . = . + ((0x06dc - .) & 0x1fff);
  *(.text.hot.ge_fence)
  . = . + ((0x0c0c - .) & 0x1fff);
  *libc.a:libc_a-fputc.o(.text)
  . = . + ((0x0da8 - .) & 0x1fff);
  *(.text.hot.take.lto_priv.0)
  . = . + ((0x0dbc - .) & 0x1fff);
  *(.text.gain_of.lto_priv.0)
  . = . + ((0x1034 - .) & 0x1fff);
  *(.text.turn_cost)
  . = . + ((0x10e0 - .) & 0x1fff);
  *(.text.level)
  . = . + ((0x1288 - .) & 0x1fff);
  *(.text.people_start_all)
  . = . + ((0x1ce0 - .) & 0x1fff);
  *(.text.view_perf.lto_priv.0)
  . = . + ((0x1ed4 - .) & 0x1fff);
  *(.text.hot.bind_car_texture.lto_priv.0)
  . = . + ((0x1f40 - .) & 0x1fff);
  *(.text.ctl_map)
  . = . + ((0x0278 - .) & 0x1fff);
  *(.text.scene_effects.lto_priv.0)
  . = . + ((0x0540 - .) & 0x1fff);
  *(.text.intro_handover.lto_priv.0)
  . = . + ((0x1588 - .) & 0x1fff);
  *(.text.career_part_open)
  . = . + ((0x1640 - .) & 0x1fff);
  *(.text.modes_skill.lto_priv.0)
  . = . + ((0x1800 - .) & 0x1fff);
  *(.text.lanes_free)
  . = . + ((0x18bc - .) & 0x1fff);
  *(.text.world_drop_residents)
  . = . + ((0x1c60 - .) & 0x1fff);
  *(.text.hot.live_apply.constprop.0)
  . = . + ((0x0824 - .) & 0x1fff);
  *(.text.tyres_bank)
  . = . + ((0x0930 - .) & 0x1fff);
  *(.text.chyron_draw.lto_priv.0)
  . = . + ((0x13b0 - .) & 0x1fff);
  *(.text.hot.car_gear_drive.lto_priv.0)
  . = . + ((0x1e88 - .) & 0x1fff);
  *(.text.on_stretch.constprop.0)
  . = . + ((0x00c0 - .) & 0x1fff);
  *(.text.hud_draw)
  . = . + ((0x06c4 - .) & 0x1fff);
  *(.text.rain_move)
  . = . + ((0x1634 - .) & 0x1fff);
  *(.text.perf_part_fitted)
  . = . + ((0x1698 - .) & 0x1fff);
  *(.text.audio_stitch_playing)
  . = . + ((0x16f0 - .) & 0x1fff);
  *(.text.fe_texture_read)
  . = . + ((0x193c - .) & 0x1fff);
  *(.text.wall_impact.isra.0)
  . = . + ((0x0640 - .) & 0x1fff);
  *(.text.rewind_setup.lto_priv.0)
  . = . + ((0x0b04 - .) & 0x1fff);
  *(.text.hot.cell_open.constprop.0)
  . = . + ((0x15fc - .) & 0x1fff);
  *(.text.hot.world_update_impl.lto_priv.0)
  . = . + ((0x0c08 - .) & 0x1fff);
  *(.text.stream_prediction_update.lto_priv.0)
  . = . + ((0x11cc - .) & 0x1fff);
  *(.text.hot.tristrip)
  . = . + ((0x1f24 - .) & 0x1fff);
  *(.text.new_hit)
  . = . + ((0x0314 - .) & 0x1fff);
  *(.text.presentation_flip_stop.lto_priv.0)
  . = . + ((0x034c - .) & 0x1fff);
  *(.text.__wrap_sceDisplayWaitVblankStart)
  . = . + ((0x0388 - .) & 0x1fff);
  *(.text.ms_run_done.lto_priv.0)
  . = . + ((0x03fc - .) & 0x1fff);
  *(.text.hot.fixf_sign.lto_priv.0)
  . = . + ((0x07d0 - .) & 0x1fff);
  *(.text.tod_update.part.0.lto_priv.0)
  . = . + ((0x08f0 - .) & 0x1fff);
  *libc.a:libc_a-writer.o(.text)
  . = . + ((0x095c - .) & 0x1fff);
  *(.text.loading_progress_complete.isra.0)
  . = . + ((0x0a0c - .) & 0x1fff);
  *(.text.hot.vista_build)
  . = . + ((0x1c80 - .) & 0x1fff);
  *(.text.outrun_tick.lto_priv.0)
  . = . + ((0x1bfc - .) & 0x1fff);
  *(.text.power_callback.lto_priv.0)
  . = . + ((0x1dc4 - .) & 0x1fff);
  *(.text.read_blob.lto_priv.0)
  . = . + ((0x1f30 - .) & 0x1fff);
  *(.text.hot.mp3_feed.lto_priv.0)
  . = . + ((0x0048 - .) & 0x1fff);
  *libcglue.a:__retarget_lock_release.o(.text)
  . = . + ((0x0050 - .) & 0x1fff);
  *(.text.view_far.lto_priv.0)
  . = . + ((0x0150 - .) & 0x1fff);
  *(.text.hot.driver_ahead)
  . = . + ((0x0304 - .) & 0x1fff);
  *(.text.gap_own_car.lto_priv.0)
  . = . + ((0x03ec - .) & 0x1fff);
  *libc.a:libc_a-tzset.o(.text)
  . = . + ((0x0420 - .) & 0x1fff);
  *(.text.hot.shadow.lto_priv.0)
  . = . + ((0x06e0 - .) & 0x1fff);
  *(.text.hot.car_downforce)
  . = . + ((0x08a0 - .) & 0x1fff);
  *(.text.suspend_sim_thread.lto_priv.0)
  . = . + ((0x0a6c - .) & 0x1fff);
  *(.text.flares_begin)
  . = . + ((0x0ba4 - .) & 0x1fff);
  *(.text.landmark_dependency_layout.lto_priv.0)
  . = . + ((0x0c68 - .) & 0x1fff);
  *(.text.modes_car)
  . = . + ((0x0cf4 - .) & 0x1fff);
  *(.text.hot.table.lto_priv.0)
  . = . + ((0x0d90 - .) & 0x1fff);
  *(.text.col_cell_changed.lto_priv.0)
  . = . + ((0x0e4c - .) & 0x1fff);
  *(.text.race_intro_start)
  . = . + ((0x1db8 - .) & 0x1fff);
  *(.text.xing_ensure.part.0.lto_priv.0)
  . = . + ((0x1e20 - .) & 0x1fff);
  *(.text.opp_pool_build.lto_priv.0)
  . = . + ((0x0160 - .) & 0x1fff);
  *(.text.power_keep_awake.lto_priv.0)
  . = . + ((0x01c4 - .) & 0x1fff);
  *(.text.rachel_parts.lto_priv.0)
  . = . + ((0x0260 - .) & 0x1fff);
  *(.text.stage_check.lto_priv.0)
  . = . + ((0x0468 - .) & 0x1fff);
  *(.text.rachel_update.lto_priv.0)
  . = . + ((0x12b0 - .) & 0x1fff);
  *(.text.label_lines)
  . = . + ((0x1664 - .) & 0x1fff);
  *(.text.paint_publish.lto_priv.0)
  . = . + ((0x19dc - .) & 0x1fff);
  *(.text.clock_measure.lto_priv.0)
  . = . + ((0x1ce8 - .) & 0x1fff);
  *(.text.car_phys_load.constprop.0.isra.0)
  . = . + ((0x09b4 - .) & 0x1fff);
  *(.text.hydraulics_step.lto_priv.0)
  . = . + ((0x1694 - .) & 0x1fff);
  *(.text.read_file.lto_priv.0)
  . = . + ((0x1a0c - .) & 0x1fff);
  *(.text.scene_load.lto_priv.0)
  . = . + ((0x0044 - .) & 0x1fff);
  *(.text.seg_dir.lto_priv.0)
  . = . + ((0x0100 - .) & 0x1fff);
  *(.text.worldanim_draw)
  . = . + ((0x0210 - .) & 0x1fff);
  *(.text.hot.__wrap_sceGuGetMemory)
  . = . + ((0x0268 - .) & 0x1fff);
  *(.text.zone_race.lto_priv.0)
  . = . + ((0x02a8 - .) & 0x1fff);
  *(.text.hot.quad.lto_priv.0)
  . = . + ((0x0b70 - .) & 0x1fff);
  *(.text.hot.pose_at.lto_priv.0)
  . = . + ((0x16d4 - .) & 0x1fff);
  *(.text.need_time.lto_priv.0)
  . = . + ((0x1d74 - .) & 0x1fff);
  *(.text.power_notice_draw.lto_priv.0)
  . = . + ((0x1fa8 - .) & 0x1fff);
  *(.text.hot.pos_at_rpm.constprop.0)
  . = . + ((0x00a4 - .) & 0x1fff);
  *(.text.hot.scale_rgb.lto_priv.0)
  . = . + ((0x012c - .) & 0x1fff);
  *(.text.hot.flip_thread.lto_priv.0)
  . = . + ((0x0278 - .) & 0x1fff);
  *(.text.calls_layout.lto_priv.0)
  . = . + ((0x0610 - .) & 0x1fff);
  *(.text.clock_us)
  . = . + ((0x0618 - .) & 0x1fff);
  *(.text.vr_read.constprop.0.isra.0)
  . = . + ((0x10c0 - .) & 0x1fff);
  *(.text.hot.world_collision_known)
  . = . + ((0x1154 - .) & 0x1fff);
  *(.text.ctl_pad_load.part.0)
  . = . + ((0x12ec - .) & 0x1fff);
  *(.text.records_flush.lto_priv.0)
  . = . + ((0x1374 - .) & 0x1fff);
  *(.text.movie_queue.lto_priv.0)
  . = . + ((0x1588 - .) & 0x1fff);
  *(.text.sweep_load.lto_priv.0)
  . = . + ((0x1fe8 - .) & 0x1fff);
  *(.text.rain_target)
  . = . + ((0x00a0 - .) & 0x1fff);
  *(.text.set_frame_buf.lto_priv.0)
  . = . + ((0x00e4 - .) & 0x1fff);
  *(.text.audio_speech_say)
  . = . + ((0x03f0 - .) & 0x1fff);
  *(.text.__wrap_sceIoOpen)
  . = . + ((0x136c - .) & 0x1fff);
  *(.text.fe_find_nth)
  . = . + ((0x13d0 - .) & 0x1fff);
  *(.text.audio_stitch_of)
  . = . + ((0x1420 - .) & 0x1fff);
  *(.text.hot.mus_read_at.lto_priv.0)
  . = . + ((0x1abc - .) & 0x1fff);
  *(.text.engsnd_player)
  . = . + ((0x1e84 - .) & 0x1fff);
  *(.text.color_grade.lto_priv.0)
  . = . + ((0x1330 - .) & 0x1fff);
  *(.text.place_event_markers.lto_priv.0)
  . = . + ((0x1338 - .) & 0x1fff);
  *(.text.hot.car_level.lto_priv.0)
  . = . + ((0x1474 - .) & 0x1fff);
  *(.text.env_update)
  . = . + ((0x1888 - .) & 0x1fff);
  *(.text.hot.batch_model.isra.0)
  . = . + ((0x18f4 - .) & 0x1fff);
  *(.text.suv_model.lto_priv.0)
  . = . + ((0x196c - .) & 0x1fff);
  *libc.a:libc_a-strrchr.o(.text)
  . = . + ((0x19d8 - .) & 0x1fff);
  *(.text.geo_alloc.lto_priv.0)
  . = . + ((0x1a5c - .) & 0x1fff);
  *(.text.ctl_context.lto_priv.0)
  . = . + ((0x1ac8 - .) & 0x1fff);
  *(.text.wet_capture.lto_priv.0)
  . = . + ((0x1df8 - .) & 0x1fff);
  *(.text.hot.box_rect.constprop.0)
  . = . + ((0x0124 - .) & 0x1fff);
  *(.text.car_place)
  . = . + ((0x0268 - .) & 0x1fff);
  *libc.a:libc_a-mlock.o(.text)
  . = . + ((0x0280 - .) & 0x1fff);
  *(.text.hot.inside_shut.lto_priv.0)
  . = . + ((0x0328 - .) & 0x1fff);
  *(.text.perf_fit_package)
  . = . + ((0x03dc - .) & 0x1fff);
  *(.text.fail_model.isra.0)
  . = . + ((0x0478 - .) & 0x1fff);
  *(.text.arch_evict.part.0.lto_priv.0)
  . = . + ((0x0d9c - .) & 0x1fff);
  *libc.a:libc_a-strcspn.o(.text)
  . = . + ((0x0e10 - .) & 0x1fff);
  *(.text.races_add.isra.0)
  . = . + ((0x1cd0 - .) & 0x1fff);
  *(.text.hot.car_outside.lto_priv.0)
  . = . + ((0x1e90 - .) & 0x1fff);
  *(.text.career_save_service)
  . = . + ((0x00d4 - .) & 0x1fff);
  *(.text.intro_sound.lto_priv.0)
  . = . + ((0x01b4 - .) & 0x1fff);
  *(.text.impacts_contact)
  . = . + ((0x0208 - .) & 0x1fff);
  *(.text.hot.audio_set)
  . = . + ((0x0554 - .) & 0x1fff);
  *(.text.allocator_diag_capture)
  . = . + ((0x0640 - .) & 0x1fff);
  *(.text.hot.traffic_zone.lto_priv.0)
  . = . + ((0x078c - .) & 0x1fff);
  *(.text.flat)
  . = . + ((0x0854 - .) & 0x1fff);
  *(.text.presentation_flip_retire.lto_priv.0)
  . = . + ((0x0a70 - .) & 0x1fff);
  *(.text.hot.cam_building.lto_priv.0)
  . = . + ((0x0b20 - .) & 0x1fff);
  *(.text.decode_xa.lto_priv.0)
  . = . + ((0x0f84 - .) & 0x1fff);
  *(.text.hot.path_point.lto_priv.0)
  . = . + ((0x1280 - .) & 0x1fff);
  *(.text.paint_sheen_warm.lto_priv.0)
  . = . + ((0x12cc - .) & 0x1fff);
  *(.text.rewind_free)
  . = . + ((0x1338 - .) & 0x1fff);
  *(.text.read_table.lto_priv.0)
  . = . + ((0x13e4 - .) & 0x1fff);
  *(.text.skids_wheel_motion)
  . = . + ((0x1694 - .) & 0x1fff);
  *(.text.worldanim_stats)
  . = . + ((0x1728 - .) & 0x1fff);
  *(.text.hot.flare.lto_priv.0)
  . = . + ((0x026c - .) & 0x1fff);
  *(.text.smokeable_load.isra.0)
  . = . + ((0x089c - .) & 0x1fff);
  *(.text.chyron_show_in.constprop.0)
  . = . + ((0x0ffc - .) & 0x1fff);
  *(.text.career_find)
  . = . + ((0x1144 - .) & 0x1fff);
  *(.text.hot.ground_cell.lto_priv.0)
  . = . + ((0x1724 - .) & 0x1fff);
  *(.text.drive_hud.lto_priv.0)
  . = . + ((0x0efc - .) & 0x1fff);
  *(.text.hot.ground_surface.lto_priv.0)
  . = . + ((0x1160 - .) & 0x1fff);
  *(.text.vista_hold.lto_priv.0)
  . = . + ((0x13c8 - .) & 0x1fff);
  *(.text.worldanim_restart)
  . = . + ((0x15a0 - .) & 0x1fff);
  *(.text.hot.wheel_pos.lto_priv.0)
  . = . + ((0x1640 - .) & 0x1fff);
  *(.text.hot.loader_io.lto_priv.0)
  . = . + ((0x1974 - .) & 0x1fff);
  *(.text.hot.racer_perf.lto_priv.0)
  . = . + ((0x1aa8 - .) & 0x1fff);
  *(.text.diff_load.constprop.0.isra.0)
  . = . + ((0x1c34 - .) & 0x1fff);
  *(.text.bottom_sound.lto_priv.0)
  . = . + ((0x037c - .) & 0x1fff);
  *(.text.freeze_test_tick.lto_priv.0)
  . = . + ((0x0434 - .) & 0x1fff);
  *(.text.draw_street_car.lto_priv.0)
  . = . + ((0x0640 - .) & 0x1fff);
  *(.text.zone_jump_speed)
  . = . + ((0x06c0 - .) & 0x1fff);
  *(.text.fx_update)
  . = . + ((0x09e4 - .) & 0x1fff);
  *(.text.impacts_knock)
  . = . + ((0x0ce8 - .) & 0x1fff);
  *(.text.td_free)
  . = . + ((0x0da4 - .) & 0x1fff);
  *(.text.tele_write_snapshot.lto_priv.0)
  . = . + ((0x0f70 - .) & 0x1fff);
  *(.text.hot.__wrap_sceGuCallList)
  . = . + ((0x0fe4 - .) & 0x1fff);
  *libcglue.a:__retarget_lock_close_recursive.o(.text)
  . = . + ((0x1004 - .) & 0x1fff);
  *libc.a:libc_a-errno.o(.text)
  . = . + ((0x1010 - .) & 0x1fff);
  *(.text.hot.noise_fbm)
  . = . + ((0x178c - .) & 0x1fff);
  *(.text.hot.part_gate.lto_priv.0)
  . = . + ((0x1808 - .) & 0x1fff);
  *(.text.hot.in_tunnel.lto_priv.0)
  . = . + ((0x199c - .) & 0x1fff);
  *(.text.career_sms_unread)
  . = . + ((0x1a08 - .) & 0x1fff);
  *(.text.draw_horizon_band.lto_priv.0)
  . = . + ((0x1d04 - .) & 0x1fff);
  *(.text.hot.request.lto_priv.0)
  . = . + ((0x1eec - .) & 0x1fff);
  *(.text.level_complete.lto_priv.0)
  . = . + ((0x1fa4 - .) & 0x1fff);
  *(.text.draw_layer.part.0.lto_priv.0)
  . = . + ((0x15ec - .) & 0x1fff);
  *(.text.moment_detect.lto_priv.0)
  . = . + ((0x14f8 - .) & 0x1fff);
  *(.text.hot.fade_touch)
  . = . + ((0x1610 - .) & 0x1fff);
  *(.text.mus_after.lto_priv.0)
  . = . + ((0x179c - .) & 0x1fff);
  *(.text.mul.lto_priv.0)
  . = . + ((0x18bc - .) & 0x1fff);
  *(.text.hot.headlight_on_road.lto_priv.0)
  . = . + ((0x0d4c - .) & 0x1fff);
  *libc.a:libc_a-sprintf.o(.text)
  . = . + ((0x0e3c - .) & 0x1fff);
  *(.text.hot.lit_count.lto_priv.0)
  . = . + ((0x0ec4 - .) & 0x1fff);
  *(.text.bars_match)
  . = . + ((0x1178 - .) & 0x1fff);
  *(.text.split_reached.lto_priv.0)
  . = . + ((0x1290 - .) & 0x1fff);
  *(.text.region_hud.lto_priv.0)
  . = . + ((0x1450 - .) & 0x1fff);
  *(.text.jukebox_load.lto_priv.0)
  . = . + ((0x1614 - .) & 0x1fff);
  *libc.a:libc_a-fflush.o(.text)
  . = . + ((0x1a44 - .) & 0x1fff);
  *(.text.far_first_soon.lto_priv.0)
  . = . + ((0x1acc - .) & 0x1fff);
  *(.text.gps_to_target.lto_priv.0)
  . = . + ((0x1cfc - .) & 0x1fff);
  *(.text.hot.deepest_push.constprop.0)
  . = . + ((0x01b4 - .) & 0x1fff);
  *(.text.race_line_forks.isra.0)
  . = . + ((0x0790 - .) & 0x1fff);
  *(.text.menu_input.lto_priv.0)
  . = . + ((0x1604 - .) & 0x1fff);
  *(.text.race_line)
  . = . + ((0x1e70 - .) & 0x1fff);
  *libpthreadglue.a:pteTlsGlobalInit.o(.text)
  . = . + ((0x1ec8 - .) & 0x1fff);
  *(.text.extend.lto_priv.1)
  . = . + ((0x04e0 - .) & 0x1fff);
  *(.text.style_award)
  . = . + ((0x0864 - .) & 0x1fff);
  *(.text.perf_dyno)
  . = . + ((0x0a84 - .) & 0x1fff);
  *(.text.hot.corr_reach.lto_priv.0)
  . = . + ((0x0afc - .) & 0x1fff);
  *(.text.points_text.lto_priv.0)
  . = . + ((0x0bb4 - .) & 0x1fff);
  *libpthreadglue.a:__pteTlsSetValue.o(.text)
  . = . + ((0x0bd4 - .) & 0x1fff);
  *(.text.career_see_events)
  . = . + ((0x0bfc - .) & 0x1fff);
  *(.text.hot.solid_poly.lto_priv.0)
  . = . + ((0x0db0 - .) & 0x1fff);
  *(.text.record_lap.part.0.lto_priv.0)
  . = . + ((0x0ebc - .) & 0x1fff);
  *(.text.load.lto_priv.1)
  . = . + ((0x1250 - .) & 0x1fff);
  *(.text.hot.box_overlap.lto_priv.0)
  . = . + ((0x16e0 - .) & 0x1fff);
  *libcglue.a:__fdman_release_descriptor.o(.text)
  . = . + ((0x178c - .) & 0x1fff);
  *(.text.wheel_push.part.0.lto_priv.0)
  . = . + ((0x1c38 - .) & 0x1fff);
  *(.text.nearest)
  . = . + ((0x1ce0 - .) & 0x1fff);
  *(.text.resolve_body.lto_priv.0)
  . = . + ((0x0250 - .) & 0x1fff);
  *(.text.__wrap_sceIoRead)
  . = . + ((0x0e30 - .) & 0x1fff);
  *(.text.draw_car.lto_priv.0)
  . = . + ((0x1420 - .) & 0x1fff);
  *(.text.traffic_update.lto_priv.0)
  . = . + ((0x1d94 - .) & 0x1fff);
  *(.text.fitted_of.lto_priv.0)
  . = . + ((0x1f00 - .) & 0x1fff);
  *(.text.world_wall_strip_layer)
  . = . + ((0x03f4 - .) & 0x1fff);
  *(.text.read_car.lto_priv.0)
  . = . + ((0x0510 - .) & 0x1fff);
  *(.text.part.constprop.0)
  . = . + ((0x0760 - .) & 0x1fff);
  *(.text.search_take)
  . = . + ((0x0970 - .) & 0x1fff);
  *(.text.forks_free)
  . = . + ((0x09fc - .) & 0x1fff);
  *(.text.hot.street_car_lights.lto_priv.0)
  . = . + ((0x0b04 - .) & 0x1fff);
  *(.text.eng_entry.lto_priv.0)
  . = . + ((0x0d9c - .) & 0x1fff);
  *(.text.hood_at.lto_priv.0)
  . = . + ((0x0edc - .) & 0x1fff);
  *(.text.shops_update.lto_priv.0)
  . = . + ((0x1330 - .) & 0x1fff);
  *(.text.hot.line_climb)
  . = . + ((0x1598 - .) & 0x1fff);
  *(.text.model_quantize.constprop.0)
  . = . + ((0x11c8 - .) & 0x1fff);
  *(.text.ai_can)
  . = . + ((0x136c - .) & 0x1fff);
  *(.text.hot.point_dir)
  . = . + ((0x1434 - .) & 0x1fff);
  *(.text.dot.constprop.0)
  . = . + ((0x1488 - .) & 0x1fff);
  *(.text.original_prepare_event.constprop.0)
  . = . + ((0x1b14 - .) & 0x1fff);
  *(.text.intro_ground)
  . = . + ((0x1b90 - .) & 0x1fff);
  *(.text.hot.power_fd.isra.0)
  . = . + ((0x1c48 - .) & 0x1fff);
  *(.text.fe_debrief_ensure)
  . = . + ((0x1f64 - .) & 0x1fff);
  *(.text.grid_onto_course.lto_priv.0)
  . = . + ((0x0190 - .) & 0x1fff);
  *(.text.career_engage_update.lto_priv.0)
  . = . + ((0x05b8 - .) & 0x1fff);
  *(.text.cam_high.lto_priv.0)
  . = . + ((0x0b44 - .) & 0x1fff);
  *(.text.chase_camera.lto_priv.0)
  . = . + ((0x1b08 - .) & 0x1fff);
  *(.text.racer_label.part.0.lto_priv.0)
  . = . + ((0x1c20 - .) & 0x1fff);
  *(.text.hot.to_screen_at.lto_priv.0)
  . = . + ((0x1eac - .) & 0x1fff);
  *(.text.push)
  . = . + ((0x1f88 - .) & 0x1fff);
  *(.text.car_step_control.isra.0)
  . = . + ((0x1218 - .) & 0x1fff);
  *(.text.pmap_free)
  . = . + ((0x1258 - .) & 0x1fff);
  *(.text.ai_nos.lto_priv.0)
  . = . + ((0x1908 - .) & 0x1fff);
  *(.text.rd_pedal_map.part.0)
  . = . + ((0x1c70 - .) & 0x1fff);
  *(.text.hot.traffic_beyond_horizon)
  . = . + ((0x1ccc - .) & 0x1fff);
  *(.text.sky_cut_row.lto_priv.0)
  . = . + ((0x04dc - .) & 0x1fff);
  *(.text.draw_sky.lto_priv.0)
  . = . + ((0x0f80 - .) & 0x1fff);
  *(.text.dawn_hue_bgr.lto_priv.0)
  . = . + ((0x123c - .) & 0x1fff);
  *(.text.car_effects.lto_priv.0)
  . = . + ((0x000c - .) & 0x1fff);
  *libc.a:libc_a-malloc.o(.text)
  . = . + ((0x002c - .) & 0x1fff);
  *(.text.original_placed.lto_priv.0)
  . = . + ((0x00d0 - .) & 0x1fff);
  *(.text.lang_en_n)
  . = . + ((0x0300 - .) & 0x1fff);
  *(.text.draw_drive.lto_priv.0)
  . = . + ((0x14b0 - .) & 0x1fff);
  *(.text.hot.reach)
  . = . + ((0x1740 - .) & 0x1fff);
  *(.text.street_tint.lto_priv.0)
  . = . + ((0x1d40 - .) & 0x1fff);
  *(.text.audio_mixer_hold)
  . = . + ((0x1f14 - .) & 0x1fff);
  *(.text.ride_level.lto_priv.0)
  . = . + ((0x00a4 - .) & 0x1fff);
  *(.text.race_update.lto_priv.0)
  . = . + ((0x0ee8 - .) & 0x1fff);
  *(.text.hot.mus_level.lto_priv.0)
  . = . + ((0x0fb4 - .) & 0x1fff);
  *libpthreadglue.a:pte_osMutexLock.o(.text)
  . = . + ((0x0fd8 - .) & 0x1fff);
  *(.text.threads_stop.lto_priv.0)
  . = . + ((0x1080 - .) & 0x1fff);
  *(.text.landmark_dependencies_valid.constprop.0)
  . = . + ((0x12d0 - .) & 0x1fff);
  *(.text.sound_update.lto_priv.0)
  . = . + ((0x115c - .) & 0x1fff);
  *(.text.outrun_watch.lto_priv.0)
  . = . + ((0x17f8 - .) & 0x1fff);
  *(.text.tick)
  . = . + ((0x08dc - .) & 0x1fff);
  *(.text.shows_update.lto_priv.0)
  . = . + ((0x10bc - .) & 0x1fff);
  *(.text.draw_minimap.lto_priv.0)
  . = . + ((0x1588 - .) & 0x1fff);
  *(.text.hot.rgb_of.lto_priv.0)
  . = . + ((0x1790 - .) & 0x1fff);
  *(.text.col_mark_cars.lto_priv.0)
  . = . + ((0x00ec - .) & 0x1fff);
  *(.text.manual_street_box.lto_priv.0)
  . = . + ((0x06dc - .) & 0x1fff);
  *(.text.mips_service)
  . = . + ((0x14e8 - .) & 0x1fff);
  *(.text.targets_build.lto_priv.0)
  . = . + ((0x06b0 - .) & 0x1fff);
  *(.text.cash_update.lto_priv.0)
  . = . + ((0x08c8 - .) & 0x1fff);
  *(.text.ramp_at.lto_priv.0)
  . = . + ((0x0960 - .) & 0x1fff);
  *(.text.trax_play.lto_priv.0)
  . = . + ((0x10cc - .) & 0x1fff);
  *(.text.__wrap_sceGeListUpdateStallAddr)
  . = . + ((0x115c - .) & 0x1fff);
  *(.text.hot.lit_alpha_zero_box.lto_priv.0)
  . = . + ((0x14c8 - .) & 0x1fff);
  *(.text.init_neon_tex.lto_priv.0)
  . = . + ((0x17d4 - .) & 0x1fff);
  *libpthreadglue.a:pte_osInit.o(.text)
  . = . + ((0x18fc - .) & 0x1fff);
  *(.text.car_inside_solid)
  . = . + ((0x1944 - .) & 0x1fff);
  *(.text.telemetry.lto_priv.0)
  . = . + ((0x15c0 - .) & 0x1fff);
  *(.text.hot.car_quant_push.lto_priv.0)
  . = . + ((0x1628 - .) & 0x1fff);
  *(.text.car_ground_tilt)
  . = . + ((0x198c - .) & 0x1fff);
  *libcglue.a:__retarget_lock_acquire_recursive.o(.text)
  . = . + ((0x1998 - .) & 0x1fff);
  *(.text.fe_stamp)
  . = . + ((0x1d20 - .) & 0x1fff);
  *libc.a:libc_a-fvwrite.o(.text)
  . = . + ((0x0214 - .) & 0x1fff);
  *(.text.race_track.part.0.lto_priv.0)
  . = . + ((0x1270 - .) & 0x1fff);
  *(.text.car_reset.lto_priv.0)
  . = . + ((0x13c8 - .) & 0x1fff);
  *(.text.hot.col_layers.lto_priv.0)
  . = . + ((0x14fc - .) & 0x1fff);
  *(.text.live_turn)
  . = . + ((0x169c - .) & 0x1fff);
  *(.text.career_shown)
  . = . + ((0x1770 - .) & 0x1fff);
  *(.text.hl_colour)
  . = . + ((0x17f0 - .) & 0x1fff);
  *(.text.career_sms_sent.part.0)
  . = . + ((0x1838 - .) & 0x1fff);
  *(.text.nis_play_camera)
  . = . + ((0x1b38 - .) & 0x1fff);
  *(.text.lang_en)
  . = . + ((0x1be0 - .) & 0x1fff);
  *(.text.lamps_read_file.lto_priv.0)
  . = . + ((0x1e88 - .) & 0x1fff);
  *(.text.impacts_contact.part.0)
  . = . + ((0x04c4 - .) & 0x1fff);
  *(.text.manual_on.lto_priv.0)
  . = . + ((0x0548 - .) & 0x1fff);
  *(.text.near_free)
  . = . + ((0x19cc - .) & 0x1fff);
  *(.text.__wrap_sceIoClose)
  . = . + ((0x1a10 - .) & 0x1fff);
  *(.text.st_sides.lto_priv.0)
  . = . + ((0x1bf8 - .) & 0x1fff);
  *(.text.solid_for_bit.lto_priv.0)
  . = . + ((0x1e94 - .) & 0x1fff);
  *(.text.layer_read.lto_priv.0)
  . = . + ((0x0070 - .) & 0x1fff);
  *(.text.body_tick.lto_priv.0)
  . = . + ((0x18c4 - .) & 0x1fff);
  *(.text.hot.__wrap_fseek)
  . = . + ((0x1a5c - .) & 0x1fff);
  *(.text.original_hud.lto_priv.0)
  . = . + ((0x0bcc - .) & 0x1fff);
  *(.text.state_valid.constprop.0)
  . = . + ((0x1204 - .) & 0x1fff);
  *(.text.hot.people_strips)
  . = . + ((0x0078 - .) & 0x1fff);
  *(.text.post_hit)
  . = . + ((0x05b0 - .) & 0x1fff);
  *(.text.damage_load.lto_priv.0)
  . = . + ((0x0744 - .) & 0x1fff);
  *(.text.world_lit_alpha_probe)
  . = . + ((0x0a54 - .) & 0x1fff);
  *(.text.verb_load.lto_priv.0)
  . = . + ((0x0cf8 - .) & 0x1fff);
  *(.text.net_on)
  . = . + ((0x0d28 - .) & 0x1fff);
  *(.text.mus_thread)
  . = . + ((0x000c - .) & 0x1fff);
  *(.text.career_events_seen)
  . = . + ((0x0244 - .) & 0x1fff);
  *(.text.engine_build)
  . = . + ((0x0488 - .) & 0x1fff);
  *(.text.fit_engine.lto_priv.0)
  . = . + ((0x114c - .) & 0x1fff);
  *(.text.nos_sound_step.part.0.constprop.0)
  . = . + ((0x0020 - .) & 0x1fff);
  *(.text.hill_update)
  . = . + ((0x020c - .) & 0x1fff);
  *(.text.ram_text.lto_priv.0)
  . = . + ((0x0340 - .) & 0x1fff);
  *(.text.career_marker.lto_priv.0)
  . = . + ((0x0554 - .) & 0x1fff);
  *(.text.street_engage_kind.lto_priv.0)
  . = . + ((0x06d0 - .) & 0x1fff);
  *(.text.audio_music_open.constprop.0.isra.0)
  . = . + ((0x0d34 - .) & 0x1fff);
  *(.text.paint_at.constprop.0)
  . = . + ((0x0f4c - .) & 0x1fff);
  *(.text.measure_of.lto_priv.0)
  . = . + ((0x126c - .) & 0x1fff);
  *(.text.external_body)
  . = . + ((0x12a4 - .) & 0x1fff);
  *(.text.tyres_rival_update.isra.0)
  . = . + ((0x0044 - .) & 0x1fff);
  *(.text.skid_noise_context.lto_priv.0)
  . = . + ((0x0d44 - .) & 0x1fff);
  *libc.a:libc_a-vfscanf.o(.text)
  . = . + ((0x12bc - .) & 0x1fff);
  *(.text.rival_tyres_update.constprop.0)
  . = . + ((0x01e4 - .) & 0x1fff);
  *libcglue.a:__path_absolute.o(.text)
  . = . + ((0x06bc - .) & 0x1fff);
  *(.text.clear_owned.lto_priv.0)
  . = . + ((0x0810 - .) & 0x1fff);
  *(.text.roads_within)
  . = . + ((0x0b64 - .) & 0x1fff);
  *libc.a:libc_a-strlen.o(.text)
  . = . + ((0x0b80 - .) & 0x1fff);
  *(.text.__libcglue_init)
  . = . + ((0x0c50 - .) & 0x1fff);
  *(.text.player_measure.lto_priv.0)
  . = . + ((0x0fc4 - .) & 0x1fff);
  *(.text.option_entry)
  . = . + ((0x12c0 - .) & 0x1fff);
  *libz.a:crc32.c.o(.text)
  . = . + ((0x1ed8 - .) & 0x1fff);
  *(.text.tach_set.lto_priv.0)
  . = . + ((0x1ff4 - .) & 0x1fff);
  *libcglue.a:__timezone_update.o(.text)
  . = . + ((0x00d0 - .) & 0x1fff);
  *(.text.hot.pose.isra.0)
  . = . + ((0x08e0 - .) & 0x1fff);
  *(.text.shows_done.lto_priv.0)
  . = . + ((0x0908 - .) & 0x1fff);
  *(.text.rewind_add)
  . = . + ((0x0984 - .) & 0x1fff);
  *(.text.audio_nos_mix.constprop.0)
  . = . + ((0x1900 - .) & 0x1fff);
  *(.text.fe_string_align)
  . = . + ((0x1bf8 - .) & 0x1fff);
  *(.text.hot.track_of)
  . = . + ((0x106c - .) & 0x1fff);
  *(.text.roads_nearest)
  . = . + ((0x16cc - .) & 0x1fff);
  *(.text.audio_half)
  . = . + ((0x1714 - .) & 0x1fff);
  *(.text.hot.overlay_reach.lto_priv.0)
  . = . + ((0x1754 - .) & 0x1fff);
  *(.text.draw_far_world.lto_priv.0)
  . = . + ((0x1a08 - .) & 0x1fff);
  *(.text.world_critical_begin)
  . = . + ((0x1c88 - .) & 0x1fff);
  *(.text.arch_free.lto_priv.0)
  . = . + ((0x02ec - .) & 0x1fff);
  *(.text.hot.fe_play)
  . = . + ((0x04c4 - .) & 0x1fff);
  *(.text.sr_ask.lto_priv.0)
  . = . + ((0x04e8 - .) & 0x1fff);
  *(.text.parse_model.lto_priv.0)
  . = . + ((0x1080 - .) & 0x1fff);
  *(.text.exchange.isra.0)
  . = . + ((0x151c - .) & 0x1fff);
  *(.text.search_start)
  . = . + ((0x19f0 - .) & 0x1fff);
  *(.text.traffic_near.lto_priv.0)
  . = . + ((0x1b48 - .) & 0x1fff);
  *(.text.host_thread)
  . = . + ((0x1cf8 - .) & 0x1fff);
  *(.text.perf_brake_style)
  . = . + ((0x1d3c - .) & 0x1fff);
  *(.text.mus_head_in.lto_priv.0)
  . = . + ((0x1e5c - .) & 0x1fff);
  *libcglue.a:__fdman_init.o(.text)
  . = . + ((0x1f28 - .) & 0x1fff);
  *(.text.hot.rail_pack)
  . = . + ((0x003c - .) & 0x1fff);
  *(.text.career_unique_open)
  . = . + ((0x00dc - .) & 0x1fff);
  *(.text.cam_ref_update.lto_priv.0)
  . = . + ((0x0154 - .) & 0x1fff);
  *(.text.style_show)
  . = . + ((0x0310 - .) & 0x1fff);
  *(.text.fpu_poll)
  . = . + ((0x046c - .) & 0x1fff);
  *(.text.source_option.part.0)
  . = . + ((0x04f8 - .) & 0x1fff);
  *libcglue.a:getcwd.o(.text)
  . = . + ((0x0580 - .) & 0x1fff);
  *(.text.fe_free.part.0)
  . = . + ((0x0854 - .) & 0x1fff);
  *libcglue.a:__retarget_lock_release_recursive.o(.text)
  . = . + ((0x085c - .) & 0x1fff);
  *(.text.look_hood_split.constprop.0)
  . = . + ((0x08b4 - .) & 0x1fff);
  *(.text.fe_discard.lto_priv.0)
  . = . + ((0x09e8 - .) & 0x1fff);
  *(.text.launch_pull.lto_priv.0)
  . = . + ((0x0b08 - .) & 0x1fff);
  *(.text.hot.col_kinds.lto_priv.0)
  . = . + ((0x0b9c - .) & 0x1fff);
  *(.text.loader_behind.lto_priv.0)
  . = . + ((0x0c48 - .) & 0x1fff);
  *(.text.cyl_contact.lto_priv.0)
  . = . + ((0x0d74 - .) & 0x1fff);
  *(.text.pr_share.lto_priv.0)
  . = . + ((0x0e18 - .) & 0x1fff);
  *libc.a:libc_a-strstr.o(.text)
  . = . + ((0x1944 - .) & 0x1fff);
  *(.text.thread_end.lto_priv.0)
  . = . + ((0x19bc - .) & 0x1fff);
  *(.text.intro_stop)
  . = . + ((0x19f8 - .) & 0x1fff);
  *(.text.start.lto_priv.1)
  . = . + ((0x1aa0 - .) & 0x1fff);
  *(.text.i103_flush.lto_priv.0)
  . = . + ((0x1b18 - .) & 0x1fff);
  *(.text.hot.box_outside.lto_priv.0)
  . = . + ((0x1e54 - .) & 0x1fff);
  *libc.a:libc_a-unlinkr.o(.text)
  . = . + ((0x1eb4 - .) & 0x1fff);
  *(.text.career_save_mode.lto_priv.0)
  . = . + ((0x082c - .) & 0x1fff);
  *(.text.make_textures.lto_priv.0)
  . = . + ((0x1630 - .) & 0x1fff);
  *(.text.career_sms_new)
  . = . + ((0x1908 - .) & 0x1fff);
  *libcglue.a:_unlink.o(.text)
  . = . + ((0x1958 - .) & 0x1fff);
  *(.text.hot.garage_paint_material)
  . = . + ((0x19b8 - .) & 0x1fff);
  *(.text.hot.draw_items.lto_priv.0)
  . = . + ((0x0654 - .) & 0x1fff);
  *(.text.tyres_load.isra.0)
  . = . + ((0x145c - .) & 0x1fff);
  *(.text.hot.lit_collect.lto_priv.0)
  . = . + ((0x14d4 - .) & 0x1fff);
  *(.text.rvm_wanted)
  . = . + ((0x15d4 - .) & 0x1fff);
  *(.text.build.lto_priv.1)
  . = . + ((0x1df8 - .) & 0x1fff);
  *(.text.draw_flares.lto_priv.0)
  . = . + ((0x1ff8 - .) & 0x1fff);
  *(.text.fe_owned_bytes)
  . = . + ((0x0404 - .) & 0x1fff);
  *(.text.launch_go.lto_priv.0)
  . = . + ((0x0564 - .) & 0x1fff);
  *(.text.gps_arrow_draw.lto_priv.0)
  . = . + ((0x0830 - .) & 0x1fff);
  *(.text.hot.hud_colour_sync.lto_priv.0)
  . = . + ((0x0a60 - .) & 0x1fff);
  *(.text.garage_stock_look)
  . = . + ((0x0a8c - .) & 0x1fff);
  *(.text.db_from_q15.lto_priv.0)
  . = . + ((0x0bbc - .) & 0x1fff);
  *(.text.mus_resolve.lto_priv.0)
  . = . + ((0x0e1c - .) & 0x1fff);
  *(.text.hot.frustum.lto_priv.0)
  . = . + ((0x139c - .) & 0x1fff);
  *(.text.perf_fit_part)
  . = . + ((0x1450 - .) & 0x1fff);
  *(.text.style_load.lto_priv.0)
  . = . + ((0x1510 - .) & 0x1fff);
  *(.text.bot_lx.lto_priv.0)
  . = . + ((0x1838 - .) & 0x1fff);
  *(.text.traffic_ground_check.lto_priv.0)
  . = . + ((0x01a0 - .) & 0x1fff);
  *(.text.traffic_in_clear_start.isra.0)
  . = . + ((0x04ec - .) & 0x1fff);
  *(.text.wide_apply.lto_priv.0)
  . = . + ((0x053c - .) & 0x1fff);
  *(.text.headlight_grid)
  . = . + ((0x0a98 - .) & 0x1fff);
  *(.text.racer_box.lto_priv.0)
  . = . + ((0x0d40 - .) & 0x1fff);
  *(.text.moment_update)
  . = . + ((0x1134 - .) & 0x1fff);
  *(.text.audio_stitch_set)
  . = . + ((0x1240 - .) & 0x1fff);
  *(.text.hot.point_ok.lto_priv.0)
  . = . + ((0x12fc - .) & 0x1fff);
  *(.text.vol_acquire)
  . = . + ((0x17e4 - .) & 0x1fff);
  *(.text.hot.col_at_size.lto_priv.0)
  . = . + ((0x1bf8 - .) & 0x1fff);
  *(.text.vr_entry.lto_priv.0)
  . = . + ((0x1dcc - .) & 0x1fff);
  *libc.a:libc_a-lseekr.o(.text)
  . = . + ((0x1e38 - .) & 0x1fff);
  *(.text.start_ground_known.lto_priv.0)
  . = . + ((0x1edc - .) & 0x1fff);
  *(.text.ride_reskin.constprop.0)
  . = . + ((0x0c80 - .) & 0x1fff);
  *(.text.fe_play_at)
  . = . + ((0x0d14 - .) & 0x1fff);
  *(.text.age.lto_priv.0)
  . = . + ((0x0f84 - .) & 0x1fff);
  *(.text.search_step)
  . = . + ((0x1498 - .) & 0x1fff);
  *(.text.kernel_free_watch)
  . = . + ((0x14f0 - .) & 0x1fff);
  *(.text.hot.gate_open)
  . = . + ((0x1624 - .) & 0x1fff);
  *(.text.loader_main.lto_priv.0)
  . = . + ((0x168c - .) & 0x1fff);
  *(.text.fx_emit_tint.part.0)
  . = . + ((0x1ba8 - .) & 0x1fff);
  *(.text.hot.boxes_meet)
  . = . + ((0x1df8 - .) & 0x1fff);
  *(.text.stitch_start.lto_priv.0)
  . = . + ((0x07c0 - .) & 0x1fff);
  *(.text.opp_random.lto_priv.0)
  . = . + ((0x0874 - .) & 0x1fff);
  *(.text.car_cull_update)
  . = . + ((0x17f0 - .) & 0x1fff);
  *libc.a:libc_a-wsetup.o(.text)
  . = . + ((0x1978 - .) & 0x1fff);
  *(.text.load_model_buffer.lto_priv.0)
  . = . + ((0x1a84 - .) & 0x1fff);
  *(.text.look_colours.lto_priv.0)
  . = . + ((0x01c4 - .) & 0x1fff);
  *(.text.look_lamps.lto_priv.0)
  . = . + ((0x03ec - .) & 0x1fff);
  *(.text.draw_vista.lto_priv.0)
  . = . + ((0x1400 - .) & 0x1fff);
  *(.text.ge_draw_ready.lto_priv.0)
  . = . + ((0x1468 - .) & 0x1fff);
  *(.text.draw_headlight_lit)
  . = . + ((0x1448 - .) & 0x1fff);
  *(.text.nos_fill)
  . = . + ((0x14bc - .) & 0x1fff);
  *(.text.bound.lto_priv.0)
  . = . + ((0x1630 - .) & 0x1fff);
  *(.text.hot.__wrap_sceGuDrawArray)
  . = . + ((0x1838 - .) & 0x1fff);
  *(.text.__wrap_sceKernelCreateThread)
  . = . + ((0x18ac - .) & 0x1fff);
  *(.text.fx_new.lto_priv.0)
  . = . + ((0x19dc - .) & 0x1fff);
  *(.text.opp_match.lto_priv.0)
  . = . + ((0x0bfc - .) & 0x1fff);
  *(.text.zone_air.lto_priv.0)
  . = . + ((0x0fc4 - .) & 0x1fff);
  *(.text.world_sound.lto_priv.0)
  . = . + ((0x1a10 - .) & 0x1fff);
  *(.text.flares_draw_car)
  . = . + ((0x1c7c - .) & 0x1fff);
  *(.text.pack_parse)
  . = . + ((0x0060 - .) & 0x1fff);
  *(.text.rvm_load.lto_priv.0)
  . = . + ((0x01dc - .) & 0x1fff);
  *(.text.lp1.lto_priv.0)
  . = . + ((0x0588 - .) & 0x1fff);
  *libc.a:libc_a-realloc.o(.text)
  . = . + ((0x05a0 - .) & 0x1fff);
  *(.text.rvm_draw.lto_priv.0)
  . = . + ((0x0a5c - .) & 0x1fff);
  *(.text.hot.intro_fx)
  . = . + ((0x0a98 - .) & 0x1fff);
  *(.text.hot.rot_of.lto_priv.0)
  . = . + ((0x0bac - .) & 0x1fff);
  *(.text.minimap_pictured)
  . = . + ((0x0c28 - .) & 0x1fff);
  *(.text.opponent_big.lto_priv.0)
  . = . + ((0x0f2c - .) & 0x1fff);
  *(.text.mode_hud.lto_priv.0)
  . = . + ((0x1e1c - .) & 0x1fff);
  *(.text.mus_take.lto_priv.0)
  . = . + ((0x1f20 - .) & 0x1fff);
  *(.text.outrun_hud.lto_priv.0)
  . = . + ((0x185c - .) & 0x1fff);
  *(.text.nos_flames.lto_priv.0)
  . = . + ((0x1aa8 - .) & 0x1fff);
  *(.text.contact_impulse.lto_priv.0)
  . = . + ((0x1f54 - .) & 0x1fff);
  *(.text.begin.lto_priv.0)
  . = . + ((0x1470 - .) & 0x1fff);
  *(.text.freeze_watch)
  . = . + ((0x16c0 - .) & 0x1fff);
  *(.text.hud_style_sync.lto_priv.0)
  . = . + ((0x1a1c - .) & 0x1fff);
  *(.text.opp_looks.lto_priv.0)
  . = . + ((0x0e70 - .) & 0x1fff);
  *(.text.find.lto_priv.1)
  . = . + ((0x10d0 - .) & 0x1fff);
  *(.text.owner_of.lto_priv.0)
  . = . + ((0x1410 - .) & 0x1fff);
  *(.text.draw_parts.lto_priv.0)
  . = . + ((0x0518 - .) & 0x1fff);
  *(.text.driver_rail.part.0)
  . = . + ((0x0778 - .) & 0x1fff);
  *libc.a:libc_a-sbrkr.o(.text)
  . = . + ((0x07d8 - .) & 0x1fff);
  *libc.a:libc_a-fstatr.o(.text)
  . = . + ((0x0840 - .) & 0x1fff);
  *(.text.add_points.isra.0)
  . = . + ((0x08d0 - .) & 0x1fff);
  *(.text.vinyl_at.constprop.0)
  . = . + ((0x0cb4 - .) & 0x1fff);
  *libc.a:libc_a-flags.o(.text)
  . = . + ((0x0d68 - .) & 0x1fff);
  *(.text.slot_sync.lto_priv.0)
  . = . + ((0x0f24 - .) & 0x1fff);
  *(.text.garage_rim_available)
  . = . + ((0x14c0 - .) & 0x1fff);
  *(.text.car_step_control.lto_priv.0)
  . = . + ((0x18e8 - .) & 0x1fff);
  *(.text.row_key)
  . = . + ((0x1aa0 - .) & 0x1fff);
  *(.text.career_contract)
  . = . + ((0x1c2c - .) & 0x1fff);
  *(.text.spin_decel.isra.0)
  . = . + ((0x1d68 - .) & 0x1fff);
  *(.text.rvm_render.lto_priv.0)
  . = . + ((0x01f4 - .) & 0x1fff);
  *libc.a:libc_a-fseeko.o(.text)
  . = . + ((0x0760 - .) & 0x1fff);
  *(.text.loading_thread.lto_priv.0)
  . = . + ((0x1fe0 - .) & 0x1fff);
  *(.text.driveby_car)
  . = . + ((0x01ac - .) & 0x1fff);
  *(.text.new_body.lto_priv.0)
  . = . + ((0x040c - .) & 0x1fff);
  *(.text.split_diag_bytes)
  . = . + ((0x0484 - .) & 0x1fff);
  *(.text.hot.draw_batch.lto_priv.0)
  . = . + ((0x0530 - .) & 0x1fff);
  *(.text.smokeable_free)
  . = . + ((0x0638 - .) & 0x1fff);
  *(.text.lit_arena_allocate.lto_priv.0)
  . = . + ((0x06d0 - .) & 0x1fff);
  *(.text.question_idle.lto_priv.0)
  . = . + ((0x077c - .) & 0x1fff);
  *(.text.log_hit.lto_priv.0)
  . = . + ((0x1b98 - .) & 0x1fff);
  *(.text.col_soon.lto_priv.0)
  . = . + ((0x1c04 - .) & 0x1fff);
  *(.text.unlock_sms.lto_priv.0)
  . = . + ((0x1dcc - .) & 0x1fff);
  *libc.a:libc_a-svfiscanf.o(.text)
  . = . + ((0x0144 - .) & 0x1fff);
  *(.text.car_turn_grip)
  . = . + ((0x028c - .) & 0x1fff);
  *libc.a:libc_a-rewind.o(.text)
  . = . + ((0x0300 - .) & 0x1fff);
  *(.text.dist2)
  . = . + ((0x0350 - .) & 0x1fff);
  *(.text.garage_part_colour)
  . = . + ((0x0440 - .) & 0x1fff);
  *libc.a:libc_a-malignr.o(.text)
  . = . + ((0x05ec - .) & 0x1fff);
  *libc.a:libc_a-fread.o(.text)
  . = . + ((0x0980 - .) & 0x1fff);
  *(.text.nos_strength)
  . = . + ((0x0a14 - .) & 0x1fff);
  *(.text.fpu_quiet)
  . = . + ((0x0a44 - .) & 0x1fff);
  *libc.a:libc_a-fopen.o(.text)
  . = . + ((0x0bf4 - .) & 0x1fff);
  *(.text.playing.isra.0)
  . = . + ((0x0c0c - .) & 0x1fff);
  *(.text.largest_block.lto_priv.0)
  . = . + ((0x0c98 - .) & 0x1fff);
  *(.text.garage_bulb_colour)
  . = . + ((0x0d58 - .) & 0x1fff);
  *libc.a:libc_a-setenv_r.o(.text)
  . = . + ((0x10cc - .) & 0x1fff);
  *(.text.vinyl_table.lto_priv.0)
  . = . + ((0x1384 - .) & 0x1fff);
  *libc.a:libc_a-fmemopen.o(.text)
  . = . + ((0x18dc - .) & 0x1fff);
  *(.text.records_load.lto_priv.0)
  . = . + ((0x194c - .) & 0x1fff);
  *(.text.line_malloc.lto_priv.0)
  . = . + ((0x19b8 - .) & 0x1fff);
  *(.text.draw_opponents.lto_priv.0)
  . = . + ((0x1e6c - .) & 0x1fff);
  *libc.a:libc_a-strcmp.o(.text)
  . = . + ((0x00ac - .) & 0x1fff);
  *(.text.option_named.lto_priv.0)
  . = . + ((0x0370 - .) & 0x1fff);
  *(.text.output.constprop.0)
  . = . + ((0x08e8 - .) & 0x1fff);
  *(.text.freeze_watch_start.lto_priv.0)
  . = . + ((0x0a80 - .) & 0x1fff);
  *libc.a:libc_a-svfscanf.o(.text)
  . = . + ((0x0d74 - .) & 0x1fff);
  *(.text.sound_init.lto_priv.0)
  . = . + ((0x0064 - .) & 0x1fff);
  *(.text.flares_lamps_option)
  . = . + ((0x0800 - .) & 0x1fff);
  *(.text.moment_live)
  . = . + ((0x0cb0 - .) & 0x1fff);
  *(.text.awaken.lto_priv.0)
  . = . + ((0x0d84 - .) & 0x1fff);
  *(.text.nos_start)
  . = . + ((0x0db0 - .) & 0x1fff);
  *(.text.boot_report.lto_priv.0)
  . = . + ((0x0ee0 - .) & 0x1fff);
  *(.text.hot.bayer_alpha_rect.lto_priv.0)
  . = . + ((0x14cc - .) & 0x1fff);
  *(.text.hot.fe_alpha)
  . = . + ((0x151c - .) & 0x1fff);
  *(.text.give.lto_priv.0)
  . = . + ((0x153c - .) & 0x1fff);
  *(.text.hot.end2d.lto_priv.0)
  . = . + ((0x1580 - .) & 0x1fff);
  *libc.a:libc_a-remove.o(.text)
  . = . + ((0x15dc - .) & 0x1fff);
  *(.text.hit_body)
  . = . + ((0x173c - .) & 0x1fff);
  *(.text.tele_thread.lto_priv.0)
  . = . + ((0x17c0 - .) & 0x1fff);
  *(.text.intro_view.lto_priv.0)
  . = . + ((0x18d0 - .) & 0x1fff);
  *(.text.mix)
  . = . + ((0x15d8 - .) & 0x1fff);
  *(.text.fpu_thread)
  . = . + ((0x16d8 - .) & 0x1fff);
  *libc.a:libc_a-strtol.o(.text)
  . = . + ((0x1940 - .) & 0x1fff);
  *(.text.garage_skin_small)
  . = . + ((0x09cc - .) & 0x1fff);
  *(.text.st_window)
  . = . + ((0x0afc - .) & 0x1fff);
  *(.text.audio_thread.lto_priv.0)
  . = . + ((0x0e80 - .) & 0x1fff);
  *(.text.box_support.lto_priv.0)
  . = . + ((0x1084 - .) & 0x1fff);
  *(.text.new_set.lto_priv.0)
  . = . + ((0x1204 - .) & 0x1fff);
  *(.text.vinyl_read.lto_priv.0)
  . = . + ((0x15e0 - .) & 0x1fff);
  *(.text.trigfx_room)
  . = . + ((0x1694 - .) & 0x1fff);
  *(.text.challenge_on.lto_priv.0)
  . = . + ((0x1708 - .) & 0x1fff);
  *(.text.hot.posted)
  . = . + ((0x17a0 - .) & 0x1fff);
  *(.text.probe_mark)
  . = . + ((0x1894 - .) & 0x1fff);
  *(.text.stream_read.lto_priv.0)
  . = . + ((0x19c4 - .) & 0x1fff);
  *(.text.state_off.lto_priv.0)
  . = . + ((0x1a00 - .) & 0x1fff);
  *(.text.pack_read_at.constprop.0)
  . = . + ((0x1b00 - .) & 0x1fff);
  *libc.a:libc_a-strncmp.o(.text)
  . = . + ((0x1bc8 - .) & 0x1fff);
  *(.text.pack_read_at)
  . = . + ((0x1d00 - .) & 0x1fff);
  *(.text.gather)
  *(.text .stub .text.* .gnu.linkonce.t.*)
  . = ALIGN(0x2000) + 0x42c;
}
