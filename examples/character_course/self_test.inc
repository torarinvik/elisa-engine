# Finite checks for the character course, included in its private section.
        def simulate_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            for _ in 0..<Tune::INITIAL_STEP_COUNT |session|:
                _ = try RuntimeServices::physics_fixed_step(session,
                    Runtime::fixed_step_seconds())
            initial: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                session, character)
            max_height: mutable f32 = initial.position.y
            crossed_step: mutable bool = false
            for _ in 0..<Tune::SELF_TEST_TICKS |session, character, max_height, crossed_step|:
                try RuntimeServices::physics_character_move(session, character,
                    Geometry::Vec3{x: 1.0, y: 0.0, z: 0.0}, Tune::WALK_SPEED,
                    0.0, false)
                _ = try RuntimeServices::physics_fixed_step(session,
                    Runtime::fixed_step_seconds())
                pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                    session, character)
                max_height <- pose.position.y if pose.position.y > max_height
                crossed_step <- true if pose.position.x > Tune::STEP_CLEAR_X
            return 30 if not crossed_step
            return 31 if max_height < initial.position.y + Tune::SELF_TEST_MIN_STEP_RISE
            final_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                session, character)
            return 32 if final_pose.position.x < initial.position.x + Tune::SELF_TEST_MIN_PROGRESS
            0

        def state_test() -> i32:
            state: mutable GameState = GameState()
            toggle_pause(&state)
            return Tune::STATE_TEST_PAUSE_CODE if state.phase != Phase.Paused
            toggle_pause(&state)
            return Tune::STATE_TEST_RESUME_CODE if state.phase != Phase.Playing
            update_outcome(&state, Geometry::Vec3{x: 0.0, y: Tune::FAILURE_Y - Tune::SELF_TEST_FAILURE_MARGIN, z: 0.0})
            return Tune::STATE_TEST_FAIL_CODE if state.phase != Phase.Failed
            update_outcome(&state, Geometry::Vec3{x: Tune::GOAL_X, y: Tune::GOAL_HEIGHT, z: Tune::GOAL_Z})
            return Tune::STATE_TEST_FAIL_LOCK_CODE if state.phase != Phase.Failed
            restart(&state)
            return Tune::STATE_TEST_FAIL_RESTART_CODE if state.phase != Phase.Playing
            update_outcome(&state, Geometry::Vec3{x: Tune::GOAL_X, y: Tune::GOAL_HEIGHT, z: Tune::GOAL_Z})
            return Tune::STATE_TEST_WIN_CODE if state.phase != Phase.Won
            update_outcome(&state, Geometry::Vec3{x: 0.0, y: Tune::FAILURE_Y - Tune::SELF_TEST_FAILURE_MARGIN, z: 0.0})
            return Tune::STATE_TEST_WIN_LOCK_CODE if state.phase != Phase.Won
            toggle_pause(&state)
            return Tune::STATE_TEST_TERMINAL_PAUSE_CODE if state.phase != Phase.Won
            restart(&state)
            return Tune::STATE_TEST_WIN_RESTART_CODE if state.phase != Phase.Playing
            0

        struct ProgressOutcome:
            status: i32
            character: PhysicsRuntime::CharacterHandle

        def near(first: Geometry::Vec3, second: Geometry::Vec3) -> bool:
            ElisaMath::fabsf(first.x - second.x) <= Tune::SELF_TEST_RESTART_TOLERANCE and ElisaMath::fabsf(first.y - second.y) <= Tune::SELF_TEST_RESTART_TOLERANCE and ElisaMath::fabsf(first.z - second.z) <= Tune::SELF_TEST_RESTART_TOLERANCE

        def progress_status(status: CourseProgress::LoadStatus) -> i64:
            if status == CourseProgress::LoadStatus.Loaded:
                0
            elif status == CourseProgress::LoadStatus.Missing:
                1
            elif status == CourseProgress::LoadStatus.Unreadable:
                2
            else:
                3

        # Save, restart and load through UserData; malformed, wrong-shape and
        # missing records must leave the restored character untouched.
        def progress_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, Save::SaveError, UserData::UserDataError]:
            state: mutable GameState = GameState()
            state.attempts <- Tune::SELF_TEST_ATTEMPTS
            state.phase <- Phase.Paused
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            saved: CourseProgress::Checkpoint = checkpoint_of(state, pose, false)
            try CourseProgress::save(&saved)
            current: mutable PhysicsRuntime::CharacterHandle = try restart_character(session, character)
            restart(&state)
            loaded: CourseProgress::LoadResult = CourseProgress::load()
            return ProgressOutcome{status: Tune::PROGRESS_LOAD_CODE, character: current} if progress_status(loaded.status) != 0 or loaded.checkpoint.attempts != Tune::SELF_TEST_ATTEMPTS or phase_from_code(loaded.checkpoint.phase) != Phase.Paused
            current <- try apply_checkpoint(session, current, loaded.checkpoint)
            restored: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
            return ProgressOutcome{status: Tune::PROGRESS_RESTORE_CODE, character: current} if not near(restored.position, pose.position)
            unknown_phase: CourseProgress::Checkpoint = CourseProgress::Checkpoint{
                phase: 9, position: pose.position, crouched: false, attempts: 1}
            try CourseProgress::save(&unknown_phase)
            return ProgressOutcome{status: Tune::PROGRESS_MALFORMED_CODE, character: current} if progress_status(CourseProgress::load().status) != 3
            values: i64[Save::MAX_FIELDS] = zeroed
            short_record: Save::Blob = try Save::save_write(values, 2)
            try UserData::write_blob(CourseProgress::key(), &short_record)
            return ProgressOutcome{status: Tune::PROGRESS_SHAPE_CODE, character: current} if progress_status(CourseProgress::load().status) != 3
            try UserData::remove_blob(CourseProgress::key())
            return ProgressOutcome{status: Tune::PROGRESS_MISSING_CODE, character: current} if progress_status(CourseProgress::load().status) != 1
            after_rejects: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
            return ProgressOutcome{status: Tune::PROGRESS_UNCHANGED_CODE, character: current} if not near(after_rejects.position, restored.position)
            cycle_status: mutable i32 = 0
            for _ in 0..<Tune::PROGRESS_CYCLES |session, current, state, cycle_status, restored|:
                cycle_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
                cycle_saved: CourseProgress::Checkpoint = checkpoint_of(state, cycle_pose, false)
                try CourseProgress::save(&cycle_saved)
                current <- try restart_character(session, current)
                restart(&state)
                cycle_loaded: CourseProgress::LoadResult = CourseProgress::load()
                if progress_status(cycle_loaded.status) == 0:
                    current <- try apply_checkpoint(session, current, cycle_loaded.checkpoint)
                    state.attempts <- cycle_loaded.checkpoint.attempts
                    cycle_restored: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
                    cycle_status <- Tune::PROGRESS_CYCLE_CODE if not near(cycle_restored.position, restored.position)
                else:
                    cycle_status <- Tune::PROGRESS_CYCLE_CODE
            try UserData::remove_blob(CourseProgress::key())
            ProgressOutcome{status: cycle_status, character: current}

        const module ControlsCodes:
            DEFAULTS: i32 = 70
            RESERVED: i32 = 71
            SWAP: i32 = 72
            ROUND_TRIP: i32 = 73
            INVALID_MAP: i32 = 74
            VERSION: i32 = 75
            PERSIST: i32 = 76
            MENU: i32 = 77
            REBIND_INPUT: i32 = 78
            FOCUS: i32 = 79
            GAMEPAD_MAPPING: i32 = 80

        def key_code(key: Application::KeyboardKeyCode) -> i64:
            Application::keyboard_key_code(key)

        def same_keymap(first: CourseControls::Keymap&, second: CourseControls::Keymap&) -> bool:
            first.forward == second.forward and first.backward == second.backward and first.left == second.left and first.right == second.right and first.jump == second.jump and first.crouch == second.crouch

        def device_event(device: ActionInput::Device, code: i64, value: f32) -> ActionInput::Event:
            ActionInput::Event{device: device, code: code, value: value,
                pressed: value > 0.0, released: value <= 0.0, chord_down: false}

        def rejected_to_defaults(values: i64[Save::MAX_FIELDS]) -> bool error[Save::SaveError]:
            blob: Save::Blob = try Save::save_write(values, 7)
            result: CourseControls::LoadResult = CourseControls::decode(&blob)
            defaults: CourseControls::Keymap = CourseControls::defaults()
            result.status == CourseControls::LoadStatus.Rejected and same_keymap(&result.keymap, &defaults)

        # Rebinding rules, the saved record, the menu state machine and the
        # rebuilt input map. Gamepad checks are mapping-only: synthetic events,
        # no physical controller.
        def controls_test() -> i32 error[Save::SaveError, UserData::UserDataError]:
            defaults: CourseControls::Keymap = CourseControls::defaults()
            return ControlsCodes::DEFAULTS if not CourseControls::valid(&defaults)
            escape: CourseControls::AssignOutcome = CourseControls::assign(defaults,
                CourseControls::SLOT_JUMP, key_code(Application::KeyboardKeyCode.Escape))
            arrow: CourseControls::AssignOutcome = CourseControls::assign(defaults,
                CourseControls::SLOT_FORWARD, key_code(Application::KeyboardKeyCode.ArrowUp))
            return ControlsCodes::RESERVED if escape.result != CourseControls::AssignResult.Reserved or arrow.result != CourseControls::AssignResult.Reserved or not same_keymap(&escape.keymap, &defaults)
            swapped: CourseControls::AssignOutcome = CourseControls::assign(defaults,
                CourseControls::SLOT_JUMP, key_code(Application::KeyboardKeyCode.W))
            return ControlsCodes::SWAP if swapped.result != CourseControls::AssignResult.Swapped or swapped.keymap.jump != key_code(Application::KeyboardKeyCode.W) or swapped.keymap.forward != key_code(Application::KeyboardKeyCode.Space) or not CourseControls::valid(&swapped.keymap)
            blob: Save::Blob = try CourseControls::encode(&swapped.keymap)
            decoded: CourseControls::LoadResult = CourseControls::decode(&blob)
            return ControlsCodes::ROUND_TRIP if decoded.status != CourseControls::LoadStatus.Loaded or not same_keymap(&decoded.keymap, &swapped.keymap)
            values: mutable i64[Save::MAX_FIELDS] = zeroed
            values[0] <- 1
            values[1] <- defaults.forward
            values[2] <- defaults.forward
            values[3] <- defaults.left
            values[4] <- defaults.right
            values[5] <- defaults.jump
            values[6] <- defaults.crouch
            duplicate_rejected: bool = try rejected_to_defaults(values)
            values[2] <- key_code(Application::KeyboardKeyCode.Escape)
            reserved_rejected: bool = try rejected_to_defaults(values)
            return ControlsCodes::INVALID_MAP if not duplicate_rejected or not reserved_rejected
            values[2] <- defaults.backward
            values[0] <- 2
            version_rejected: bool = try rejected_to_defaults(values)
            return ControlsCodes::VERSION if not version_rejected
            _ = catch UserData::remove_blob(CourseControls::storage_key()):
                ok: true
                error failure: false
            missing: CourseControls::LoadResult = CourseControls::load()
            try CourseControls::save(&swapped.keymap)
            persisted: CourseControls::LoadResult = CourseControls::load()
            short_record: Save::Blob = try Save::save_write(values, 2)
            try UserData::write_blob(CourseControls::storage_key(), &short_record)
            corrupt: CourseControls::LoadResult = CourseControls::load()
            try UserData::remove_blob(CourseControls::storage_key())
            return ControlsCodes::PERSIST if missing.status != CourseControls::LoadStatus.Missing or not same_keymap(&missing.keymap, &defaults) or persisted.status != CourseControls::LoadStatus.Loaded or not same_keymap(&persisted.keymap, &swapped.keymap) or corrupt.status != CourseControls::LoadStatus.Rejected or not same_keymap(&corrupt.keymap, &defaults)
            menu: mutable CourseControls::Menu = CourseControls::Menu(defaults)
            menu <- CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.ArrowDown)).menu
            down_selected: i64 = menu.selected
            menu <- CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.ArrowUp)).menu
            menu <- CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.ArrowUp)).menu
            wrapped_selected: i64 = menu.selected
            menu <- CourseControls::Menu{keymap: menu.keymap, selected: CourseControls::SLOT_CROUCH}
            assigned: CourseControls::MenuStep = CourseControls::menu_key(menu, key_code(Application::KeyboardKeyCode.J))
            blocked: CourseControls::MenuStep = CourseControls::menu_key(assigned.menu, key_code(Application::KeyboardKeyCode.P))
            closed: CourseControls::MenuStep = CourseControls::menu_key(blocked.menu, key_code(Application::KeyboardKeyCode.Escape))
            return ControlsCodes::MENU if down_selected != CourseControls::SLOT_BACKWARD or wrapped_selected != CourseControls::MENU_ROWS - 1 or assigned.result != CourseControls::MenuResult.Assigned or assigned.menu.keymap.crouch != key_code(Application::KeyboardKeyCode.J) or blocked.result != CourseControls::MenuResult.Reserved or closed.result != CourseControls::MenuResult.Closed or closed.menu.keymap.crouch != key_code(Application::KeyboardKeyCode.J)
            remapped: CourseControls::Keymap = CourseControls::assign(defaults,
                CourseControls::SLOT_FORWARD, key_code(Application::KeyboardKeyCode.J)).keymap
            input: mutable ActionInput::Input = ActionInput::Input()
            return ControlsCodes::REBIND_INPUT if not CourseControls::bind(&input, &remapped)
            ActionInput::apply(&input, device_event(ActionInput::Device.Keyboard, key_code(Application::KeyboardKeyCode.W), 1.0))
            old_key_moves: bool = ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            ActionInput::apply(&input, device_event(ActionInput::Device.Keyboard, key_code(Application::KeyboardKeyCode.J), 1.0))
            new_key_moves: bool = ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            ActionInput::apply(&input, device_event(ActionInput::Device.Keyboard, key_code(Application::KeyboardKeyCode.J), 0.0))
            released: bool = not ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            return ControlsCodes::REBIND_INPUT if old_key_moves or not new_key_moves or not released
            ActionInput::apply(&input, device_event(ActionInput::Device.Keyboard, key_code(Application::KeyboardKeyCode.ArrowUp), 1.0))
            arrow_moves: bool = ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            focus_lost: mutable Application::InputEvent = zeroed
            focus_lost.kind <- Application::InputEventKind.FocusLost
            ActionInputRuntime::apply_application_event(&input, focus_lost)
            return ControlsCodes::FOCUS if not arrow_moves or ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            stick: i64 = Application::gamepad_axis_code(Application::GamepadAxisCode.LeftYNegative)
            ActionInput::apply(&input, device_event(ActionInput::Device.Gamepad, stick, 0.2))
            inside_deadzone: bool = ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            ActionInput::apply(&input, device_event(ActionInput::Device.Gamepad, stick, 0.8))
            stick_moves: bool = ActionInput::action_down(&input, CourseControls::ACTION_FORWARD)
            ActionInput::apply(&input, device_event(ActionInput::Device.Gamepad,
                Application::gamepad_button_code(Application::GamepadButtonCode.South), 1.0))
            return ControlsCodes::GAMEPAD_MAPPING if inside_deadzone or not stick_moves or not ActionInput::action_down(&input, CourseControls::ACTION_JUMP)
            0

        const module SoundCodes:
            START: i32 = 90
            ONCE_PER_TICK: i32 = 91
            GROUPS: i32 = 92
            PAUSE: i32 = 93
            MUSIC_ADVANCES: i32 = 94
            UNDERRUN: i32 = 95
            SCENE_RESET: i32 = 96
            RELEASE: i32 = 97
            REUSE: i32 = 98
            # Longer than the 500 ms stream ring, so music must be refilled.
            STREAM_NANOS: u64 = 1500000000
            MAX_PUMPS: usize = 1000000

        def audio_idle(session: RuntimeServices::Session&) -> bool:
            voices: usize = catch RuntimeServices::audio_active_voice_count(session):
                value: value
                error failure: 99
            streams: usize = catch AudioRuntime::active_stream_count():
                value: value
                error failure: 99
            voices == 0 and streams == 0

        # Events fire once per tick and respect their groups; pausing holds
        # effects while music streams through ordinary frame pumping without
        # underruns; a scene reset silences events and forgets history; stop
        # releases every clip, voice and stream so a second start reuses them.
        def sound_test(session: mutable RuntimeServices::Session&) -> i32:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return SoundCodes::START if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            return SoundCodes::ONCE_PER_TICK if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 1) != CourseSounds::PLAYED
            return SoundCodes::ONCE_PER_TICK if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 1) != CourseSounds::DUPLICATE
            return SoundCodes::GROUPS if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 2) != CourseSounds::REPLACED
            return SoundCodes::GROUPS if CourseSounds::fire(&sounds, CourseSounds::EVENT_LAND, 2) != CourseSounds::PLAYED
            return SoundCodes::GROUPS if CourseSounds::fire(&sounds, CourseSounds::EVENT_LAND, 5) != CourseSounds::COOLING
            return SoundCodes::GROUPS if CourseSounds::fire(&sounds, CourseSounds::EVENT_WIN, 5) != CourseSounds::PLAYED
            return SoundCodes::GROUPS if CourseSounds::fire(&sounds, CourseSounds::EVENT_FALL, 6) != CourseSounds::REPLACED
            return SoundCodes::PAUSE if not CourseSounds::set_paused(&sounds, true)
            before: AudioRuntime::StreamStatus = CourseSounds::music_status(&sounds)
            started_at: u64 = Application::uptime_nanos()
            elapsed: u64 =
                for _ in 0..<SoundCodes::MAX_PUMPS |waited: u64 = 0, session| -> waited:
                    pumped: bool = catch RuntimeServices::pump(session):
                        value: true
                        error failure: false
                    waited <- Application::uptime_nanos() - started_at
                    break SoundCodes::STREAM_NANOS * 2 if not pumped
                    break waited if waited >= SoundCodes::STREAM_NANOS
            return SoundCodes::PAUSE if elapsed > SoundCodes::STREAM_NANOS * 2 - 1 or elapsed < SoundCodes::STREAM_NANOS
            after: AudioRuntime::StreamStatus = CourseSounds::music_status(&sounds)
            return SoundCodes::MUSIC_ADVANCES if after.state != AudioRuntime::StreamState.Playing or after.frames_played <= before.frames_played
            return SoundCodes::UNDERRUN if after.underrun_frames != 0
            return SoundCodes::PAUSE if not CourseSounds::set_paused(&sounds, false)
            CourseSounds::reset_scene(&sounds)
            return SoundCodes::SCENE_RESET if CourseSounds::event_voices(&sounds) != 0
            return SoundCodes::SCENE_RESET if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 1) != CourseSounds::PLAYED
            return SoundCodes::RELEASE if not CourseSounds::stop(&sounds) or not audio_idle(session)
            return SoundCodes::REUSE if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            return SoundCodes::REUSE if CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, 1) != CourseSounds::PLAYED
            return SoundCodes::RELEASE if not CourseSounds::stop(&sounds) or not audio_idle(session)
            0
