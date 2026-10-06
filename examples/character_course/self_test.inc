# Finite checks for the character course, included in its private section.
        @callconv(c)
        extern elisa_audio_v1_test_fail_next_stream_stop() -> i32
        extern elisa_audio_v1_test_fail_next_voice_stop() -> i32

        const module CoursePathTune:
            SEGMENT_MAX_TICKS: usize = 180
            TUNNEL_EXIT_MARGIN: f32 = 0.05
            PLATFORM_APPROACH_MARGIN: f32 = 0.5
            GOAL_ENTRY_MARGIN: f32 = 0.05
            FALL_EDGE_MARGIN: f32 = 5.0
            FALL_SPAWN_INSET: f32 = 1.5
            MIN_JUMP_RISE: f32 = 0.04
            TUNNEL_EXIT_X: f32 = Tune::TUNNEL_X + Tune::TUNNEL_HALF_LENGTH + Tune::CHARACTER_RADIUS + TUNNEL_EXIT_MARGIN
            PLATFORM_TARGET_X: f32 = Tune::PLATFORM_CENTER_X + PLATFORM_APPROACH_MARGIN
            GOAL_TARGET_X: f32 = Tune::GOAL_X - Tune::GOAL_TOLERANCE + GOAL_ENTRY_MARGIN
            FALL_TARGET_X: f32 = Tune::FLOOR_HALF_WIDTH + Tune::CHARACTER_RADIUS + FALL_EDGE_MARGIN

        const module CoursePathCodes:
            STEP_NOT_CROSSED: i32 = 30
            STEP_RISE: i32 = 31
            STEP_PROGRESS: i32 = 32
            CROUCH_REJECTED: i32 = 33
            TUNNEL_BLOCKED: i32 = 34
            STAND_REJECTED: i32 = 35
            NOT_GROUNDED: i32 = 36
            JUMP_MISSING: i32 = 37
            PLATFORM_MISSED: i32 = 38
            GOAL_MISSED: i32 = 39
            FALL_MISSED: i32 = 40
            LANE_MISSED: i32 = 41

        def walk_course_until(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle, state: mutable CourseState::State&,
            direction: Geometry::Vec3, x_axis: bool, target: f32) -> PhysicsRuntime::BodyPose error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            pose: mutable PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            for _ in 0..<CoursePathTune::SEGMENT_MAX_TICKS |session, character, state, direction, x_axis, target, pose|:
                reached: bool = pose.position.x >= target if x_axis else pose.position.z >= target
                break if reached or CourseState::phase(state) != CourseState::Phase.Playing
                try RuntimeServices::physics_character_move(session, character,
                    direction, Tune::WALK_SPEED, 0.0, false)
                _ = try RuntimeServices::physics_fixed_step(session,
                    Runtime::fixed_step_seconds())
                pose <- try RuntimeServices::physics_character_pose(session, character)
                CourseState::update_outcome(state,
                    pose.position.y < Tune::FAILURE_Y,
                    ElisaMath::fabsf(pose.position.x - Tune::GOAL_X) < Tune::GOAL_TOLERANCE and ElisaMath::fabsf(pose.position.z - Tune::GOAL_Z) < Tune::GOAL_TOLERANCE)
            pose

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
            return CoursePathCodes::STEP_NOT_CROSSED if not crossed_step
            return CoursePathCodes::STEP_RISE if max_height < initial.position.y + Tune::SELF_TEST_MIN_STEP_RISE
            final_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(
                session, character)
            return CoursePathCodes::STEP_PROGRESS if final_pose.position.x < initial.position.x + Tune::SELF_TEST_MIN_PROGRESS

            crouched: bool = try RuntimeServices::physics_character_set_capsule(session,
                character, Tune::CHARACTER_RADIUS, Tune::CROUCHED_HALF_HEIGHT)
            return CoursePathCodes::CROUCH_REJECTED if not crouched
            state: mutable CourseState::State = CourseState::State()
            forward: Geometry::Vec3 = Geometry::Vec3{x: 1.0, y: 0.0, z: 0.0}
            tunnel_pose: PhysicsRuntime::BodyPose = try walk_course_until(session,
                character, state, forward, true, CoursePathTune::TUNNEL_EXIT_X)
            return CoursePathCodes::TUNNEL_BLOCKED if tunnel_pose.position.x < CoursePathTune::TUNNEL_EXIT_X
            standing: bool = try RuntimeServices::physics_character_set_capsule(session,
                character, Tune::CHARACTER_RADIUS, Tune::STANDING_HALF_HEIGHT)
            return CoursePathCodes::STAND_REJECTED if not standing
            ground: PhysicsRuntime::CharacterGround = try RuntimeServices::physics_character_ground(session, character)
            return CoursePathCodes::NOT_GROUNDED if ground.state != PhysicsRuntime::CharacterGroundState.OnGround

            jump_start: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            try RuntimeServices::physics_character_move(session, character,
                forward, Tune::WALK_SPEED, Tune::JUMP_SPEED, false)
            _ = try RuntimeServices::physics_fixed_step(session,
                Runtime::fixed_step_seconds())
            jump_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            CourseState::update_outcome(&state,
                jump_pose.position.y < Tune::FAILURE_Y,
                ElisaMath::fabsf(jump_pose.position.x - Tune::GOAL_X) < Tune::GOAL_TOLERANCE and ElisaMath::fabsf(jump_pose.position.z - Tune::GOAL_Z) < Tune::GOAL_TOLERANCE)
            return CoursePathCodes::JUMP_MISSING if jump_pose.position.y < jump_start.position.y + CoursePathTune::MIN_JUMP_RISE

            platform_pose: PhysicsRuntime::BodyPose = try walk_course_until(session,
                character, state, forward, true, CoursePathTune::PLATFORM_TARGET_X)
            return CoursePathCodes::PLATFORM_MISSED if platform_pose.position.x < CoursePathTune::PLATFORM_TARGET_X or platform_pose.position.y < Tune::PLATFORM_CENTER_Y
            lane: Geometry::Vec3 = Geometry::Vec3{x: 0.0, y: 0.0, z: 1.0}
            lane_pose: PhysicsRuntime::BodyPose = try walk_course_until(session,
                character, state, lane, false, Tune::GOAL_Z)
            return CoursePathCodes::LANE_MISSED if lane_pose.position.z < Tune::GOAL_Z
            goal_pose: PhysicsRuntime::BodyPose = try walk_course_until(session,
                character, state, forward, true, CoursePathTune::GOAL_TARGET_X)
            return CoursePathCodes::GOAL_MISSED if CourseState::phase(state) != CourseState::Phase.Won or ElisaMath::fabsf(goal_pose.position.x - Tune::GOAL_X) >= Tune::GOAL_TOLERANCE or ElisaMath::fabsf(goal_pose.position.z - Tune::GOAL_Z) >= Tune::GOAL_TOLERANCE

            fall_spawn: Geometry::Vec3 = Geometry::Vec3{
                x: Tune::FLOOR_HALF_WIDTH - CoursePathTune::FALL_SPAWN_INSET,
                y: Tune::START_Y, z: 0.0}
            fall_character: PhysicsRuntime::CharacterHandle = try create_character_at(
                session, fall_spawn, Tune::STANDING_HALF_HEIGHT)
            for _ in 0..<Tune::INITIAL_STEP_COUNT |session|:
                _ = try RuntimeServices::physics_fixed_step(session,
                    Runtime::fixed_step_seconds())
            fall_state: mutable CourseState::State = CourseState::State()
            fall_pose: PhysicsRuntime::BodyPose = try walk_course_until(session,
                fall_character, fall_state, forward, true, CoursePathTune::FALL_TARGET_X)
            fell: bool = CourseState::phase(fall_state) == CourseState::Phase.Failed and fall_pose.position.y < Tune::FAILURE_Y
            try RuntimeServices::physics_character_destroy(session, fall_character)
            return CoursePathCodes::FALL_MISSED if not fell
            0

        def state_test() -> i32:
            state: mutable CourseState::State = CourseState::State()
            CourseState::toggle_pause(&state)
            return Tune::STATE_TEST_PAUSE_CODE if CourseState::phase(state) != CourseState::Phase.Paused
            CourseState::toggle_pause(&state)
            return Tune::STATE_TEST_RESUME_CODE if CourseState::phase(state) != CourseState::Phase.Playing
            CourseState::update_outcome(&state, true, false)
            return Tune::STATE_TEST_FAIL_CODE if CourseState::phase(state) != CourseState::Phase.Failed
            CourseState::update_outcome(&state, false, true)
            return Tune::STATE_TEST_FAIL_LOCK_CODE if CourseState::phase(state) != CourseState::Phase.Failed
            CourseState::restart(&state)
            return Tune::STATE_TEST_FAIL_RESTART_CODE if CourseState::phase(state) != CourseState::Phase.Playing
            CourseState::update_outcome(&state, false, true)
            return Tune::STATE_TEST_WIN_CODE if CourseState::phase(state) != CourseState::Phase.Won
            CourseState::update_outcome(&state, true, false)
            return Tune::STATE_TEST_WIN_LOCK_CODE if CourseState::phase(state) != CourseState::Phase.Won
            CourseState::toggle_pause(&state)
            return Tune::STATE_TEST_TERMINAL_PAUSE_CODE if CourseState::phase(state) != CourseState::Phase.Won
            CourseState::restart(&state)
            return Tune::STATE_TEST_WIN_RESTART_CODE if CourseState::phase(state) != CourseState::Phase.Playing
            0

        struct ProgressOutcome:
            status: i32
            character: PhysicsRuntime::CharacterHandle

        # The checkpoint chain is valid and upgrades to the schema it writes.
        def chain_matches_schema() -> bool:
            chain: SaveMigrations::Chain = CourseProgress::checkpoint_chain()
            SaveMigrations::check_chain(&chain) == SaveMigrations::Check.Ok and SaveMigrations::chain_version(&chain) == CourseProgress::SCHEMA

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
            state: mutable CourseState::State = CourseState::State()
            return ProgressOutcome{status: Tune::PROGRESS_SAVE_CODE, character: character} if not CourseState::restore(&state, CourseState::PHASE_PAUSED, Tune::SELF_TEST_ATTEMPTS)
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            saved: CourseProgress::Checkpoint = checkpoint_of(state, pose, false)
            try CourseProgress::save(&saved)
            current: mutable PhysicsRuntime::CharacterHandle = try restart_character(session, character)
            CourseState::restart(&state)
            loaded: CourseProgress::LoadResult = CourseProgress::load()
            return ProgressOutcome{status: Tune::PROGRESS_LOAD_CODE, character: current} if progress_status(loaded.status) != 0 or loaded.checkpoint.attempts != Tune::SELF_TEST_ATTEMPTS or phase_from_code(loaded.checkpoint.phase) != CourseState::Phase.Paused
            current <- try apply_checkpoint(session, current, loaded.checkpoint)
            restored: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
            return ProgressOutcome{status: Tune::PROGRESS_RESTORE_CODE, character: current} if not near(restored.position, pose.position)
            unknown_phase: CourseProgress::Checkpoint = CourseProgress::Checkpoint{
                phase: 9, position: pose.position, crouched: false, attempts: 1}
            try CourseProgress::save(&unknown_phase)
            return ProgressOutcome{status: Tune::PROGRESS_MALFORMED_CODE, character: current} if progress_status(CourseProgress::load().status) != 3
            zero_attempts: CourseProgress::Checkpoint = CourseProgress::Checkpoint{
                phase: CourseProgress::PHASE_PLAYING, position: pose.position,
                crouched: false, attempts: 0}
            try CourseProgress::save(&zero_attempts)
            return ProgressOutcome{status: Tune::PROGRESS_MALFORMED_CODE, character: current} if progress_status(CourseProgress::load().status) != 3
            values: i64[Save::MAX_FIELDS] = zeroed
            short_record: Save::Blob = try Save::save_write(values, 2)
            try UserData::write_blob(CourseProgress::key(), &short_record)
            return ProgressOutcome{status: Tune::PROGRESS_SHAPE_CODE, character: current} if progress_status(CourseProgress::load().status) != 3
            # Schema migration: an older release's six-field record loads
            # (marked migrated), a current save is not, and a tampered
            # checksum or unknown schema is rejected.
            older: Save::Blob = try CourseProgress::encode_legacy(&saved)
            try UserData::write_blob(CourseProgress::key(), &older)
            migrated: CourseProgress::LoadResult = CourseProgress::load()
            try CourseProgress::save(&saved)
            current_load: CourseProgress::LoadResult = CourseProgress::load()
            return ProgressOutcome{status: Tune::PROGRESS_MIGRATE_CODE, character: current} if not chain_matches_schema() or progress_status(migrated.status) != 0 or not migrated.migrated or migrated.checkpoint.attempts != Tune::SELF_TEST_ATTEMPTS or progress_status(current_load.status) != 0 or current_load.migrated
            tampered: mutable Save::Blob = try CourseProgress::encode(&saved)
            tampered.fields[7] <- tampered.fields[7] + 1
            try UserData::write_blob(CourseProgress::key(), &tampered)
            bad_sum: i32 = progress_status(CourseProgress::load().status)
            future: mutable Save::Blob = try CourseProgress::encode(&saved)
            future.fields[1] <- CourseProgress::SCHEMA + 1
            try UserData::write_blob(CourseProgress::key(), &future)
            bad_schema: i32 = progress_status(CourseProgress::load().status)
            return ProgressOutcome{status: Tune::PROGRESS_CHECKSUM_CODE, character: current} if bad_sum != 3 or bad_schema != 3
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
                CourseState::restart(&state)
                cycle_loaded: CourseProgress::LoadResult = CourseProgress::load()
                if progress_status(cycle_loaded.status) == 0:
                    current <- try apply_checkpoint(session, current, cycle_loaded.checkpoint)
                    state_restored: bool = CourseState::restore(&state, cycle_loaded.checkpoint.phase, cycle_loaded.checkpoint.attempts)
                    if state_restored:
                        cycle_restored: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, current)
                        cycle_status <- Tune::PROGRESS_CYCLE_CODE if not near(cycle_restored.position, restored.position)
                    else:
                        cycle_status <- Tune::PROGRESS_CYCLE_CODE
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
            default_pad: CoursePad::Pad = CoursePad::defaults()
            return ControlsCodes::REBIND_INPUT if not CourseControls::bind(&input, &remapped, &default_pad)
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
            MUSIC_REWIND: i32 = 193
            VIRTUAL: i32 = 194
            TRIGGERS: i32 = 195
            # 100 ms at 48 kHz: the realized ambience must land this close.
            DRIFT_FRAMES: u64 = 4800
            # 0.25 s at 48 kHz: more than one device period, well under 1.5 s.
            REWOUND_FRAMES: u64 = 12000
            PAST_END: u64 = 18446744073709551615
            # Longer than the 500 ms stream ring, so music must be refilled.
            STREAM_NANOS: u64 = 1500000000
            MAX_PUMPS: usize = 1000000

        # 0 seeked, 1 out of range, 2 stale handle, 3 any other error.
        def seek_outcome(stream: AudioRuntime::StreamHandle, frame: u64) -> i32:
            catch AudioRuntime::seek_stream(stream, frame):
                ok: 0
                AudioRuntime::AudioError.InvalidArgument: 1
                AudioRuntime::AudioError.InvalidHandle: 2
                error failure: 3

        def set_effects_budget(budget: u32) -> bool:
            catch AudioRuntime::set_voice_budget(AudioRuntime::Bus.Effects, budget):
                ok: true
                error failure: false

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
            # The authored triggers: a step per 0.65 m walked, and a landing
            # only after a drop of 0.3 m or more.
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, true, Geometry::Vec3{x: 0.0, y: 0.0, z: 0.0}, 20) != 0
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, true, Geometry::Vec3{x: 0.4, y: 0.0, z: 0.0}, 21) != 0
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, true, Geometry::Vec3{x: 0.8, y: 0.0, z: 0.0}, 22) != CourseSounds::TRACKED_STEP
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, false, Geometry::Vec3{x: 0.8, y: 0.2, z: 0.0}, 23) != 0
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, true, Geometry::Vec3{x: 0.8, y: 0.0, z: 0.0}, 24) != 0
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, false, Geometry::Vec3{x: 0.8, y: 2.0, z: 0.0}, 25) != 0
            return SoundCodes::TRIGGERS if CourseSounds::track(&sounds, true, Geometry::Vec3{x: 0.8, y: 0.0, z: 0.0}, 40) != CourseSounds::TRACKED_LANDING
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
            played_to: u64 = CourseSounds::music_position(&sounds)
            return SoundCodes::MUSIC_REWIND if played_to < SoundCodes::REWOUND_FRAMES or not CourseSounds::rewind_music(&sounds)
            return SoundCodes::MUSIC_REWIND if CourseSounds::music_position(&sounds) >= SoundCodes::REWOUND_FRAMES
            return SoundCodes::MUSIC_REWIND if seek_outcome(CourseSounds::music_stream(&sounds), SoundCodes::PAST_END) != 1
            music: AudioRuntime::StreamHandle = CourseSounds::music_stream(&sounds)
            return SoundCodes::PAUSE if not CourseSounds::set_paused(&sounds, false)
            CourseSounds::reset_scene(&sounds)
            return SoundCodes::SCENE_RESET if CourseSounds::event_voices(&sounds) != 0
            return SoundCodes::VIRTUAL if not CourseSounds::update(&sounds) or CourseSounds::ambience_virtual(&sounds)
            return SoundCodes::VIRTUAL if not set_effects_budget(1)
            return SoundCodes::VIRTUAL if CourseSounds::fire(&sounds, CourseSounds::EVENT_LAND, 10) != CourseSounds::PLAYED
            return SoundCodes::VIRTUAL if CourseSounds::update(&sounds) or not CourseSounds::ambience_virtual(&sounds)
            CourseSounds::reset_scene(&sounds)
            return SoundCodes::VIRTUAL if not CourseSounds::update(&sounds) or CourseSounds::ambience_virtual(&sounds)
            return SoundCodes::VIRTUAL if CourseSounds::ambience_drift(&sounds) > SoundCodes::DRIFT_FRAMES
            return SoundCodes::VIRTUAL if not set_effects_budget(32)
            return SoundCodes::SCENE_RESET if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 1) != CourseSounds::PLAYED
            return SoundCodes::RELEASE if not CourseSounds::stop(&sounds) or not audio_idle(session)
            return SoundCodes::MUSIC_REWIND if seek_outcome(music, 0) != 2
            return SoundCodes::REUSE if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            return SoundCodes::REUSE if CourseSounds::fire(&sounds, CourseSounds::EVENT_SAVE, 1) != CourseSounds::PLAYED
            return SoundCodes::RELEASE if not CourseSounds::stop(&sounds) or not audio_idle(session)
            return SoundCodes::REUSE if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            return SoundCodes::REUSE if CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, 2) != CourseSounds::PLAYED
            return SoundCodes::RELEASE if elisa_audio_v1_test_fail_next_voice_stop() != 0
            return SoundCodes::RELEASE if elisa_audio_v1_test_fail_next_stream_stop() != 0
            return SoundCodes::RELEASE if CourseSounds::stop(&sounds)
            remaining_voices: usize = catch RuntimeServices::audio_active_voice_count(session):
                value: value
                error failure: 99
            remaining_streams: usize = catch AudioRuntime::active_stream_count():
                value: value
                error failure: 99
            return SoundCodes::RELEASE if remaining_voices != 1 or remaining_streams != 1
            return SoundCodes::RELEASE if not CourseSounds::stop(&sounds) or not audio_idle(session)
            0
