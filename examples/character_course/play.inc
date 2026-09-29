# Interactive play loop, HUD text and the rebind menu, included in the
# CharacterCourse private section.
        # Nine rows: six key slots, then the three CourseAccess settings. The
        # panel backs the HUD in high contrast; `caption` names sound events.
        struct Legend:
            panel: mutable RenderScene::OverlayPanelHandle
            header: mutable RenderScene::OverlayTextHandle
            labels: mutable RenderScene::OverlayTextHandle[9]
            keys: mutable RenderScene::OverlayTextHandle[9]
            caption: mutable RenderScene::OverlayTextHandle

        def status_text(state: GameState&, crouched: bool) -> cstr:
            if state.phase == Phase.Paused:
                "Paused | P resume | Tab controls | R restart | Q save | L load | Esc quit"
            elif state.phase == Phase.Won:
                "Summit reached! | R play again | Esc quit"
            elif state.phase == Phase.Failed:
                "You fell! | R try again | Esc quit"
            elif crouched:
                "Crouched | Arrows also move | P pause | R restart"
            else:
                "Arrows also move | P pause | R restart | Q save | L load"

        def refresh_status(handle: RenderScene::OverlayTextHandle,
            state: GameState&, crouched: bool) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text(handle, status_text(state, crouched))

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
            "Menu: Up/Down or click a row | press a key, Left/Right or click | Tab done"

        def legend_header(menu_open: bool) -> cstr:
            "Rebinding (Esc/Tab closes)" if menu_open else "Controls | Tab while paused to rebind"

        def legend_key_offset(settings: CourseAccess::Settings&) -> f32:
            CourseAccess::scaled(settings, Tune::LEGEND_KEY_X - Tune::LEGEND_X)

        def row_label(row: i64) -> cstr:
            CourseControls::slot_name(row) if row < CourseControls::SLOT_COUNT else CourseAccess::row_name(row - CourseControls::SLOT_COUNT)

        def row_key(keymap: CourseControls::Keymap&, settings: CourseAccess::Settings&, row: i64) -> cstr:
            CourseControls::key_name(CourseControls::slot_code(keymap, row)) if row < CourseControls::SLOT_COUNT else CourseAccess::row_value(settings, row - CourseControls::SLOT_COUNT)

        # Every menu row as a semantic control: six key bindings, then the
        # text-size slider and the two toggles.
        def row_semantics(keymap: CourseControls::Keymap&, settings: CourseAccess::Settings&, row: i64, selected: i64) -> UiSemantics::Control error[UiSemantics::SemanticsError]:
            role: mutable UiSemantics::Role = UiSemantics::Role.KeyBinding
            if row >= CourseControls::SLOT_COUNT:
                role <- UiSemantics::Role.Slider if row - CourseControls::SLOT_COUNT == CourseAccess::ROW_TEXT_SIZE else UiSemantics::Role.Toggle
            try UiSemantics::Control(role, row_label(row), row_key(keymap, settings, row), row, CourseControls::MENU_ROWS, row == selected)

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
            event: Application::PointerEvent) -> CourseControls::MenuStep:
            if event.kind == Application::PointerEventKind.Button and event.pressed:
                CourseControls::menu_pointer(menu, legend_hit(settings, event.x, event.y), event.button)
            elif event.kind == Application::PointerEventKind.Wheel:
                CourseControls::menu_wheel(menu, event.delta_y)
            else:
                CourseControls::step(menu, CourseControls::MenuResult.Ignored)

        # Like `drain_menu`: stops at Adjusted so each change is applied first.
        def drain_menu_pointer(menu: CourseControls::Menu, settings: CourseAccess::Settings&) -> CourseControls::MenuStep:
            current: mutable CourseControls::MenuStep = CourseControls::step(menu, CourseControls::MenuResult.Ignored)
            open: mutable bool = true
            scale: CourseAccess::Settings = settings
            event: mutable Application::PointerEvent = zeroed
            while open and Application::next_pointer_event(&event) |current, open, scale, event|:
                next: CourseControls::MenuStep = menu_pointer_event(current.menu, &scale, event)
                if next.result != CourseControls::MenuResult.Ignored:
                    current <- next
                open <- false if next.result == CourseControls::MenuResult.Adjusted
            current

        # The panel is created first so the text draws over it. Handles are
        # gathered in locals; element writes into a struct field inside a
        # captured loop were lost.
        def create_legend(keymap: CourseControls::Keymap&, settings: CourseAccess::Settings&) -> Legend error[RenderScene::RenderSceneError]:
            panel: RenderScene::OverlayPanelHandle = try RenderScene::create_overlay_panel(Tune::LEGEND_X, Tune::LEGEND_Y, 1.0, 1.0, CourseAccess::panel_color(settings))
            header: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(legend_header(false),
                Tune::LEGEND_X, Tune::LEGEND_Y, Tune::LEGEND_FONT_SIZE,
                box_color(0.78, 0.84, 0.9))
            labels: mutable RenderScene::OverlayTextHandle[9] = zeroed
            keys: mutable RenderScene::OverlayTextHandle[9] = zeroed
            map: CourseControls::Keymap = keymap
            values: CourseAccess::Settings = settings
            for row in 0..<CourseControls::MENU_ROWS |labels, keys, map, values|:
                label: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(
                    row_label(row), Tune::LEGEND_X, Tune::LEGEND_Y,
                    Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                key: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(
                    row_key(&map, &values, row), Tune::LEGEND_KEY_X, Tune::LEGEND_Y,
                    Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                labels[row.usize()] <- label
                keys[row.usize()] <- key
            caption: RenderScene::OverlayTextHandle = try RenderScene::create_overlay_text(CourseAccess::caption_text(0),
                Tune::LEGEND_X, Tune::LEGEND_Y, Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
            try RenderScene::set_overlay_text_visible(caption, false)
            legend: Legend = Legend{panel: panel, header: header, labels: labels, keys: keys, caption: caption}
            try layout_legend(&legend, settings)
            try refresh_legend(&legend, keymap, settings, -1)
            legend

        # Text size moves every row and the key column; high contrast shows
        # the backing panel. The caption line sits below the last row.
        def layout_legend(legend: Legend&, settings: CourseAccess::Settings&) -> void error[RenderScene::RenderSceneError]:
            size: i32 = CourseAccess::font_size(settings, Tune::LEGEND_FONT_SIZE)
            key_x: f32 = Tune::LEGEND_X + legend_key_offset(settings)
            try RenderScene::set_overlay_text_size(legend.header, size)
            try RenderScene::set_overlay_text_position(legend.header, Tune::LEGEND_X, legend_row_y(settings, -1))
            try RenderScene::set_overlay_text_color(legend.header, CourseAccess::header_color(settings))
            # Fields of a forwarded `Legend&` are copied out before the loop.
            labels: RenderScene::OverlayTextHandle[9] = legend.labels
            keys: RenderScene::OverlayTextHandle[9] = legend.keys
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
            try RenderScene::set_overlay_text_position(legend.caption, Tune::LEGEND_X, caption_y)
            try RenderScene::set_overlay_text_color(legend.caption, CourseAccess::caption_color(settings))
            margin: f32 = CourseAccess::scaled(settings, Tune::LEGEND_PANEL_MARGIN)
            try RenderScene::set_overlay_panel_position(legend.panel, Tune::LEGEND_X - margin, legend_row_y(settings, -1) - margin)
            try RenderScene::set_overlay_panel_size(legend.panel, legend_key_offset(settings) + CourseAccess::scaled(settings, Tune::LEGEND_KEY_WIDTH) + margin * 2.0,
                caption_y - legend_row_y(settings, -1) + CourseAccess::scaled(settings, Tune::LEGEND_ROW_HEIGHT) + margin * 2.0)
            try RenderScene::set_overlay_panel_visible(legend.panel, settings.high_contrast)

        # The instruction line scales and recolours with the legend.
        def layout_hud(legend: Legend&, instruction: RenderScene::OverlayTextHandle, settings: CourseAccess::Settings&) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text_size(instruction, CourseAccess::font_size(settings, Tune::HUD_FONT_SIZE))
            try RenderScene::set_overlay_text_color(instruction, CourseAccess::text_color(settings))
            try layout_legend(legend, settings)

        # `selected` is the highlighted row, or -1 outside the menu.
        def refresh_legend(legend: Legend&, keymap: CourseControls::Keymap&,
            settings: CourseAccess::Settings&, selected: i64) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text(legend.header, legend_header(selected >= 0))
            highlight: RenderScene::Color = CourseAccess::highlight_color(settings)
            plain: RenderScene::Color = CourseAccess::text_color(settings)
            labels: RenderScene::OverlayTextHandle[9] = legend.labels
            keys: RenderScene::OverlayTextHandle[9] = legend.keys
            map: CourseControls::Keymap = keymap
            values: CourseAccess::Settings = settings
            for row in 0..<CourseControls::MENU_ROWS |labels, keys, map, values, selected, highlight, plain|:
                color: RenderScene::Color = highlight if row == selected else plain
                label: RenderScene::OverlayTextHandle = labels[row.usize()]
                key: RenderScene::OverlayTextHandle = keys[row.usize()]
                try RenderScene::set_overlay_text(key, row_key(&map, &values, row))
                try RenderScene::set_overlay_text_color(label, color)
                try RenderScene::set_overlay_text_color(key, color)

        # Returns the event now on screen (0 when hidden), touching the text
        # only when that changes.
        def show_caption(legend: Legend&, captions: CourseAccess::Captions&, settings: CourseAccess::Settings&,
            now_nanos: u64, shown: u32) -> u32 error[RenderScene::RenderSceneError]:
            wanted: u32 = captions.event if CourseAccess::caption_visible(captions, settings, now_nanos) else 0
            return shown if wanted == shown
            if wanted != 0:
                try RenderScene::set_overlay_text(legend.caption, CourseAccess::caption_text(wanted))
            try RenderScene::set_overlay_text_visible(legend.caption, wanted != 0)
            wanted

        def release_legend(legend: Legend&) -> void error[RenderScene::RenderSceneError]:
            labels: RenderScene::OverlayTextHandle[9] = legend.labels
            keys: RenderScene::OverlayTextHandle[9] = legend.keys
            for row in 0..<CourseControls::MENU_ROWS |labels, keys|:
                label: RenderScene::OverlayTextHandle = labels[row.usize()]
                key: RenderScene::OverlayTextHandle = keys[row.usize()]
                try RenderScene::destroy_overlay_text(label)
                try RenderScene::destroy_overlay_text(key)
            try RenderScene::destroy_overlay_text(legend.header)
            try RenderScene::destroy_overlay_text(legend.caption)
            try RenderScene::destroy_overlay_panel(legend.panel)

        def interactive_loop(session: mutable RuntimeServices::Session&,
            character: mutable PhysicsRuntime::CharacterHandle,
            avatar: RenderScene::InstanceHandle,
            instruction: RenderScene::OverlayTextHandle,
            guide: mutable Guide) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, Runtime::ClockError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            running: mutable bool = true
            crouched: mutable bool = false
            state: mutable GameState = GameState()
            jump_queued: mutable bool = false
            previous_elapsed_nanos: mutable u64 = 0
            resize_serial: mutable u64 = 0
            clock: mutable Runtime::StepClock = Runtime::StepClock()
            # Invalid saved controls fall back to the defaults; the file is only
            # replaced when the player closes the rebind menu.
            saved_controls: CourseControls::LoadResult = CourseControls::load()
            keymap: mutable CourseControls::Keymap = saved_controls.keymap
            input: mutable ActionInput::Input = ActionInput::Input()
            return 33 if not CourseControls::bind(&input, &keymap)
            # Accessibility settings load the same way and apply before the
            # first frame; they are saved with the controls on menu close.
            saved_access: CourseAccess::LoadResult = CourseAccess::load()
            settings: mutable CourseAccess::Settings = saved_access.settings
            legend: Legend = try create_legend(&keymap, &settings)
            try layout_hud(&legend, instruction, &settings)
            captions: mutable CourseAccess::Captions = CourseAccess::Captions()
            caption_shown: mutable u32 = 0
            menu_open: mutable bool = false
            menu: mutable CourseControls::Menu = CourseControls::Menu(keymap)
            # Sound is optional: a failed start releases what it loaded and
            # the course plays silently. `sound_tick` counts fixed steps.
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            if CourseSounds::start(&sounds, session) != CourseSounds::STARTED:
                _ = CourseSounds::stop(&sounds)
            sound_tick: mutable u64 = 0
            guide_agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
            rigs: Rigs = optional_rigs()
            walker: mutable Walker = optional_walker(session, &guide)
            grounded: mutable bool = true
            # Beacons light as the player passes; Q/L save and load their
            # prefab overrides alongside the checkpoint.
            beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                value: value
                error failure: return 34
            if saved_controls.status == CourseControls::LoadStatus.Rejected or saved_controls.status == CourseControls::LoadStatus.Unreadable:
                _ = try RenderScene::set_overlay_text(instruction,
                    "Saved controls were invalid; defaults restored")
            elif saved_access.status == CourseAccess::LoadStatus.Rejected or saved_access.status == CourseAccess::LoadStatus.Unreadable:
                _ = try RenderScene::set_overlay_text(instruction,
                    "Saved accessibility settings were invalid; defaults restored")
            else:
                _ = try refresh_status(instruction, state, crouched)
            while running |session, input, character, avatar, instruction, crouched, state, jump_queued, previous_elapsed_nanos, resize_serial, clock, keymap, legend, menu_open, menu, sounds, sound_tick, grounded, guide, guide_agent, rigs, walker, beacons, settings, captions, caption_shown|:
                pump: Application::PumpStatus = try RuntimeServices::pump(session)
                if pump == Application::PumpStatus.ExitRequested:
                    running <- false
                elif pump != Application::PumpStatus.Running and pump != Application::PumpStatus.Suspended:
                    return 40
                if running and pump == Application::PumpStatus.Running:
                    frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
                    mood: i32 = CourseSounds::mood_for(menu_open, state.phase == Phase.Paused)
                    _ = CourseSounds::mix(&sounds, mood, frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(state.phase == Phase.Won), frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::poll_reload(&sounds, frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::update(&sounds)
                    if menu_open:
                        # Keys first; pointer presses when no key acted.
                        step: mutable CourseControls::MenuStep = CourseControls::drain_menu(menu)
                        if step.result == CourseControls::MenuResult.Ignored:
                            step <- drain_menu_pointer(menu, settings)
                        menu <- step.menu
                        previous_elapsed_nanos <- frame.elapsed_nanos
                        if step.result == CourseControls::MenuResult.Closed:
                            # A fresh input map drops anything held while the
                            # menu was open, so no key leaks into gameplay.
                            menu_open <- false
                            keymap <- menu.keymap
                            rebound: mutable ActionInput::Input = ActionInput::Input()
                            return 33 if not CourseControls::bind(&rebound, &keymap)
                            input <- rebound
                            keys_stored: bool = catch CourseControls::save(&keymap):
                                ok: true
                                error failure: false
                            access_stored: bool = catch CourseAccess::save(&settings):
                                ok: true
                                error failure: false
                            stored: bool = keys_stored and access_stored
                            _ = try refresh_legend(legend, keymap, settings, -1)
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Controls saved | P resume" if stored else "Controls not saved; kept until quit | P resume")
                        elif step.result == CourseControls::MenuResult.Reserved:
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Reserved key | Esc P R Q L Tab and arrows stay fixed")
                        elif step.result == CourseControls::MenuResult.Adjusted:
                            settings <- CourseAccess::adjust(settings, menu.selected - CourseControls::SLOT_COUNT, step.delta)
                            _ = try layout_hud(legend, instruction, settings)
                            _ = try refresh_legend(legend, menu.keymap, settings, menu.selected)
                            caption_shown <- try show_caption(legend, captions, settings, frame.elapsed_nanos, caption_shown)
                        elif step.result != CourseControls::MenuResult.Ignored:
                            _ = try refresh_legend(legend, menu.keymap, settings, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                    else:
                        _ = CourseControls::discard_pointer()
                        focus_lost: bool = CourseControls::drain_gameplay(input)
                        if focus_lost and state.phase == Phase.Playing:
                            toggle_pause(&state)
                            Runtime::set_paused(&clock, true)
                            _ = CourseSounds::set_paused(&sounds, true)
                            jump_queued <- false
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Paused: window lost focus | P resume | Tab controls")
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_QUIT):
                            running <- false
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_CONTROLS) and state.phase == Phase.Paused:
                            menu_open <- true
                            menu <- CourseControls::Menu(keymap)
                            _ = try refresh_legend(legend, keymap, settings, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_RESTART):
                            character <- try restart_character(session, character)
                            restart(&state)
                            CourseSounds::reset_scene(&sounds)
                            _ = CourseSounds::restart_music(&sounds)
                            _ = CourseSounds::set_paused(&sounds, false)
                            _ = try CourseBeacons::reset(&beacons)
                            grounded <- true
                            crouched <- false
                            jump_queued <- false
                            clock <- Runtime::StepClock()
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                            _ = try update_avatar(avatar, pose, crouched)
                            _ = try update_camera(pose)
                            _ = try refresh_status(instruction, state, crouched)
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_SAVE):
                            saved_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                            checkpoint: CourseProgress::Checkpoint = checkpoint_of(state, saved_pose, crouched)
                            checkpoint_stored: bool = catch CourseProgress::save(&checkpoint):
                                ok: true
                                error failure: false
                            stored: bool = checkpoint_stored and CourseBeacons::save(beacons)
                            if stored:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_SAVE, frame.elapsed_nanos)
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Checkpoint saved | L load" if stored else "Checkpoint could not be saved")
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_LOAD):
                            loaded: CourseProgress::LoadResult = CourseProgress::load()
                            if loaded.status == CourseProgress::LoadStatus.Loaded:
                                character <- try apply_checkpoint(session, character, loaded.checkpoint)
                                beacons_loaded: i32 = try CourseBeacons::load(&beacons)
                                state.phase <- phase_from_code(loaded.checkpoint.phase)
                                state.attempts <- loaded.checkpoint.attempts
                                crouched <- loaded.checkpoint.crouched
                                jump_queued <- false
                                clock <- Runtime::StepClock()
                                Runtime::set_paused(&clock, state.phase != Phase.Playing)
                                CourseSounds::reset_scene(&sounds)
                                _ = CourseSounds::set_paused(&sounds, state.phase == Phase.Paused)
                                grounded <- false
                                previous_elapsed_nanos <- frame.elapsed_nanos
                                loaded_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                                _ = try update_avatar(avatar, loaded_pose, crouched)
                                _ = try update_camera(loaded_pose)
                                _ = try refresh_status(instruction, state, crouched)
                                if beacons_loaded != 0:
                                    _ = try RenderScene::set_overlay_text(instruction,
                                        "Checkpoint loaded; saved beacons unreadable, current kept")
                            else:
                                _ = try RenderScene::set_overlay_text(instruction, load_message(loaded.status))
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_PAUSE):
                            toggle_pause(&state)
                            Runtime::set_paused(&clock, state.phase != Phase.Playing)
                            _ = CourseSounds::set_paused(&sounds, state.phase == Phase.Paused)
                            jump_queued <- false
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            _ = try refresh_status(instruction, state, crouched)
                        elif state.phase == Phase.Playing:
                            desired_crouch: bool = ActionInput::action_down(input, CourseControls::ACTION_CROUCH)
                            if desired_crouch != crouched:
                                half_height: f32 = Tune::CROUCHED_HALF_HEIGHT if desired_crouch else Tune::STANDING_HALF_HEIGHT
                                changed: bool = try RuntimeServices::physics_character_set_capsule(
                                    session, character, Tune::CHARACTER_RADIUS, half_height)
                                crouched <- desired_crouch if changed
                                if changed:
                                    _ = try refresh_status(instruction, state, crouched)
                                else:
                                    _ = try RenderScene::set_overlay_text(instruction,
                                        "Ceiling blocks standing | keep crouching | P pause")
                            jump_queued <- true if ActionInput::action_pressed(input, CourseControls::ACTION_JUMP)
                            jump_sounds: bool = jump_queued and grounded
                            elapsed_delta: i64 = ((frame.elapsed_nanos -
                                previous_elapsed_nanos) / 1000).i64()
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            due_steps: usize = try Runtime::advance(&clock, elapsed_delta)
                            direction: Geometry::Vec3 = movement_direction(input)
                            if jump_queued and jump_sounds and due_steps > 0:
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_JUMP, frame.elapsed_nanos)
                            for _ in 0..<due_steps |session, character, direction, jump_queued, sounds, sound_tick, jump_sounds, guide, guide_agent|:
                                sound_tick <- sound_tick + 1
                                try patrol_guide(session, guide, guide_agent)
                                jump_speed: f32 = Tune::JUMP_SPEED if jump_queued else 0.0
                                if jump_queued and jump_sounds:
                                    _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, sound_tick)
                                try RuntimeServices::physics_character_move(session,
                                    character, direction, Tune::WALK_SPEED, jump_speed, false)
                                jump_queued <- false
                                _ = try RuntimeServices::physics_fixed_step(session,
                                    Runtime::fixed_step_seconds())
                            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                                session, character)
                            update_outcome(&state, pose.position)
                            newly_lit: usize = try CourseBeacons::update(&beacons, pose.position)
                            if newly_lit > 0:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_SAVE, frame.elapsed_nanos)
                                _ = try RenderScene::set_overlay_text(instruction,
                                    beacon_message(CourseBeacons::lit_count(beacons)))
                            ground: PhysicsRuntime::CharacterGround = try RuntimeServices::physics_character_ground(session, character)
                            now_grounded: bool = ground.state == PhysicsRuntime::CharacterGroundState.OnGround
                            # Footsteps and landings come from the authored triggers; the
                            # landing caption shows for every touchdown, heard or not.
                            _ = CourseSounds::track(&sounds, now_grounded, pose.position, sound_tick)
                            if now_grounded and not grounded:
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_LAND, frame.elapsed_nanos)
                            grounded <- now_grounded
                            if state.phase == Phase.Won or state.phase == Phase.Failed:
                                ending: u32 = CourseSounds::EVENT_WIN if state.phase == Phase.Won else CourseSounds::EVENT_FALL
                                _ = CourseSounds::fire(&sounds, ending, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, ending, frame.elapsed_nanos)
                                Runtime::set_paused(&clock, true)
                                jump_queued <- false
                                _ = try refresh_status(instruction, state, crouched)
                            _ = try update_avatar(avatar, pose, crouched)
                            _ = try update_camera(pose)
                            _ = try show_guide(session, guide)
                            guide_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
                            cycles_before: u64 = AudioTriggers::millimetres(walker.cycles)
                            _ = try update_walker(&walker, guide_pose, NavAgent::state(&guide_agent) == NavAgent::State.Moving)
                            _ = CourseSounds::animation_markers(&sounds, cycles_before, AudioTriggers::millimetres(walker.cycles), sound_tick)
                            _ = try advance_rigs(rigs, elapsed_delta.f32() / 1000000.0)
                        else:
                            previous_elapsed_nanos <- frame.elapsed_nanos
                    caption_shown <- try show_caption(legend, captions, settings, frame.elapsed_nanos, caption_shown)
                    # Resize applies on every running frame, whatever input arrived.
                    if frame.resize_serial != resize_serial and frame.pixel_width > 0 and frame.pixel_height > 0:
                        _ = try RenderScene::resize(frame.pixel_width, frame.pixel_height)
                        resize_serial <- frame.resize_serial
            _ = CourseSounds::stop(&sounds)
            try CourseBeacons::release(&beacons)
            try release_legend(&legend)
            try release_walker(&walker)
            try NavMesh::unload(guide.mesh)
            0
