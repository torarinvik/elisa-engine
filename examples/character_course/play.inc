# Interactive play loop, HUD text and the rebind menu, included in the
# CharacterCourse private section.
        # Sixteen rows cover keys, accessibility, pads and devices. The
        # panel backs the HUD in high contrast; `caption` names sound events.
        struct Legend:
            panel: mutable RenderScene::OverlayPanelHandle
            header: mutable RenderScene::OverlayTextHandle
            labels: mutable RenderScene::OverlayTextHandle[16]
            keys: mutable RenderScene::OverlayTextHandle[16]
            caption: mutable RenderScene::OverlayTextHandle
            words: CourseText::Captions

        def status_text(state: CourseState::State&, crouched: bool) -> cstr:
            if CourseState::phase(state) == CourseState::Phase.Paused:
                "Paused | P resume | Tab controls | R restart | Q save | L load | Esc quit"
            elif CourseState::phase(state) == CourseState::Phase.Won:
                "Summit! | R replay | Esc quit"
            elif CourseState::phase(state) == CourseState::Phase.Failed:
                "You fell! | R retry | Esc quit"
            elif crouched:
                "Crouched | Arrows also move | P pause | R restart"
            else:
                "Arrows also move | P pause | R restart | Q save | L load"

        def refresh_status(handle: RenderScene::OverlayTextHandle,
            state: CourseState::State&, crouched: bool) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text(handle, status_text(state, crouched))

        # A failed restart should leave the game running with a status message.
        # Character creation is transactional, so allocation failure preserves
        # the current attempt.
        def restart_live(session: mutable RuntimeServices::Session&,
            character: mutable PhysicsRuntime::CharacterHandle&,
            state: mutable CourseState::State&, sounds: mutable CourseSounds::Sounds&,
            beacons: mutable CourseBeacons::Beacons&, grounded: mutable bool&,
            crouched: mutable bool&, jump_queued: mutable bool&,
            clock: mutable Runtime::StepClock&, previous_elapsed_nanos: mutable u64&,
            elapsed_nanos: u64, avatar: RenderScene::InstanceHandle,
            instruction: RenderScene::OverlayTextHandle,
            look: CourseLook::Look) -> bool:
            can Global.Read, Global.Write:
                restarted: PhysicsRuntime::CharacterHandle = catch restart_character(session, character):
                    value: value
                    error failure: return false
                character <- restarted
                CourseState::restart(state)
                CourseSounds::reset_scene(sounds)
                _ = CourseSounds::restart_music(sounds)
                _ = CourseSounds::set_paused(sounds, false)
                beacon_status: i32 = catch CourseBeacons::reset(beacons):
                    value: value
                    error failure: return false
                return false if beacon_status != 0
                crouched <- false
                jump_queued <- false
                clock <- Runtime::StepClock()
                previous_elapsed_nanos <- elapsed_nanos
                stepped: bool = catch RuntimeServices::physics_fixed_step(session,
                    Runtime::fixed_step_seconds()):
                    value: true
                    error failure: false
                return false if not stepped
                ground: PhysicsRuntime::CharacterGround = catch RuntimeServices::physics_character_ground(session, character):
                    value: value
                    error failure: return false
                grounded <- ground.state == PhysicsRuntime::CharacterGroundState.OnGround
                pose: PhysicsRuntime::BodyPose = catch RuntimeServices::physics_character_pose(session, character):
                    value: value
                    error failure: return false
                avatar_updated: bool = catch update_avatar(avatar, pose, crouched):
                    ok: true
                    error failure: false
                return false if not avatar_updated
                camera_updated: bool = catch update_camera_look(pose, look):
                    ok: true
                    error failure: false
                return false if not camera_updated
                status_updated: bool = catch refresh_status(instruction, state, crouched):
                    ok: true
                    error failure: false
                return false if not status_updated
                return true

        def load_message(status: CourseProgress::LoadStatus) -> cstr:
            if status == CourseProgress::LoadStatus.Missing:
                "No checkpoint saved yet | Q save"
            elif status == CourseProgress::LoadStatus.Unreadable:
                "Checkpoint file is unreadable; current run kept"
            else:
                "Checkpoint rejected; current run kept"

        def beacon_message(lit: usize) -> cstr:
            if lit >= CourseBeacons::COUNT:
                "All beacons lit (3/3) | Q saves them"
            elif lit == 2:
                "Beacon lit (2/3)"
            else:
                "Beacon lit (1/3)"

        def menu_hint() -> cstr:
            "Up/Down select | click or type | Tab done"

        def legend_header(menu_open: bool) -> cstr:
            "Rebinding (Esc/Tab closes)" if menu_open else "Controls | Tab while paused to rebind"

        def legend_key_offset(settings: CourseAccess::Settings&) -> f32:
            CourseAccess::scaled(settings, Tune::LEGEND_KEY_X - Tune::LEGEND_X)

        def row_label(row: i64) -> cstr:
            if row < CourseControls::SLOT_COUNT:
                CourseControls::slot_name(row)
            elif row < CourseControls::PAD_FIRST:
                CourseAccess::row_name(row - CourseControls::SLOT_COUNT)
            elif row < CourseControls::DEVICE_FIRST:
                CoursePad::slot_name(row - CourseControls::PAD_FIRST)
            else:
                CourseDeviceMenu::row_name(row - CourseControls::DEVICE_FIRST)

        def row_key(keymap: CourseControls::Keymap&, pads: CoursePad::Pad&, settings: CourseAccess::Settings&, row: i64) -> cstr:
            if row < CourseControls::SLOT_COUNT:
                CourseControls::key_name(CourseControls::slot_code(keymap, row))
            elif row < CourseControls::PAD_FIRST:
                CourseAccess::row_value(settings, row - CourseControls::SLOT_COUNT)
            elif row >= CourseControls::DEVICE_FIRST:
                "< Left/Right >"
            else:
                CoursePad::button_name(CoursePad::slot_code(pads, row - CourseControls::PAD_FIRST))

        # Every menu row as a semantic control: six key bindings, the
        # text-size slider, three toggles, and the language and pad rows,
        # which cycle like sliders.
        def row_semantics(keymap: CourseControls::Keymap&, pads: CoursePad::Pad&, settings: CourseAccess::Settings&, row: i64, selected: i64) -> UiSemantics::Control error[UiSemantics::SemanticsError]:
            role: mutable UiSemantics::Role = UiSemantics::Role.KeyBinding
            if row >= CourseControls::PAD_FIRST:
                role <- UiSemantics::Role.Slider
            elif row >= CourseControls::SLOT_COUNT:
                access_row: i64 = row - CourseControls::SLOT_COUNT
                toggle: bool = access_row == CourseAccess::ROW_CONTRAST or access_row == CourseAccess::ROW_CAPTIONS or access_row == CourseAccess::ROW_MOTION
                role <- UiSemantics::Role.Toggle if toggle else UiSemantics::Role.Slider
            try UiSemantics::Control(role, row_label(row), row_key(keymap, pads, settings, row), row, CourseControls::MENU_ROWS, row == selected)

        # Speaks the focused row to a screen reader. Device rows carry their
        # listing in the legend text, not in row_key, so only the name is read.
        def announce_row(keymap: CourseControls::Keymap&, pads: CoursePad::Pad&, settings: CourseAccess::Settings&, row: i64) -> ApplicationAccessibility::Announce can[Global.Read, Global.Write]:
            return ApplicationAccessibility::Announce.Invalid if row < 0 or row >= CourseControls::MENU_ROWS
            control: UiSemantics::Control = catch row_semantics(keymap, pads, settings, row, row):
                value: value
                # An empty label is refused by the host.
                error failure: UiSemantics::Control{role: UiSemantics::Role.Label, label: "", value: "", index: 0, count: 1, selected: false, enabled: false}
            value: cstr = "" if row >= CourseControls::DEVICE_FIRST else control.value
            ApplicationAccessibility::announce(control.label, value, UiSemantics::role_name(control.role),
                UiSemantics::position(&control), control.count, ApplicationAccessibility::Priority.Medium)

        def legend_row_y(settings: CourseAccess::Settings&, row: i64) -> f32:
            Tune::HUD_Y + CourseAccess::scaled(settings, Tune::LEGEND_Y - Tune::HUD_Y + (row + 1).f32() * Tune::LEGEND_ROW_HEIGHT)

        # The legend row under a pointer in logical window units, or -1. A row
        # spans both columns and one scaled row height below its top edge.
        def legend_hit(settings: CourseAccess::Settings&, x: f32, y: f32) -> i64:
            right: f32 = Tune::LEGEND_X + legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH)
            return -1 if x < Tune::LEGEND_X or x >= right
            height: f32 = CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT)
            scale: CourseAccess::Settings = settings
            hit: mutable i64 = -1
            for row in 0..<CourseControls::MENU_ROWS |hit, scale, height, y|:
                top: f32 = legend_row_y(&scale, row)
                hit <- row if y >= top and y < top + height
            hit

        # Presses hit-test the legend and the wheel moves the selection.
        def menu_pointer_event(menu: CourseControls::Menu, settings: CourseAccess::Settings&,
            event: Application::PointerEvent) -> CourseControls::MenuStep can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                if event.kind == Application::PointerEventKind.Button and event.pressed:
                    return CourseControls::menu_pointer(menu, legend_hit(settings, event.x, event.y), event.button)
                elif event.kind == Application::PointerEventKind.Wheel:
                    return CourseControls::menu_wheel(menu, event.delta_y)
                else:
                    return CourseControls::step(menu, CourseControls::MenuResult.Ignored)

            # Like `drain_menu`: stops at Adjusted so each change is applied first.
        def drain_menu_pointer(menu: CourseControls::Menu, settings: CourseAccess::Settings&, height: i32) -> CourseControls::MenuStep can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                current: mutable CourseControls::MenuStep = CourseControls::step(menu, CourseControls::MenuResult.Ignored)
                open: mutable bool = true
                scale: CourseAccess::Settings = settings
                visible: i64 = legend_visible_rows(settings, height)
                first: i64 = CourseMenuView::first(menu.selected, visible)
                event: mutable Application::PointerEvent = zeroed
                while open and Application::next_pointer_event(&event) |current, open, scale, event, first, visible|:
                    next: mutable CourseControls::MenuStep = menu_pointer_event(current.menu, &scale, event)
                    if event.kind == Application::PointerEventKind.Button and event.pressed:
                        slot: i64 = legend_hit(&scale, event.x, event.y)
                        next <- CourseControls::menu_pointer(current.menu, CourseMenuView::hit(first, visible, slot), event.button)
                    if next.result != CourseControls::MenuResult.Ignored:
                        current <- next
                    open <- false if next.result == CourseControls::MenuResult.Adjusted
                return current

            # The panel is created first so the text draws over it. Handles are
            # gathered in locals; element writes into a struct field inside a
            # captured loop were lost.
        def create_legend(keymap: CourseControls::Keymap&, pads: CoursePad::Pad&, settings: CourseAccess::Settings&) -> Legend error[RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                panel: RenderScene::OverlayPanelHandle = try RenderScene::create_overlay_panel(Tune::LEGEND_X, Tune::LEGEND_Y, 1.0, 1.0, CourseAccess::panel_color(settings))
                header: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(legend_header(false),
                    Tune::LEGEND_X, Tune::LEGEND_Y, Tune::LEGEND_FONT_SIZE,
                    box_color(0.78, 0.84, 0.9))
                labels: mutable RenderScene::OverlayTextHandle[16] = zeroed
                keys: mutable RenderScene::OverlayTextHandle[16] = zeroed
                map: CourseControls::Keymap = keymap
                pad_map: CoursePad::Pad = pads
                values: CourseAccess::Settings = settings
                for row in 0..<CourseControls::MENU_ROWS |labels, keys, map, pad_map, values|:
                    label: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(
                        row_label(row), Tune::LEGEND_X, Tune::LEGEND_Y,
                        Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                    key: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(
                        row_key(&map, &pad_map, &values, row), Tune::LEGEND_KEY_X, Tune::LEGEND_Y,
                        Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                    labels[row.usize()] <- label
                    keys[row.usize()] <- key
                caption: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(CourseAccess::caption_text(0),
                    Tune::LEGEND_X, Tune::LEGEND_Y, Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                try RenderScene::set_overlay_text_visible(caption, false)
                legend: Legend = Legend{panel: panel, header: header, labels: labels, keys: keys, caption: caption,
                    words: CourseText::load(CourseText::PATH)}
                try layout_legend(&legend, settings)
                try refresh_legend(&legend, keymap, pads, settings, -1)
                return legend

            # The caption line spans the label and key columns.
        def caption_width(settings: CourseAccess::Settings&) -> f32:
            legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH)

        # The caption for `event` broken into lines that fit the caption line.
        def wrapped_caption(words: CourseText::Captions&, settings: CourseAccess::Settings&, event: u32) -> UiLocaleData::Utf8:
            em: i64 = CourseAccess::font_size(settings, Tune::LEGEND_FONT_SIZE).i64()
            wrapped: UiLocaleData::Utf8 = UiLocaleData::wrap_utf8(CourseText::line(words, settings.locale, event), em, caption_width(settings).i64())
            # The overlay draws bytes left to right, so lines go in display order.
            UiBidiUtf8::bidi_utf8_lines(wrapped, UiLocale::is_rtl(settings.locale))

        # Right-to-left captions hang from the right edge of the legend.
        def caption_anchor_x(settings: CourseAccess::Settings&) -> f32:
            Tune::LEGEND_X + caption_width(settings) if UiLocale::is_rtl(settings.locale) else Tune::LEGEND_X

        # Text size moves every row and the key column; high contrast shows
        # the backing panel. The caption line sits below the last row.
        def layout_legend(legend: Legend&, settings: CourseAccess::Settings&) -> void error[RenderScene::RenderSceneError]:
            size: i32 = CourseAccess::font_size(settings, Tune::LEGEND_FONT_SIZE)
            key_x: f32 = Tune::LEGEND_X + legend_key_offset(settings)
            try RenderScene::set_overlay_text_size(legend.header, size)
            try RenderScene::set_overlay_text_position(legend.header, Tune::LEGEND_X, legend_row_y(settings, -1))
            try RenderScene::set_overlay_text_color(legend.header, CourseAccess::header_color(settings))
            # Fields of a forwarded `Legend&` are copied out before the loop.
            labels: RenderScene::OverlayTextHandle[16] = legend.labels
            keys: RenderScene::OverlayTextHandle[16] = legend.keys
            scale: CourseAccess::Settings = settings
            for row in 0..<CourseControls::MENU_ROWS |labels, keys, scale, size, key_x|:
                row_y: f32 = legend_row_y(&scale, row)
                label: RenderScene::OverlayTextHandle = labels[row.usize()]
                key: RenderScene::OverlayTextHandle = keys[row.usize()]
                try RenderScene::set_overlay_text_size(label, size)
                try RenderScene::set_overlay_text_position(label, Tune::LEGEND_X, row_y)
                try RenderScene::set_overlay_text_size(key, size)
                try RenderScene::set_overlay_text_position(key, key_x, row_y)
            caption_y: f32 = legend_row_y(settings, CourseControls::MENU_ROWS)
            try RenderScene::set_overlay_text_size(legend.caption, size)
            try RenderScene::set_overlay_text_position(legend.caption, caption_anchor_x(settings), caption_y)
            if UiLocale::is_rtl(settings.locale):
                try RenderScene::set_overlay_text_alignment(legend.caption, RenderScene::OverlayTextAlignment.Right, RenderScene::OverlayTextAlignment.Top)
            else:
                try RenderScene::set_overlay_text_alignment(legend.caption, RenderScene::OverlayTextAlignment.Left, RenderScene::OverlayTextAlignment.Top)
            try RenderScene::set_overlay_text_color(legend.caption, CourseAccess::caption_color(settings))
            margin: f32 = CourseAccess::scaled(settings, Tune::LEGEND_PANEL_MARGIN)
            try RenderScene::set_overlay_panel_position(legend.panel, Tune::LEGEND_X - margin, legend_row_y(settings, -1) - margin)
            try RenderScene::set_overlay_panel_size(legend.panel, legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH) + margin * 2.0,
                caption_y - legend_row_y(settings, -1) + CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT) + margin * 2.0)
            try RenderScene::set_overlay_panel_visible(legend.panel, settings.high_contrast)

        # Reserve the header, caption and bottom margin without shrinking text.
        def legend_visible_rows(settings: CourseAccess::Settings&, height: i32) -> i64:
            row_height: f32 = CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT)
            available: f32 = height.f32() - legend_row_y(settings, 0) - row_height - 18.0
            CourseMenuView::count((available / row_height).i64())

        def fit_legend(legend: Legend&, settings: CourseAccess::Settings&, height: i32, selected: i64) -> void error[RenderScene::RenderSceneError]:
            visible: i64 = legend_visible_rows(settings, height)
            first: i64 = CourseMenuView::first(selected, visible)
            labels: RenderScene::OverlayTextHandle[16] = legend.labels
            keys: RenderScene::OverlayTextHandle[16] = legend.keys
            scale: CourseAccess::Settings = settings
            key_x: f32 = Tune::LEGEND_X + legend_key_offset(settings)
            for row in 0..<CourseControls::MENU_ROWS |labels, keys, scale, visible, first, key_x|:
                shown: bool = row >= first and row < first + visible
                try RenderScene::set_overlay_text_visible(labels[row.usize()], shown)
                try RenderScene::set_overlay_text_visible(keys[row.usize()], shown)
                if shown:
                    y: f32 = legend_row_y(&scale, row - first)
                    try RenderScene::set_overlay_text_position(labels[row.usize()], Tune::LEGEND_X, y)
                    try RenderScene::set_overlay_text_position(keys[row.usize()], key_x, y)
            caption_y: f32 = legend_row_y(settings, visible)
            try RenderScene::set_overlay_text_position(legend.caption, caption_anchor_x(settings), caption_y)
            margin: f32 = CourseAccess::scaled(settings, Tune::LEGEND_PANEL_MARGIN)
            try RenderScene::set_overlay_panel_size(legend.panel, legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH) + margin * 2.0,
                caption_y - legend_row_y(settings, -1) + CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT) + margin * 2.0)

        # The instruction line scales and recolours with the legend.
        def layout_hud(legend: Legend&, instruction: RenderScene::OverlayTextHandle, settings: CourseAccess::Settings&) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text_size(instruction, CourseAccess::font_size(settings, Tune::HUD_FONT_SIZE))
            try RenderScene::set_overlay_text_color(instruction, CourseAccess::text_color(settings))
            try layout_legend(legend, settings)

        # `selected` is the highlighted row, or -1 outside the menu.
        def refresh_legend(legend: Legend&, keymap: CourseControls::Keymap&, pads: CoursePad::Pad&,
            settings: CourseAccess::Settings&, selected: i64) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text(legend.header, legend_header(selected >= 0))
            highlight: RenderScene::Color = CourseAccess::highlight_color(settings)
            plain: RenderScene::Color = CourseAccess::text_color(settings)
            labels: RenderScene::OverlayTextHandle[16] = legend.labels
            keys: RenderScene::OverlayTextHandle[16] = legend.keys
            map: CourseControls::Keymap = keymap
            pad_map: CoursePad::Pad = pads
            values: CourseAccess::Settings = settings
            for row in 0..<CourseControls::MENU_ROWS |labels, keys, map, pad_map, values, selected, highlight, plain|:
                color: RenderScene::Color = highlight if row == selected else plain
                label: RenderScene::OverlayTextHandle = labels[row.usize()]
                key: RenderScene::OverlayTextHandle = keys[row.usize()]
                try RenderScene::set_overlay_text(key, row_key(&map, &pad_map, &values, row))
                try RenderScene::set_overlay_text_color(label, color)
                try RenderScene::set_overlay_text_color(key, color)

        # Returns the event now on screen (0 when hidden), touching the text
        # only when that changes; pass `shown` 0 to redraw the same
        # event (the Language row changes its text, not its visibility).
        def show_caption(legend: Legend&, captions: CourseAccess::Captions&, settings: CourseAccess::Settings&,
            now_nanos: u64, shown: u32) -> u32 error[RenderScene::RenderSceneError]:
            wanted: u32 = captions.event if CourseAccess::caption_visible(captions, settings, now_nanos) else 0
            return shown if wanted == shown
            if wanted != 0 and legend.words.loaded:
                line: UiLocaleData::Utf8 = wrapped_caption(&legend.words, settings, wanted)
                try RenderScene::set_overlay_text_utf8(legend.caption, &line.bytes[0], line.length)
            elif wanted != 0:
                try RenderScene::set_overlay_text(legend.caption, CourseAccess::caption_text(wanted))
            try RenderScene::set_overlay_text_visible(legend.caption, wanted != 0)
            wanted

        # The device rows show live listings, so they are drawn apart from
        # `row_key`: the display number, and the output name or default.
        def refresh_device_rows(legend: Legend&, picks: CourseDeviceMenu::Picks&) -> void error[RenderScene::RenderSceneError]:
            keys: RenderScene::OverlayTextHandle[16] = legend.keys
            display: RenderScene::OverlayTextHandle = keys[(CourseControls::DEVICE_FIRST + CourseDeviceMenu::ROW_DISPLAY).usize()]
            output: RenderScene::OverlayTextHandle = keys[(CourseControls::DEVICE_FIRST + CourseDeviceMenu::ROW_OUTPUT).usize()]
            try RenderScene::set_overlay_text_i64(display, "< Display ", picks.display + 1)
            name: AudioDevices::Device = CourseDeviceMenu::output_label(picks)
            if name.name_count == 0:
                try RenderScene::set_overlay_text(output, "< System default >")
            else:
                try RenderScene::set_overlay_text_utf8(output, &name.name[0], name.name_count.u32())

        def release_legend(legend: Legend&) -> void error[RenderScene::RenderSceneError]:
            labels: RenderScene::OverlayTextHandle[16] = legend.labels
            keys: RenderScene::OverlayTextHandle[16] = legend.keys
            for row in 0..<CourseControls::MENU_ROWS |labels, keys|:
                label: RenderScene::OverlayTextHandle = labels[row.usize()]
                key: RenderScene::OverlayTextHandle = keys[row.usize()]
                try RenderScene::destroy_overlay_text(label)
                try RenderScene::destroy_overlay_text(key)
            try RenderScene::destroy_overlay_text(legend.header)
            try RenderScene::destroy_overlay_text(legend.caption)
            try RenderScene::destroy_overlay_panel(legend.panel)
