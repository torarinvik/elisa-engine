# Interactive play loop, HUD text and the rebind menu, included in the
# CharacterCourse private section.
        struct Legend:
            header: mutable RenderScene::OverlayTextHandle
            labels: mutable RenderScene::OverlayTextHandle[6]
            keys: mutable RenderScene::OverlayTextHandle[6]

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
            "Rebind: Up/Down select | press a key | Tab done"

        def legend_header(menu_open: bool) -> cstr:
            "Rebinding (Esc/Tab closes)" if menu_open else "Controls | Tab while paused to rebind"

        # One label and one key name per slot, so a rebind updates a single
        # static key string instead of formatting a sentence.
        def create_legend(keymap: CourseControls::Keymap&) -> Legend error[RenderScene::RenderSceneError]:
            legend: mutable Legend = zeroed
            legend.header <- try RenderScene::create_overlay_text(legend_header(false),
                Tune::LEGEND_X, Tune::LEGEND_Y, Tune::LEGEND_FONT_SIZE,
                box_color(0.78, 0.84, 0.9))
            row_y: mutable f32 = Tune::LEGEND_Y
            for slot in 0..<CourseControls::SLOT_COUNT |legend, keymap, row_y|:
                row_y <- row_y + Tune::LEGEND_ROW_HEIGHT
                legend.labels[slot.usize()] <- try RenderScene::create_overlay_text(
                    CourseControls::slot_name(slot), Tune::LEGEND_X, row_y,
                    Tune::LEGEND_FONT_SIZE, box_color(1.0, 1.0, 1.0))
                legend.keys[slot.usize()] <- try RenderScene::create_overlay_text(
                    CourseControls::key_name(CourseControls::slot_code(keymap, slot)),
                    Tune::LEGEND_KEY_X, row_y, Tune::LEGEND_FONT_SIZE,
                    box_color(1.0, 1.0, 1.0))
            legend

        # `selected` is the highlighted slot, or -1 outside the menu.
        def refresh_legend(legend: Legend&, keymap: CourseControls::Keymap&,
            selected: i64) -> void error[RenderScene::RenderSceneError]:
            try RenderScene::set_overlay_text(legend.header, legend_header(selected >= 0))
            for slot in 0..<CourseControls::SLOT_COUNT |legend, keymap, selected|:
                color: RenderScene::Color = box_color(1.0, 0.85, 0.2) if slot == selected else box_color(1.0, 1.0, 1.0)
                try RenderScene::set_overlay_text(legend.keys[slot.usize()],
                    CourseControls::key_name(CourseControls::slot_code(keymap, slot)))
                try RenderScene::set_overlay_text_color(legend.labels[slot.usize()], color)
                try RenderScene::set_overlay_text_color(legend.keys[slot.usize()], color)

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
            legend: Legend = try create_legend(&keymap)
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
            grounded: mutable bool = true
            # Beacons light as the player passes; Q/L save and load their
            # prefab overrides alongside the checkpoint.
            beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                value: value
                error failure: return 34
            if saved_controls.status == CourseControls::LoadStatus.Rejected or saved_controls.status == CourseControls::LoadStatus.Unreadable:
                _ = try RenderScene::set_overlay_text(instruction,
                    "Saved controls were invalid; defaults restored")
            else:
                _ = try refresh_status(instruction, state, crouched)
            while running |session, input, character, avatar, instruction, crouched, state, jump_queued, previous_elapsed_nanos, resize_serial, clock, keymap, legend, menu_open, menu, sounds, sound_tick, grounded, guide, guide_agent, rigs, beacons|:
                pump: Application::PumpStatus = try RuntimeServices::pump(session)
                if pump == Application::PumpStatus.ExitRequested:
                    running <- false
                elif pump != Application::PumpStatus.Running and pump != Application::PumpStatus.Suspended:
                    return 40
                if running and pump == Application::PumpStatus.Running:
                    frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
                    if menu_open:
                        step: CourseControls::MenuStep = CourseControls::drain_menu(menu)
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
                            stored: bool = catch CourseControls::save(&keymap):
                                ok: true
                                error failure: false
                            _ = try refresh_legend(legend, keymap, -1)
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Controls saved | P resume" if stored else "Controls not saved; kept until quit | P resume")
                        elif step.result == CourseControls::MenuResult.Reserved:
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Reserved key | Esc P R Q L Tab and arrows stay fixed")
                        elif step.result != CourseControls::MenuResult.Ignored:
                            _ = try refresh_legend(legend, menu.keymap, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                    else:
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
                            _ = try refresh_legend(legend, keymap, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_RESTART):
                            character <- try restart_character(session, character)
                            restart(&state)
                            CourseSounds::reset_scene(&sounds)
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
                                _ = try RenderScene::set_overlay_text(instruction,
                                    beacon_message(CourseBeacons::lit_count(beacons)))
                            ground: PhysicsRuntime::CharacterGround = try RuntimeServices::physics_character_ground(session, character)
                            now_grounded: bool = ground.state == PhysicsRuntime::CharacterGroundState.OnGround
                            if now_grounded and not grounded:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_LAND, sound_tick)
                            grounded <- now_grounded
                            if state.phase == Phase.Won or state.phase == Phase.Failed:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_WIN if state.phase == Phase.Won else CourseSounds::EVENT_FALL, sound_tick)
                                Runtime::set_paused(&clock, true)
                                jump_queued <- false
                                _ = try refresh_status(instruction, state, crouched)
                            _ = try update_avatar(avatar, pose, crouched)
                            _ = try update_camera(pose)
                            _ = try show_guide(session, guide)
                            _ = try advance_rigs(rigs, elapsed_delta.f32() / 1000000.0)
                        else:
                            previous_elapsed_nanos <- frame.elapsed_nanos
                    # Resize applies on every running frame, whatever input arrived.
                    if frame.resize_serial != resize_serial and frame.pixel_width > 0 and frame.pixel_height > 0:
                        _ = try RenderScene::resize(frame.pixel_width, frame.pixel_height)
                        resize_serial <- frame.resize_serial
            _ = CourseSounds::stop(&sounds)
            try CourseBeacons::release(&beacons)
            try NavMesh::unload(guide.mesh)
            0
