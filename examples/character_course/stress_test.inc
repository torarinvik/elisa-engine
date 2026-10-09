# Lifecycle stress for the character-course self-test (Q06a), included in the
# CharacterCourse private section after `audio_mix_test.inc`. Every iteration
# restarts the character, resizes the view, builds and releases the beacon
# world, and starts, pauses, resumes and stops sound. The first iteration is
# warm-up; every later one must end on the same live resource counts, and a
# drift reports 180 plus the iteration as its trace. Injected failures follow.
        const STRESS_ITERATIONS: usize = 12
        # Heap sampling starts after this iteration, once lazy caches are warm.
        const HEAP_SETTLED_ITERATION: usize = 3
        # Allowed heap growth over the remaining iterations; measured < 16 KiB.
        const HEAP_GROWTH_LIMIT: u64 = 262144
        # Frames pumped before the final heap sample; the lowest sample counts,
        # so allocations held briefly by worker threads under load drain first.
        const HEAP_SETTLE_FRAMES: usize = 8

        # Test-probe hook: resizes the real SDL window and syncs it.
        @callconv(c)
        extern elisa_application_v1_test_set_window_size(width: i32, height: i32) -> i32
        # Test-probe hook: one retained trace line per stress iteration.
        @callconv(c)
        extern elisa_application_v1_test_trace_stress(iteration: i32, instances: i64, navmeshes: i64, voices: i64, streams: i64, heap_bytes: i64)

        # Resizes the OS window, pumps one frame and checks that the frame
        # reports the requested logical size and a new resize serial.
        def window_resized(session: mutable RuntimeServices::Session&, width: i32, height: i32) -> bool error[RuntimeServices::RuntimeServicesError]:
            before: Application::FrameInfo = try RuntimeServices::frame_info(session)
            return false if elisa_application_v1_test_set_window_size(width, height) != 0
            _ = try RuntimeServices::pump(session)
            after: Application::FrameInfo = try RuntimeServices::frame_info(session)
            after.logical_width == width and after.logical_height == height and after.resize_serial > before.resize_serial

        struct StressCounts:
            instances: usize
            navmeshes: usize
            voices: usize
            streams: usize

        def pumped_heap(session: mutable RuntimeServices::Session&) -> u64 error[RuntimeServices::RuntimeServicesError]:
            _ = try RuntimeServices::pump(session)
            RenderScene::heap_bytes_in_use()

        def settled_heap(session: mutable RuntimeServices::Session&) -> u64:
            lowest: mutable u64 = RenderScene::heap_bytes_in_use()
            for frame in 0..<HEAP_SETTLE_FRAMES |session, lowest|:
                sample: u64 = catch pumped_heap(session):
                    value: value
                    error failure: lowest
                lowest <- sample if sample < lowest
            lowest

        def stress_counts(session: RuntimeServices::Session&) -> StressCounts:
            voices: usize = catch RuntimeServices::audio_active_voice_count(session):
                value: value
                error failure: 999
            streams: usize = catch AudioRuntime::active_stream_count():
                value: value
                error failure: 999
            StressCounts{instances: RenderScene::instance_count(), navmeshes: NavMesh::live_count(), voices: voices, streams: streams}

        def trace(iteration: i32, counts: StressCounts, heap_bytes: u64):
            elisa_application_v1_test_trace_stress(iteration, counts.instances.i64(), counts.navmeshes.i64(), counts.voices.i64(), counts.streams.i64(), heap_bytes.i64())

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

        def sound_cycle(session: mutable RuntimeServices::Session&, iteration: usize) -> bool can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                sounds: mutable CourseSounds::Sounds = CourseSounds::Sounds()
                return false if CourseSounds::start(&sounds, session) != CourseSounds::STARTED
                fired: i32 = CourseSounds::fire(&sounds, CourseSounds::EVENT_JUMP, iteration.u64() + 1)
                paused: bool = CourseSounds::set_paused(&sounds, true) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PAUSED, 10)
                resumed: bool = CourseSounds::set_paused(&sounds, false) and CourseSounds::mix(&sounds, CourseSounds::MOOD_PLAY, 20)
                stopped: bool = CourseSounds::stop(&sounds)
                return fired == CourseSounds::PLAYED and paused and resumed and stopped and audio_idle(session)

        def stress_cycle(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle, iteration: usize) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                restarted: PhysicsRuntime::CharacterHandle = try restart_character(session, character)
                pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, restarted)
                start: Geometry::Vec3 = Geometry::Vec3{x: Tune::START_X, y: Tune::START_Y, z: Tune::START_Z}
                return ProgressOutcome{status: 172, character: restarted} if not stale_character(session, character) or not pose_matches(pose, start)
                width: i32 = stress_width(iteration)
                window_ok: bool = try window_resized(session, width * 3 / 8, width * 15 / 64)
                return ProgressOutcome{status: 195, character: restarted} if not window_ok
                try RenderScene::resize(width, width * 5 / 8)
                beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                    value: value
                    error failure: return ProgressOutcome{status: 173, character: restarted}
                started: i32 = catch CourseBeacons::start(&beacons):
                    value: value
                    error failure: -1
                return ProgressOutcome{status: 173, character: restarted} if started != 0
                lit: usize = try CourseBeacons::update(&beacons, CourseBeacons::anchor(iteration % 3))
                try CourseBeacons::release(&beacons)
                return ProgressOutcome{status: 173, character: restarted} if lit != 1
                return ProgressOutcome{status: 174, character: restarted} if not sound_cycle(session, iteration)
                _ = try RuntimeServices::pump(session)
                return ProgressOutcome{status: 0, character: restarted}

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
            character: PhysicsRuntime::CharacterHandle, width: i32, height: i32) -> i32 can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                return 176 if not resize_rejected(0, height) or not resize_rejected(width, -1) or resize_rejected(width, height)
                return 177 if not injected_sound(session, CourseSounds::INJECT_CLIP, CourseSounds::CLIP_FAILED) or not injected_sound(session, CourseSounds::INJECT_EVENTS, CourseSounds::DEFINITION_FAILED)
                return 178 if not injected_sound(session, CourseSounds::INJECT_MUSIC, CourseSounds::MUSIC_FAILED) or not sound_cycle(session, 0)
                destroyed: bool = catch RuntimeServices::physics_character_destroy(session, character):
                    ok: true
                    error failure: false
                return 179 if destroyed
                return 0

        def stress_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                frame: Application::FrameInfo = try RuntimeServices::frame_info(session)
                width: i32 = frame.pixel_width if frame.pixel_width > 0 else 1280
                height: i32 = frame.pixel_height if frame.pixel_height > 0 else 800
                warm: ProgressOutcome = try stress_cycle(session, character, 0)
                return warm if warm.status != 0
                baseline: StressCounts = stress_counts(session)
                trace(0, baseline, RenderScene::heap_bytes_in_use())
                current: mutable PhysicsRuntime::CharacterHandle = warm.character
                status: mutable i32 = 0
                previous: mutable PhysicsRuntime::CharacterHandle = character
                heap_settled: mutable u64 = 0
                for iteration in 1..<STRESS_ITERATIONS |session, current, status, previous, baseline, heap_settled|:
                    if status == 0:
                        previous <- current
                        cycled: ProgressOutcome = catch stress_cycle(session, current, iteration):
                            value: value
                            error failure: ProgressOutcome{status: 175, character: current}
                        current <- cycled.character
                        counts: StressCounts = stress_counts(session)
                        status <- cycled.status
                        trace(iteration.i32(), counts, RenderScene::heap_bytes_in_use())
                        status <- 180 + iteration.i32() if status == 0 and not same_counts(counts, baseline)
                        heap_settled <- RenderScene::heap_bytes_in_use() if iteration == HEAP_SETTLED_ITERATION
                return ProgressOutcome{status: status, character: current} if status != 0
                heap_end: u64 = settled_heap(session)
                trace(-1, stress_counts(session), heap_end)
                return ProgressOutcome{status: 194, character: current} if heap_settled == 0 or heap_end == 0
                return ProgressOutcome{status: 193, character: current} if heap_end > heap_settled + HEAP_GROWTH_LIMIT
                logical_width: i32 = frame.logical_width if frame.logical_width > 0 else 320
                logical_height: i32 = frame.logical_height if frame.logical_height > 0 else 200
                restored: bool = try window_resized(session, logical_width, logical_height)
                return ProgressOutcome{status: 196, character: current} if not restored
                try RenderScene::resize(width, height)
                injected: i32 = injected_test(session, previous, width, height)
                final: StressCounts = stress_counts(session)
                return ProgressOutcome{status: injected, character: current} if injected != 0
                return ProgressOutcome{status: 192 if not same_counts(final, baseline) else 0, character: current}
