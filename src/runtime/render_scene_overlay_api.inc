        def create_overlay_text(text: cstr, x: f32, y: f32, font_size: i32,
            color: Color) -> OverlayTextHandle error[RenderSceneError]:
            native_value: i64 = elisa_render_scene_v1_create_text(text, x, y, font_size,
                color.red, color.green, color.blue, color.alpha)
            if native_value <= 0:
                try check_status(native_value.i32())
                raise RenderSceneError.BackendFailure
            OverlayTextHandle{id: native_value}
        def set_overlay_text(handle: OverlayTextHandle, text: cstr) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text(handle.id, text))
        def set_overlay_text_i64(handle: OverlayTextHandle, prefix: cstr,
            value: i64) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text_i64(handle.id, prefix, value))
        def set_overlay_text_position(handle: OverlayTextHandle, x: f32,
            y: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text_position(handle.id, x, y))
        def set_overlay_text_size(handle: OverlayTextHandle,
            font_size: i32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text_size(handle.id, font_size))
        def set_overlay_text_color(handle: OverlayTextHandle,
            color: Color) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text_color(handle.id,
                color.red, color.green, color.blue, color.alpha))
        def set_overlay_text_visible(handle: OverlayTextHandle, visible: bool) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_text_visible(
                handle.id, 1 if visible else 0))
        def destroy_overlay_text(handle: OverlayTextHandle) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_destroy_text(handle.id))
        # Position is the panel's upper-left corner in the logical canvas.
        def create_overlay_panel(x: f32, y: f32, width: f32, height: f32,
            color: Color) -> OverlayPanelHandle error[RenderSceneError]:
            native_value: i64 = elisa_render_scene_v1_create_overlay_panel(x, y, width, height,
                color.red, color.green, color.blue, color.alpha)
            if native_value <= 0:
                try check_status(native_value.i32())
                raise RenderSceneError.BackendFailure
            OverlayPanelHandle{id: native_value}
        def set_overlay_panel_position(handle: OverlayPanelHandle, x: f32,
            y: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_overlay_panel_position(
                handle.id, x, y))
        def set_overlay_panel_size(handle: OverlayPanelHandle,
            width: f32, height: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_overlay_panel_size(
                handle.id, width, height))
        def set_overlay_panel_color(handle: OverlayPanelHandle,
            color: Color) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_overlay_panel_color(handle.id,
                color.red, color.green, color.blue, color.alpha))
        def set_overlay_panel_visible(handle: OverlayPanelHandle,
            visible: bool) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_overlay_panel_visible(
                handle.id, 1 if visible else 0))
        def destroy_overlay_panel(handle: OverlayPanelHandle) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_destroy_overlay_panel(handle.id))
        def create_electric_arc(width: f32, amplitude: f32, seed: u32) -> ElectricArcHandle error[RenderSceneError]:
            native_value: i64 = elisa_render_scene_v1_create_electric_arc(width, amplitude, seed)
            if native_value <= 0:
                try check_status(native_value.i32())
                raise RenderSceneError.BackendFailure
            ElectricArcHandle{native_value: native_value}
        def update_electric_arc(handle: ElectricArcHandle, start: Geometry::Vec3,
            end: Geometry::Vec3, phase: f32, visibility: f32) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_update_electric_arc(handle.native_value,
                start.x, start.y, start.z, end.x, end.y, end.z, phase, visibility))
        def set_electric_arc_depth_test(handle: ElectricArcHandle, enabled: bool) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_electric_arc_depth_test(
                handle.native_value, 1 if enabled else 0))
        def set_electric_arc_visible(handle: ElectricArcHandle, visible: bool) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_set_electric_arc_visible(
                handle.native_value, 1 if visible else 0))
        def destroy_electric_arc(handle: ElectricArcHandle) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_destroy_electric_arc(handle.native_value))
        def destroy(handle: InstanceHandle) -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_destroy(handle.native_value))
        def shutdown() -> void error[RenderSceneError]:
            try check_status(elisa_render_scene_v1_shutdown())
        def instance_count() -> usize:
            elisa_render_scene_v1_instance_count().usize()
        def is_initialized() -> bool:
            elisa_render_scene_v1_is_initialized() != 0
