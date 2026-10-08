# Per-subsystem replay digests on the live course, included in its private
# section. A fresh character walks 16 recorded ticks; each tick records the
# input command (Exact), the character's millimetre pose (SameBuild), an
# AI watcher's brain (Exact) and a locomotion graph fed by the measured speed
# (SameBuild).
        const module ReplayTune:
            TICKS: i64 = 16
            NO_PUSH: i64 = -1
            PUSH_TICK: i64 = 5
            STEADY: i64 = 1
            SIDESTEP: i64 = 2
            WALK_BLEND_MS: i64 = 200
            SIGHT_M: f32 = 12.0

        # Locomotion graph: idle (0) to walk (1) from 500 mm/s, back below 200.
        def replay_graph(walk_blend_ms: i64) -> AnimGraph::Graph:
            AnimGraph::Graph{state_count: 2, entry: 0, count: 2, origin: [0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], target: [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], param: [0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], at_least: [500, 800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], blend_ms: [walk_blend_ms, 150, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]}

        def pose_digest(pose: PhysicsRuntime::BodyPose) -> i64:
            x: i64 = ElisaMath::round(pose.position.x.f64() * 1000.0).i64()
            y: i64 = ElisaMath::round(pose.position.y.f64() * 1000.0).i64()
            z: i64 = ElisaMath::round(pose.position.z.f64() * 1000.0).i64()
            ((x * 1000003 + y) % 1000000007) * 1000003 + z

        # Walks a new character forward, sidestepping on `push_tick`, and
        # returns the recorded trace.
        def replay_run(session: mutable RuntimeServices::Session&, push_tick: i64, walk_blend_ms: i64, sight_m: f32) -> ReplaySubsystems::Trace error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError] can[Global.Read, Global.Write]:
            # Away from the course's own character, which stands at the start.
            spawn: Geometry::Vec3 = Geometry::Vec3{x: LinksTune::WALKER_X, y: Tune::START_Y, z: LinksTune::WALKER_Z}
            character: PhysicsRuntime::CharacterHandle = try create_character_at(session, spawn, Tune::STANDING_HALF_HEIGHT)
            _ = try settle(session)
            trace: mutable ReplaySubsystems::Trace = ReplaySubsystems::new_trace()
            graph: AnimGraph::Graph = replay_graph(walk_blend_ms)
            motion: mutable AnimGraph::Runtime = AnimGraph::start(&graph)
            mutable tuning: AiBrain::Tuning = AiBrain::default_tuning()
            tuning.sight_range <- sight_m
            # The watcher stands 4 m behind the spawn, facing along the walk.
            watcher: mutable AiBrain::Brain = AiBrain::Brain()
            mutable last: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            step_ms: i64 = ElisaMath::round(Runtime::fixed_step_seconds().f64() * 1000.0).i64()
            for tick in 0..<ReplayTune::TICKS |session, character, trace, push_tick, motion, watcher, last|:
                command: i64 = ReplayTune::SIDESTEP if tick == push_tick else ReplayTune::STEADY
                heading: Geometry::Vec3 = Geometry::Vec3{x: 0.0, y: 0.0, z: 1.0} if command == ReplayTune::SIDESTEP else Geometry::Vec3{x: 1.0, y: 0.0, z: 0.0}
                try RuntimeServices::physics_character_move(session, character, heading, Tune::WALK_SPEED, 0.0, false)
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())
                pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                dx: f64 = (pose.position.x - last.position.x).f64()
                dz: f64 = (pose.position.z - last.position.z).f64()
                speed: i64 = ElisaMath::round(ElisaMath::sqrt(dx * dx + dz * dz) * 1000.0 / Runtime::fixed_step_seconds().f64()).i64()
                last <- pose
                params: i64[4] = [speed, 1000 - speed, 0, 0]
                _ = AnimGraph::step(&graph, &motion, &params, step_ms)
                percept: AiBrain::Percept = AiBrain::perceive(&tuning, LinksTune::WALKER_X - 4.0, LinksTune::WALKER_Z, 1.0, 0.0, pose.position.x, pose.position.z, 0.0, true)
                watcher <- AiBrain::think(watcher, &percept, &tuning, false, tick.u64()).brain
                _ = ReplaySubsystems::begin_tick(&trace)
                _ = ReplaySubsystems::record(&trace, 0, command)
                _ = ReplaySubsystems::record(&trace, 1, tick)
                _ = ReplaySubsystems::record(&trace, 2, pose_digest(pose))
                _ = ReplaySubsystems::record(&trace, 3, AiBrainDigest::brain_tick_digest(&watcher))
                _ = ReplaySubsystems::record(&trace, 4, AnimGraphDigest::graph_runtime_digest(&motion))
            try RuntimeServices::physics_character_destroy(session, character)
            trace

        # Saves `trace` under the course's trace key; false if storage failed.
        def save_trace(trace: ReplaySubsystems::Trace&) -> bool:
            stream: ReplaySubsystemsCodec::Stream = ReplaySubsystemsCodec::encode_trace(trace)
            mutable bytes: ReplaySubsystemsCodec::Bytes = ReplaySubsystemsCodec::pack_trace(&stream)
            catch UserData::write_payload("course-replay-trace", ReplaySubsystemsCodec::VERSION, &bytes.b[0], bytes.len):
                ok: true
                error failure: false

        # The saved trace, validated; any storage or decode failure is not Ok.
        def load_trace() -> ReplaySubsystemsCodec::Loaded:
            mutable bytes: ReplaySubsystemsCodec::Bytes = ReplaySubsystemsCodec::Bytes{b: zeroed, len: 0}
            mutable version: i64 = 0
            mutable length: usize = 0
            read: bool = catch UserData::read_payload("course-replay-trace", &version, &bytes.b[0], ReplaySubsystemsCodec::MAX_BYTES, &length):
                ok: true
                error failure: false
            return ReplaySubsystemsCodec::trace_failed(ReplaySubsystemsCodec::Decode.Truncated) if not read or version != ReplaySubsystemsCodec::VERSION
            bytes.len <- length
            stream: ReplaySubsystemsCodec::Stream = ReplaySubsystemsCodec::unpack_trace(&bytes)
            ReplaySubsystemsCodec::decode_trace(&stream)

        # 185: two same-input runs match in one build. 186: a sidestep on
        # tick 5 is named as an input divergence at tick 5, and a cross-build
        # comparison still sees it, because input digests are Exact.
        def replay_digest_test(session: mutable RuntimeServices::Session&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError] can[Global.Read, Global.Write]:
            first: ReplaySubsystems::Trace = try replay_run(session, ReplayTune::NO_PUSH, ReplayTune::WALK_BLEND_MS, ReplayTune::SIGHT_M)
            second: ReplaySubsystems::Trace = try replay_run(session, ReplayTune::NO_PUSH, ReplayTune::WALK_BLEND_MS, ReplayTune::SIGHT_M)
            return 185 if first.ticks != ReplayTune::TICKS or ReplaySubsystems::judge(&first, &second, true) != ReplaySubsystems::Verdict.Match
            pushed: ReplaySubsystems::Trace = try replay_run(session, ReplayTune::PUSH_TICK, ReplayTune::WALK_BLEND_MS, ReplayTune::SIGHT_M)
            found: ReplaySubsystems::Divergence = ReplaySubsystems::first_divergence(&first, &pushed, true)
            across: ReplaySubsystems::Divergence = ReplaySubsystems::first_divergence(&first, &pushed, false)
            return 186 if found.tick != ReplayTune::PUSH_TICK or found.subsystem != 0 or across.tick != ReplayTune::PUSH_TICK
            # 188: a blind watcher (same input and physics) is named as an AI
            # divergence; a slower walk blend as an animation divergence that
            # a cross-build comparison ignores.
            blind: ReplaySubsystems::Trace = try replay_run(session, ReplayTune::NO_PUSH, ReplayTune::WALK_BLEND_MS, 0.5)
            unseen: ReplaySubsystems::Divergence = ReplaySubsystems::first_divergence(&first, &blind, false)
            return 188 if unseen.tick < 0 or unseen.subsystem != 3
            slow: ReplaySubsystems::Trace = try replay_run(session, ReplayTune::NO_PUSH, 600, ReplayTune::SIGHT_M)
            blended: ReplaySubsystems::Divergence = ReplaySubsystems::first_divergence(&first, &slow, true)
            return 188 if blended.tick < 0 or blended.subsystem != 4 or ReplaySubsystems::judge(&first, &slow, false) != ReplaySubsystems::Verdict.Match
            # 187: the recorded run saves, reloads intact and still matches a
            # fresh run; once removed it no longer loads.
            return 187 if not save_trace(&first)
            reloaded: ReplaySubsystemsCodec::Loaded = load_trace()
            return 187 if reloaded.status != ReplaySubsystemsCodec::Decode.Ok or ReplaySubsystemsCodec::encode_trace(&reloaded.trace).len != ReplaySubsystemsCodec::encode_trace(&first).len
            return 187 if ReplaySubsystems::judge(&reloaded.trace, &second, true) != ReplaySubsystems::Verdict.Match
            removed: bool = catch UserData::remove_payload("course-replay-trace"):
                ok: true
                error failure: false
            return 187 if not removed or load_trace().status == ReplaySubsystemsCodec::Decode.Ok
            0
