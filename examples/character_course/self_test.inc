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
