# Lifecycle stress for the character-course self-test (Q06a), included in the
# CharacterCourse private section after `audio_mix_test.inc`. Every iteration
# restarts the character, resizes the view, builds and releases the beacon
# world, and starts, pauses, resumes and stops sound. The first iteration is
# warm-up; every later one must end on the same live resource counts, and a
# drift reports 180 plus the iteration as its trace. Injected failures follow.
        const STRESS_ITERATIONS: usize = 12

        struct StressCounts:
            instances: usize
            navmeshes: usize
            voices: usize
            streams: usize

        def stress_counts(session: RuntimeServices::Session&) -> StressCounts:
            voices: usize = catch RuntimeServices::audio_active_voice_count(session):
                value: value
                error failure: 999
            streams: usize = catch AudioRuntime::active_stream_count():
                value: value
                error failure: 999
            StressCounts{instances: RenderScene::instance_count(), navmeshes: NavMesh::live_count(), voices: voices, streams: streams}

        def same_counts(left: StressCounts, right: StressCounts) -> bool:
            left.instances == right.instances and left.navmeshes == right.navmeshes and left.voices == right.voices and left.streams == right.streams

        def stress_width(iteration: usize) -> i32:
            if iteration % 3 == 0:
                640
            elif iteration % 3 == 1:
                1920
            else:
                1024

        def stale_character(session: RuntimeServices::Session&, character: PhysicsRuntime::CharacterHandle) -> bool:
            catch RuntimeServices::physics_character_pose(session, character):
                value: false
                error failure: true

        def sound_cycle(session: mutable RuntimeServices::Session&, iteration: usize) -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            return false if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
            fired: i32 = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, iteration.u64() + 1)
            paused: bool = CourseSounds::set_paused(&sounds, true) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PAUSED, 10)
            resumed: bool = CourseSounds::set_paused(&sounds, false) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PLAY, 20)
            stopped: bool = CourseSounds::stop(&sounds)
            fired == CourseSounds::PLAYED and paused and resumed and stopped and audio_idle(session)

        def stress_cycle(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle, iteration: usize) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError]:
            restarted: PhysicsRuntime::CharacterHandle = try restart_character(session, character)
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, restarted)
            start: Geometry::Vec3 = Geometry::Vec3{x: Tune::START_X, y: Tune::START_Y, z: Tune::START_Z}
            return ProgressOutcome{status: 172, character: restarted} if not stale_character(session, character) or not pose_matches(pose, start)
            width: i32 = stress_width(iteration)
            try RenderScene::resize(width, width * 5 / 8)
            beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                value: value
                error failure: return ProgressOutcome{status: 173, character: restarted}
            lit: usize = try CourseBeacons::update(&beacons, CourseBeacons::anchor(iteration % 3))
            try CourseBeacons::release(&beacons)
            return ProgressOutcome{status: 173, character: restarted} if lit != 1
            return ProgressOutcome{status: 174, character: restarted} if not sound_cycle(session, iteration)
            _ = try RuntimeServices::pump(session)
            ProgressOutcome{status: 0, character: restarted}

        def resize_rejected(width: i32, height: i32) -> bool:
            catch RenderScene::resize(width, height):
                ok: false
                error failure: true

        # A failed start keeps what it loaded until `stop`, which releases it.
        def injected_sound(session: mutable RuntimeServices::Session&, inject: i32, expected: i32) -> bool:
            sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
            started: i32 = CourseSounds::start_injected(&sounds, session, inject)
            stopped: bool = CourseSounds::stop(&sounds)
            started == expected and stopped and audio_idle(session)

        def injected_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle, width: i32, height: i32) -> i32:
            return 176 if not resize_rejected(0, height) or not resize_rejected(width, -1) or resize_rejected(width, height)
            return 177 if not injected_sound(session, CourseSounds::INJECT_CLIP, CourseSounds::CLIP_FAILED) or not injected_sound(session, CourseSounds::INJECT_EVENTS, CourseSounds::DEFINITION_FAILED)
            return 178 if not injected_sound(session, CourseSounds::INJECT_MUSIC, CourseSounds::MUSIC_FAILED) or not sound_cycle(session, 0)
            destroyed: bool = catch RuntimeServices::physics_character_destroy(session, character):
                ok: true
                error failure: false
            return 179 if destroyed
            0

        def stress_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError]:
            frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
            width: i32 = frame.pixel_width if frame.pixel_width > 0 else 1280
            height: i32 = frame.pixel_height if frame.pixel_height > 0 else 800
            warm: ProgressOutcome = try stress_cycle(session, character, 0)
            return warm if warm.status != 0
            baseline: StressCounts = stress_counts(session)
            current: mutable PhysicsRuntime::CharacterHandle = warm.character
            status: mutable i32 = 0
            previous: mutable PhysicsRuntime::CharacterHandle = character
            for iteration in 1..<STRESS_ITERATIONS |session, current, status, previous, baseline|:
                if status == 0:
                    previous <- current
                    cycled: ProgressOutcome = catch stress_cycle(session, current, iteration):
                        value: value
                        error failure: ProgressOutcome{status: 175, character: current}
                    current <- cycled.character
                    counts: StressCounts = stress_counts(session)
                    status <- cycled.status
                    status <- 180 + iteration.i32() if status == 0 and not same_counts(counts, baseline)
            return ProgressOutcome{status: status, character: current} if status != 0
            try RenderScene::resize(width, height)
            injected: i32 = injected_test(session, previous, width, height)
            final: StressCounts = stress_counts(session)
            return ProgressOutcome{status: injected, character: current} if injected != 0
            ProgressOutcome{status: 192 if not same_counts(final, baseline) else 0, character: current}
