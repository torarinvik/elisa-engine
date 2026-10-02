        # Configures the scene's physical sky and directional sunlight. Elisa
        # owns the environment choice; Wicked entities stay engine-owned.
        def set_environment(environment: Lighting::Environment) -> void error[RenderSceneError]:
            raise RenderSceneError.InvalidValue if not Lighting::environment_valid(environment)
            fog_enabled: i32 = 1 if environment.fog_enabled else 0
            try check_status(elisa_render_scene_v1_set_environment(
                environment.sun_direction.x, environment.sun_direction.y, environment.sun_direction.z,
                environment.sun_color.x, environment.sun_color.y, environment.sun_color.z,
                environment.ambient.x, environment.ambient.y, environment.ambient.z,
                environment.sky_exposure,
                environment.fog_color.x, environment.fog_color.y, environment.fog_color.z,
                environment.fog_start, environment.fog_density, fog_enabled))
        # Sky maps may be equirectangular 2:1 images or square cubemaps.
        # Asset paths stay inside the project root and use the color pipeline.
        def set_sky_map(asset_path: cstr, rotation_radians: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_sky_map(asset_path, rotation_radians))
        def clear_sky_map() -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_clear_sky_map())
        def create_light(light: Lighting::Light) -> LightHandle error[RenderSceneError]:
            raise RenderSceneError.InvalidValue if not Lighting::light_valid(light)
            kind: i32 = native_light_kind(light.kind)
            native_value: i64 = elisa_render_scene_v1_create_light(kind,
                light.color.x, light.color.y, light.color.z,
                light.position.x, light.position.y, light.position.z,
                light.direction.x, light.direction.y, light.direction.z,
                light.intensity, light.range, light.inner_cone, light.outer_cone,
                1 if light.casts_shadow else 0, light.shadow_resolution,
                light.rectangle_width, light.rectangle_height,
                1 if light.volumetrics_enabled else 0, light.volumetric_boost)
            if native_value <= 0:
                try check_status(native_value.i32())
                raise RenderSceneError.BackendFailure
            LightHandle{id: native_value}
        def update_light(handle: LightHandle, light: Lighting::Light) -> void error[RenderSceneError]:
            raise RenderSceneError.InvalidValue if not Lighting::light_valid(light)
            kind: i32 = native_light_kind(light.kind)
            try check_status(elisa_render_scene_v1_update_light(handle.id, kind,
                light.color.x, light.color.y, light.color.z,
                light.position.x, light.position.y, light.position.z,
                light.direction.x, light.direction.y, light.direction.z,
                light.intensity, light.range, light.inner_cone, light.outer_cone,
                1 if light.casts_shadow else 0, light.shadow_resolution,
                light.rectangle_width, light.rectangle_height,
                1 if light.volumetrics_enabled else 0, light.volumetric_boost))
        def destroy_light(handle: LightHandle) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_destroy_light(handle.id))
        def set_bloom(enabled: bool, threshold: f32) -> void error[RenderSceneError]:
            enabled_value: i32 = 1 if enabled else 0
            try check_status(elisa_render_scene_v1_set_bloom(enabled_value, threshold))
        # Screen-space ambient occlusion adds contact shading around feet,
        # fence bases, and other nearby surfaces.
        def set_ambient_occlusion(enabled: bool) -> void error[RenderSceneError]:
            enabled_value: i32 = 1 if enabled else 0
            try check_status(elisa_render_scene_v1_set_ambient_occlusion(enabled_value))
        # Range is measured in world units; power controls contact contrast.
        # Both values are bounded so projects cannot destabilize the renderer.
        def set_ambient_occlusion_settings(range: f32, power: f32) -> void error[RenderSceneError]:
            raise RenderSceneError.InvalidValue if range < 0.01 or range > 32.0
            raise RenderSceneError.InvalidValue if power < 0.0 or power > 8.0
            try check_status(elisa_render_scene_v1_set_ambient_occlusion_settings(range, power))
        # FXAA smooths thin geometry edges at the current render resolution.
        def set_fxaa(enabled: bool) -> void error[RenderSceneError]:
            enabled_value: i32 = 1 if enabled else 0
            try check_status(elisa_render_scene_v1_set_fxaa(enabled_value))
        def set_tonemap(tonemap: Tonemap) -> void error[RenderSceneError]:
            tonemap_value: i32 =
                match tonemap:
                    RenderScene::Tonemap.Reinhard: 0
                    RenderScene::Tonemap.Aces: 1
                    RenderScene::Tonemap.Uchimura: 2
            try check_status(elisa_render_scene_v1_set_tonemap(tonemap_value))
        # Exposure is a bounded scene-wide post-process multiplier. Keep it
        # separate from environment sky exposure so authored lighting remains
        # stable while a project tunes final readability.
        # LOD data is shared by instances. Call once on a static source mesh.
        # Simplification preserves material boundaries; animated assets are rejected.
        def generate_static_lods(source: InstanceHandle, levels: i32, ratio: f32,
            max_error: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_generate_static_lods(
                source.native_value, levels, ratio, max_error))

        # Returns the mode actually applied, including fallback on unsupported GPUs/shaders.
        def set_indirect_lighting(mode: IndirectLighting) -> IndirectLighting error[RenderSceneError]:
            requested: i32 =
                match mode:
                    IndirectLighting.Raster: NativeIndirectLighting::RASTER
                    IndirectLighting.ScreenSpace: NativeIndirectLighting::SCREEN_SPACE
                    IndirectLighting.RayTraced: NativeIndirectLighting::RAY_TRACED
            applied: i32 = elisa_render_scene_v1_set_indirect_lighting(requested)
            if applied < 0:
                try check_status(applied)
            return IndirectLighting.RayTraced if applied == NativeIndirectLighting::RAY_TRACED
            return IndirectLighting.ScreenSpace if applied == NativeIndirectLighting::SCREEN_SPACE
            IndirectLighting.Raster

        # Changes only 3D resolution; overlays retain native resolution.
        # target_fps=0 disables adaptation. Changes are bounded and spaced apart.
        def set_adaptive_resolution(target_fps: f32, minimum: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_adaptive_resolution(target_fps, minimum))

        def set_render_scale(scale: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_render_scale(scale))

        def set_exposure(exposure: f32) -> void error[RenderSceneError]:
            raise RenderSceneError.InvalidValue if exposure < 0.0 or exposure > 8.0
            try check_status(elisa_render_scene_v1_set_exposure(exposure))
        def set_shadow_quality(quality: Quality::ShadowQuality) -> void error[RenderSceneError]:
            quality_value: i32 =
                match quality:
                    Quality::ShadowQuality.Low: 0
                    Quality::ShadowQuality.Medium: 1
                    Quality::ShadowQuality.High: 2
            try check_status(elisa_render_scene_v1_set_shadow_quality(quality_value))
