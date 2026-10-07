# Interactive runtime loop for the character course, included in the
# CharacterCourse private section after its menu and HUD helpers.
        def interactive_loop(session: mutable RuntimeServices::Session&,
            character: mutable PhysicsRuntime::CharacterHandle,
            avatar: RenderScene::InstanceHandle,
            instruction: RenderScene::OverlayTextHandle,
            guide: mutable Guide, mode: i32, cells: mutable CourseCells::Cells&,
            cell_runtime: mutable CellWorld::Runtime&, cell_world: mutable World::World&,
            beacons: mutable CourseBeacons::Beacons&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, Runtime::ClockError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            running: mutable bool = true
            crouched: mutable bool = false
            state: mutable CourseState::State = CourseState::State()
            jump_queued: mutable bool = false
            previous_elapsed_nanos: mutable u64 = 0
            resize_serial: mutable u64 = 0
            suspended: mutable bool = false
            layout_height: mutable i32 = -1
            layout_selected: mutable i64 = -2
            clock: mutable Runtime::StepClock = Runtime::StepClock()
            # Invalid saved controls fall back to the defaults; the file is only
            # replaced when the player closes the rebind menu.
            saved_controls: CourseControls::LoadResult = CourseControls::load()
            keymap: mutable CourseControls::Keymap = saved_controls.keymap
            saved_pad: CoursePad::LoadResult = CoursePad::load()
            pads: mutable CoursePad::Pad = saved_pad.pad
            input: mutable ActionInput::Input = ActionInput::Input()
            return 33 if not CourseControls::bind(&input, &keymap, &pads)
            # Accessibility settings load the same way and apply before the
            # first frame; they are saved with the controls on menu close.
            saved_access: CourseAccess::LoadResult = CourseAccess::load()
            settings: mutable CourseAccess::Settings = saved_access.settings
            legend: Legend = try create_legend(&keymap, &pads, &settings)
            try layout_hud(&legend, instruction, &settings)
            captions: mutable CourseAccess::Captions = CourseAccess::Captions()
            caption_shown: mutable u32 = 0
            menu_open: mutable bool = false
            menu: mutable CourseControls::Menu = CourseControls::Menu(keymap)
            # Display and output picks are re-read each time the menu opens.
            device_list: mutable ApplicationDisplays::Displays = ApplicationDisplays::list().displays
            device_picks: mutable CourseDeviceMenu::Picks = devices_picks(&device_list)
            # Mouse-look holds the pointer only while playing outside the menu;
            # `look_held` is the last requested mode so an unsupported
            # platform is asked once per change, not every frame.
            look: mutable CourseLook::Look = CourseLook::Look()
            look_held: mutable bool = false
            # Gameplay input is recorded by frame and saved on quit, so a
            # session can be replayed through ActionInputStore::load_recording.
            recording: mutable ActionInputReplay::Log = ActionInputReplay::Log()
            recording_tick: mutable i64 = 0
            # Sound is optional: a failed start releases what it loaded and
            # the course plays silently. `sound_tick` counts fixed steps.
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            if CourseSounds::start(&sounds, session) != CourseSounds::STARTED:
                _ = CourseSounds::stop(&sounds)
            audio_workload: mutable CourseAudioWorkload::Stats = CourseAudioWorkload::Stats()
            sound_tick: mutable u64 = 0
            contact_stats: mutable CourseContactAudio::Stats = CourseContactAudio::Stats()
            guide_agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
            rigs: Rigs = optional_rigs()
            walker: mutable Walker = optional_walker(session, &guide)
            grounded: mutable bool = true
            # Dynamic crates the player can push; Q/L save and load their
            # whole World through WorldSaveLoad alongside the checkpoint.
            props: mutable CourseProps::Props = catch CourseProps::create(session):
                value: value
                error failure: return 35
            # Floor cells stream in and out around the player (cells.elisa);
            # modes 3 and 4 also drive the real SDL input path.
            return 37 if not CourseCells::open(cells, Tune::START_Z) and (mode == 3 or mode == 4)
            if saved_controls.status == CourseControls::LoadStatus.Rejected or saved_controls.status == CourseControls::LoadStatus.Unreadable:
                _ = try RenderScene::set_overlay_text(instruction,
                    "Saved controls were invalid; defaults restored")
            elif saved_access.status == CourseAccess::LoadStatus.Rejected or saved_access.status == CourseAccess::LoadStatus.Unreadable:
                _ = try RenderScene::set_overlay_text(instruction,
                    "Saved accessibility settings were invalid; defaults restored")
            else:
                _ = try refresh_status(instruction, state, crouched)
            while running |session, input, character, avatar, instruction, crouched, state, jump_queued, previous_elapsed_nanos, resize_serial, suspended, layout_height, layout_selected, clock, keymap, legend, menu_open, menu, sounds, sound_tick, contact_stats, audio_workload, grounded, guide, guide_agent, rigs, walker, beacons, props, settings, captions, caption_shown, pads, look, look_held, recording, recording_tick, mode, cells, cell_runtime, cell_world, device_list, device_picks|:
                pump: Application::PumpStatus = try RuntimeServices::pump(session)
                if pump == Application::PumpStatus.ExitRequested:
                    running <- false
                elif pump != Application::PumpStatus.Running and pump != Application::PumpStatus.Suspended:
                    return 40
                if running and pump == Application::PumpStatus.Suspended:
                    _ = CourseControls::drain_gameplay_recorded(input, recording, recording_tick)
                    if not suspended:
                        _ = ActionInputReplay::feed_context(recording, input, recording_tick, ActionInput::Context.UI)
                        recording_tick <- recording_tick + 1
                        suspended <- true
                    _ = CourseState::pause_on_focus_loss(&state)
                    Runtime::set_paused(&clock, true)
                    _ = CourseSounds::set_paused(&sounds, true)
                    jump_queued <- false
                    previous_elapsed_nanos <- 0
                    if look_held:
                        _ = Application::set_relative_mouse(false)
                        look_held <- false
                    if mode == 4:
                        suspended_frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
                        CourseCells::pilot_course(cells, suspended_frame.elapsed_nanos,
                            crouched, ActionInput::action_down(input, CourseControls::ACTION_CROUCH),
                            ActionInput::action_down(input, CourseControls::ACTION_FORWARD),
                            grounded, CourseState::phase(state) == CourseState::Phase.Paused,
                            CourseState::phase(state) == CourseState::Phase.Won, CourseState::phase(state) == CourseState::Phase.Failed, menu_open, menu.selected, settings.text_scale)
                if running and pump == Application::PumpStatus.Running:
                    if suspended:
                        _ = ActionInputReplay::feed_context(recording, input, recording_tick, ActionInput::Context.Gameplay)
                        recording_tick <- recording_tick + 1
                        suspended <- false
                    frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
                    if mode == 3:
                        CourseCells::pilot(cells, frame.elapsed_nanos)
                    elif mode == 4:
                        CourseCells::pilot_course(cells, frame.elapsed_nanos,
                            crouched, ActionInput::action_down(input, CourseControls::ACTION_CROUCH),
                            ActionInput::action_down(input, CourseControls::ACTION_FORWARD),
                            grounded, CourseState::phase(state) == CourseState::Phase.Paused,
                            CourseState::phase(state) == CourseState::Phase.Won, CourseState::phase(state) == CourseState::Phase.Failed, menu_open, menu.selected, settings.text_scale)
                    mood: i32 = CourseSounds::mood_for(menu_open, CourseState::phase(state) == CourseState::Phase.Paused)
                    look_wanted: bool = CourseState::phase(state) == CourseState::Phase.Playing and not menu_open
                    if look_wanted != look_held:
                        _ = Application::set_relative_mouse(look_wanted)
                        look_held <- look_wanted
                    _ = CourseSounds::mix(&sounds, mood, frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::play_music(&sounds, CourseSounds::music_for(CourseState::phase(state) == CourseState::Phase.Won), frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::poll_reload(&sounds, frame.elapsed_nanos / 1000000)
                    _ = CourseSounds::update(&sounds)
                    if mode == 4:
                        audio_stage: i32 = 2 if menu_open or CourseState::phase(state) == CourseState::Phase.Paused else 1
                        return 224 if not CourseAudioWorkload::sample(session, &sounds, &audio_workload, audio_stage)
                    if menu_open:
                        # Keys first; pointer presses when no key acted.
                        step: mutable CourseControls::MenuStep = CourseControls::drain_menu(menu)
                        if step.result == CourseControls::MenuResult.Ignored:
                            step <- drain_menu_pointer(menu, settings, frame.logical_height)
                        menu <- step.menu
                        previous_elapsed_nanos <- frame.elapsed_nanos
                        if step.result == CourseControls::MenuResult.Closed:
                            # A fresh input map drops anything held while the
                            # menu was open, so no key leaks into gameplay.
                            menu_open <- false
                            keymap <- menu.keymap
                            rebound: mutable ActionInput::Input = ActionInput::Input()
                            return 33 if not CourseControls::bind(&rebound, &keymap, &pads)
                            input <- move rebound
                            keys_stored: bool = catch CourseControls::save(&keymap):
                                ok: true
                                error failure: false
                            access_stored: bool = catch CourseAccess::save(&settings):
                                ok: true
                                error failure: false
                            pad_stored: bool = catch CoursePad::save(&pads):
                                ok: true
                                error failure: false
                            devices_stored: bool = devices_menu_save(&device_picks)
                            stored: bool = keys_stored and access_stored and pad_stored and devices_stored
                            _ = try refresh_legend(legend, keymap, pads, settings, -1)
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Controls saved | P resume" if stored else "Controls not saved; kept until quit | P resume")
                        elif step.result == CourseControls::MenuResult.Reserved:
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Reserved key | Esc P R Q L Tab and arrows stay fixed")
                        elif step.result == CourseControls::MenuResult.Adjusted:
                            if menu.selected >= CourseControls::DEVICE_FIRST:
                                device_row: i64 = menu.selected - CourseControls::DEVICE_FIRST
                                device_picks <- CourseDeviceMenu::adjust(device_picks, device_row, step.delta)
                                if device_row == CourseDeviceMenu::ROW_DISPLAY and device_picks.display < device_list.count.i64():
                                    _ = ApplicationDisplays::move_window_to(device_list.items[device_picks.display.usize()].id)
                            elif menu.selected >= CourseControls::PAD_FIRST:
                                pads <- CoursePad::cycle(pads, menu.selected - CourseControls::PAD_FIRST, step.delta)
                            else:
                                settings <- CourseAccess::adjust(settings, menu.selected - CourseControls::SLOT_COUNT, step.delta)
                            _ = try layout_hud(legend, instruction, settings)
                            layout_height <- -1
                            _ = try refresh_legend(legend, menu.keymap, pads, settings, menu.selected)
                            try refresh_device_rows(legend, device_picks)
                            _ = announce_row(menu.keymap, pads, settings, menu.selected)
                            language_row: bool = menu.selected == CourseControls::SLOT_COUNT + CourseAccess::ROW_LANGUAGE
                            caption_shown <- try show_caption(legend, captions, settings, frame.elapsed_nanos, 0 if language_row else caption_shown)
                        elif step.result != CourseControls::MenuResult.Ignored:
                            _ = try refresh_legend(legend, menu.keymap, pads, settings, menu.selected)
                            _ = announce_row(menu.keymap, pads, settings, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                    else:
                        look <- CourseLook::drain(look, settings.reduced_motion)
                        focus_lost: bool = CourseControls::drain_gameplay_recorded(input, recording, recording_tick)
                        recording_tick <- recording_tick + 1
                        if focus_lost and CourseState::pause_on_focus_loss(&state):
                            Runtime::set_paused(&clock, true)
                            _ = CourseSounds::set_paused(&sounds, true)
                            jump_queued <- false
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Paused: window lost focus | P resume | Tab controls")
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_QUIT):
                            running <- false
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_CONTROLS) and CourseState::phase(state) == CourseState::Phase.Paused:
                            menu_open <- true
                            menu <- CourseControls::Menu(keymap)
                            device_list <- ApplicationDisplays::list().displays
                            device_picks <- devices_picks(&device_list)
                            _ = try refresh_legend(legend, keymap, pads, settings, menu.selected)
                            try refresh_device_rows(legend, device_picks)
                            _ = announce_row(keymap, pads, settings, menu.selected)
                            _ = try RenderScene::set_overlay_text(instruction, menu_hint())
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_RESTART):
                            restarted: bool = restart_live(session, &character,
                                state, sounds, beacons, &grounded, &crouched,
                                &jump_queued, &clock, &previous_elapsed_nanos,
                                frame.elapsed_nanos, avatar, instruction, look)
                            if mode == 4 and restarted:
                                return 224 if not CourseAudioWorkload::sample(session, &sounds, &audio_workload, 3)
                            if not restarted:
                                _ = catch RenderScene::set_overlay_text(instruction,
                                    "Restart incomplete; see the application log"):
                                    ok: true
                                    error failure: false
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_SAVE):
                            saved_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                            checkpoint: CourseProgress::Checkpoint = checkpoint_of(state, saved_pose, crouched)
                            checkpoint_stored: bool = catch CourseProgress::save(&checkpoint):
                                ok: true
                                error failure: false
                            stored: bool = checkpoint_stored and CourseBeacons::save(beacons) and CourseProps::save(props)
                            if stored:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_SAVE, frame.elapsed_nanos)
                            _ = try RenderScene::set_overlay_text(instruction,
                                "Checkpoint saved | L load" if stored else "Checkpoint could not be saved")
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_LOAD):
                            loaded: CourseProgress::LoadResult = CourseProgress::load()
                            if loaded.status == CourseProgress::LoadStatus.Loaded:
                                restored_state: mutable CourseState::State = CourseState::State()
                                state_valid: bool = CourseState::restore(&restored_state,
                                    loaded.checkpoint.phase, loaded.checkpoint.attempts)
                                if state_valid:
                                    restored_character: PhysicsRuntime::CharacterHandle = try apply_checkpoint(session, character, loaded.checkpoint)
                                    character <- restored_character
                                    state <- restored_state
                                    beacons_loaded: i32 = try CourseBeacons::load(beacons)
                                    props_loaded: i32 = try CourseProps::load(session, &props)
                                    crouched <- loaded.checkpoint.crouched
                                    jump_queued <- false
                                    clock <- Runtime::StepClock()
                                    Runtime::set_paused(&clock, CourseState::phase(state) != CourseState::Phase.Playing)
                                    CourseSounds::reset_scene(&sounds)
                                    _ = CourseSounds::set_paused(&sounds, CourseState::phase(state) == CourseState::Phase.Paused)
                                    grounded <- false
                                    previous_elapsed_nanos <- frame.elapsed_nanos
                                    loaded_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                                    _ = try update_avatar(avatar, loaded_pose, crouched)
                                    _ = try update_camera_look(loaded_pose, look)
                                    _ = try refresh_status(instruction, state, crouched)
                                    if beacons_loaded != 0 or props_loaded != 0:
                                        _ = try RenderScene::set_overlay_text(instruction,
                                            "Checkpoint loaded; saved beacons or crates unreadable, current kept")
                                else:
                                    _ = try RenderScene::set_overlay_text(instruction,
                                        "Checkpoint rejected; current run kept")
                            else:
                                _ = try RenderScene::set_overlay_text(instruction, load_message(loaded.status))
                        elif ActionInput::action_pressed(input, CourseControls::ACTION_PAUSE):
                            CourseState::toggle_pause(&state)
                            Runtime::set_paused(&clock, CourseState::phase(state) != CourseState::Phase.Playing)
                            _ = CourseSounds::set_paused(&sounds, CourseState::phase(state) == CourseState::Phase.Paused)
                            jump_queued <- false
                            previous_elapsed_nanos <- frame.elapsed_nanos
                            _ = try refresh_status(instruction, state, crouched)
                        elif CourseState::phase(state) == CourseState::Phase.Playing:
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
                            direction: Geometry::Vec3 = movement_direction_look(input, look)
                            if jump_queued and jump_sounds and due_steps > 0:
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_JUMP, frame.elapsed_nanos)
                            for _ in 0..<due_steps |session, character, direction, jump_queued, sounds, sound_tick, jump_sounds, guide, guide_agent, props, contact_stats|:
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
                                contacts_ok: bool = catch CourseContactAudio::step(session, &props, &sounds, sound_tick, &contact_stats):
                                    ok: true
                                    error failure: false
                                return 273 if not contacts_ok
                            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                                session, character)
                            CourseState::update_outcome(&state,
                                pose.position.y < Tune::FAILURE_Y,
                                ElisaMath::fabsf(pose.position.x - Tune::GOAL_X) < Tune::GOAL_TOLERANCE and ElisaMath::fabsf(pose.position.z - Tune::GOAL_Z) < Tune::GOAL_TOLERANCE)
                            CourseCells::update(cells, cell_runtime, cell_world, pose.position)
                            try CourseProps::sync(session, &props)
                            newly_lit: usize = try CourseBeacons::update(beacons, pose.position)
                            if newly_lit > 0:
                                _ = CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_SAVE, frame.elapsed_nanos)
                                _ = try RenderScene::set_overlay_text(instruction,
                                    beacon_message(CourseBeacons::lit_count(beacons)))
                            ground: PhysicsRuntime::CharacterGround = try RuntimeServices::physics_character_ground(session, character)
                            now_grounded: bool = ground.state == PhysicsRuntime::CharacterGroundState.OnGround
                            CourseCells::observe_pilot(cells, pose.position, crouched,
                                ActionInput::action_down(input, CourseControls::ACTION_CROUCH), now_grounded,
                                ActionInput::action_pressed(input, CourseControls::ACTION_JUMP)) if mode == 3
                            # Footsteps and landings come from the authored triggers; the
                            # landing caption shows for every touchdown, heard or not.
                            _ = CourseSounds::track(&sounds, now_grounded, pose.position, sound_tick)
                            if now_grounded and not grounded:
                                captions <- CourseAccess::caption(captions, &settings, CourseSounds::EVENT_LAND, frame.elapsed_nanos)
                            grounded <- now_grounded
                            if CourseState::phase(state) == CourseState::Phase.Won or CourseState::phase(state) == CourseState::Phase.Failed:
                                ending: u32 = CourseSounds::EVENT_WIN if CourseState::phase(state) == CourseState::Phase.Won else CourseSounds::EVENT_FALL
                                _ = CourseSounds::fire(&sounds, ending, sound_tick)
                                captions <- CourseAccess::caption(captions, &settings, ending, frame.elapsed_nanos)
                                Runtime::set_paused(&clock, true)
                                jump_queued <- false
                                _ = try refresh_status(instruction, state, crouched)
                            _ = try update_avatar(avatar, pose, crouched)
                            _ = try update_camera_look(pose, look)
                            _ = try show_guide(session, guide)
                            guide_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
                            cycles_before: u64 = AudioTriggers::millimetres(walker.cycles)
                            _ = try update_walker(&walker, guide_pose, NavAgent::state(&guide_agent) == NavAgent::State.Moving)
                            _ = CourseSounds::animation_markers(&sounds, &walker.animation.events,
                                walker.animation.event_count, &walker.animation.audio_events,
                                walker.animation.duration_ticks,
                                cycles_before, AudioTriggers::millimetres(walker.cycles), sound_tick,
                                pose.position, guide_pose.position)
                            _ = try advance_rigs(rigs, elapsed_delta.f32() / 1000000.0)
                        else:
                            previous_elapsed_nanos <- frame.elapsed_nanos
                    selected_row: i64 = menu.selected if menu_open else -1
                    if layout_height != frame.logical_height or layout_selected != selected_row:
                        try fit_legend(legend, settings, frame.logical_height, selected_row)
                        layout_height <- frame.logical_height
                        layout_selected <- selected_row
                    caption_shown <- try show_caption(legend, captions, settings, frame.elapsed_nanos, caption_shown)
                    # Resize applies on every running frame, whatever input arrived.
                    if frame.resize_serial != resize_serial and frame.pixel_width > 0 and frame.pixel_height > 0:
                        _ = try RenderScene::resize(frame.pixel_width, frame.pixel_height)
                        resize_serial <- frame.resize_serial
            if look_held:
                _ = Application::set_relative_mouse(false)
            no_pointer: ActionPointerReplay::Log = ActionPointerReplay::Log()
            _ = ActionInputStore::save_recording(RECORDING_KEY, &recording, &no_pointer)
            _ = CourseSounds::stop(&sounds)
            if mode == 4:
                audio_result: i32 = CourseAudioWorkload::finish(session, &audio_workload)
                return audio_result if audio_result != 0
            try CourseBeacons::release(beacons)
            try CourseProps::release(session, &props)
            try release_legend(&legend)
            try release_walker(&walker)
            try NavMesh::unload(guide.mesh)
            closed_cells: i32 = CourseCells::close(cells, cell_runtime, cell_world)
            contact_result: i32 = CourseContactAudio::verdict(&contact_stats)
            return contact_result if (mode == 3 or mode == 4) and contact_result != 0
            return CourseCells::verdict(cells, closed_cells) if mode == 3
            return CourseCells::course_verdict(cells, closed_cells) if mode == 4
            0
