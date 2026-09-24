        @callconv(c)
        extern elisa_render_scene_abi_version() -> u32
        @callconv(c)
        extern elisa_render_scene_v1_initialize(width: i32, height: i32, vertical_size: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_wait_for_pipelines(timeout_milliseconds: u32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_resize(width: i32, height: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_camera_render_target(width: i32, height: i32, update_interval: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_clear_camera_render_target() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_camera_orthographic_height(vertical_size: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_camera_perspective(vertical_fov_radians: f32, near_clip: f32, far_clip: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_camera_look_at(eye_x: f32, eye_y: f32, eye_z: f32,
            target_x: f32, target_y: f32, target_z: f32, up_x: f32, up_y: f32, up_z: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_camera_ray(screen_x: f32, screen_y: f32,
            origin_x: mutable f32&, origin_y: mutable f32&, origin_z: mutable f32&,
            direction_x: mutable f32&, direction_y: mutable f32&, direction_z: mutable f32&) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create_camera(projection: i32, width: i32, height: i32,
            projection_value: f32, near_clip: f32, far_clip: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_update_camera(handle: i64, projection: i32,
            width: i32, height: i32, projection_value: f32, near_clip: f32, far_clip: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_activate_camera(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_activate_default_camera() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy_camera(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_camera_viewport(handle: i64, x: i32, y: i32,
            width: i32, height: i32, update_interval: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_clear_camera_viewport(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_camera_viewport_ray(handle: i64,
            screen_x: f32, screen_y: f32,
            origin_x: mutable f32&, origin_y: mutable f32&, origin_z: mutable f32&,
            direction_x: mutable f32&, direction_y: mutable f32&, direction_z: mutable f32&) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create(primitive: i32, px: f32, py: f32, pz: f32,
            qx: f32, qy: f32, qz: f32, qw: f32, sx: f32, sy: f32, sz: f32,
            red: f32, green: f32, blue: f32, alpha: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_create_mesh(package_path: cstr,
            px: f32, py: f32, pz: f32, qx: f32, qy: f32, qz: f32, qw: f32,
            sx: f32, sy: f32, sz: f32, red: f32, green: f32, blue: f32, alpha: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_create_mesh_instance(source: i64,
            px: f32, py: f32, pz: f32,
            qx: f32, qy: f32, qz: f32, qw: f32,
            sx: f32, sy: f32, sz: f32,
            red: f32, green: f32, blue: f32, alpha: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_update_transform(handle: i64, px: f32, py: f32, pz: f32,
            qx: f32, qy: f32, qz: f32, qw: f32, sx: f32, sy: f32, sz: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_begin(previous_count: u32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_stage(existing_handle: i64,
            gameplay_epoch: i64, gameplay_id: i64, render_id: i64,
            mesh_high: u64, mesh_low: u64, material_high: u64, material_low: u64,
            px: f32, py: f32, pz: f32, qx: f32, qy: f32, qz: f32, qw: f32,
            sx: f32, sy: f32, sz: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_stage_tint(render_id: i64,
            red: f32, green: f32, blue: f32,
            emissive_red: f32, emissive_green: f32, emissive_blue: f32, emissive_strength: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_retire(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_commit() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_abort() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_snapshot_result(index: u32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_play_animation(handle: i64, clip_name: cstr,
            loop: i32, speed: f32, blend_seconds: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_play_animation_blended(handle: i64, clip_name: cstr,
            loop: i32, speed: f32, blend_seconds: f32, flags: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_animation_speed(handle: i64, speed: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_visibility_policy(handle: i64,
            draw_distance: f32, lod_bias: f32, layer_mask: u32, renderable: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_occlusion_culling(enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_stop_animation(handle: i64, blend_seconds: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_advance_animation(handle: i64, delta_seconds: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_animation_progress(handle: i64) -> f32
        @callconv(c)
        extern elisa_render_scene_v1_set_lit_material(handle: i64, roughness: f32, metallic: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_texture_uv_transform(handle: i64, scale_u: f32, scale_v: f32, offset_u: f32, offset_v: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_sun_shadows(enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_sun_shadow_bias(receiver_depth_bias: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_sun_cascade_distances(near_end: f32,
            middle_end: f32, far_end: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_color(handle: i64, red: f32, green: f32, blue: f32, alpha: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_alpha_mode(handle: i64, mode: i32, cutoff: f32, double_sided: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_cast_shadow(handle: i64, enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_texture(handle: i64, slot: i32, asset_path: cstr) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_emissive(handle: i64, red: f32, green: f32, blue: f32, strength: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_environment(
            sun_direction_x: f32, sun_direction_y: f32, sun_direction_z: f32,
            sun_red: f32, sun_green: f32, sun_blue: f32,
            ambient_red: f32, ambient_green: f32, ambient_blue: f32,
            sky_exposure: f32,
            fog_red: f32, fog_green: f32, fog_blue: f32,
            fog_start: f32, fog_density: f32, fog_enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_sky_map(asset_path: cstr, rotation_radians: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_clear_sky_map() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create_light(kind: i32,
            red: f32, green: f32, blue: f32,
            px: f32, py: f32, pz: f32, dx: f32, dy: f32, dz: f32,
            intensity: f32, range: f32, inner_cone: f32, outer_cone: f32,
            casts_shadow: i32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_update_light(handle: i64, kind: i32,
            red: f32, green: f32, blue: f32,
            px: f32, py: f32, pz: f32, dx: f32, dy: f32, dz: f32,
            intensity: f32, range: f32, inner_cone: f32, outer_cone: f32,
            casts_shadow: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy_light(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_bloom(enabled: i32, threshold: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_ambient_occlusion(enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_ambient_occlusion_settings(range: f32, power: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_fxaa(enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_tonemap(tonemap: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_exposure(exposure: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_shadow_quality(quality: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_visible(handle: i64, visible: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create_text(text: cstr, x: f32, y: f32,
            font_size: i32, red: f32, green: f32, blue: f32, alpha: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_set_text(handle: i64, text: cstr) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_text_i64(handle: i64, prefix: cstr, value: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_text_position(handle: i64, x: f32, y: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_text_size(handle: i64, font_size: i32) -> i32
        extern elisa_render_scene_v1_set_text_color(handle: i64,
            red: f32, green: f32, blue: f32, alpha: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_text_visible(handle: i64, visible: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy_text(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create_overlay_panel(x: f32, y: f32,
            width: f32, height: f32, red: f32, green: f32, blue: f32, alpha: f32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_set_overlay_panel_position(handle: i64, x: f32, y: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_overlay_panel_size(handle: i64, width: f32, height: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_overlay_panel_color(handle: i64,
            red: f32, green: f32, blue: f32, alpha: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_overlay_panel_visible(handle: i64, visible: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy_overlay_panel(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_create_electric_arc(width: f32, amplitude: f32, seed: u32) -> i64
        @callconv(c)
        extern elisa_render_scene_v1_update_electric_arc(handle: i64,
            start_x: f32, start_y: f32, start_z: f32,
            end_x: f32, end_y: f32, end_z: f32, phase: f32, visibility: f32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_electric_arc_depth_test(handle: i64, enabled: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_set_electric_arc_visible(handle: i64, visible: i32) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy_electric_arc(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_destroy(handle: i64) -> i32
        @callconv(c)
        extern elisa_render_scene_v1_shutdown() -> i32
        @callconv(c)
        extern elisa_render_scene_v1_instance_count() -> u64
        @callconv(c)
        extern elisa_render_scene_v1_is_initialized() -> i32
