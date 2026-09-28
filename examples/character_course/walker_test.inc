# Animated-guide checks for the character course self-test, included in the
# CharacterCourse private section after `links_test.inc`. A separate
# character walks the open floor north of the guide yard with a walker rig
# attached, so the expected distance and heading are known.
        const module WalkerCheck:
            OPEN_Z: f32 = 5.5
            PUSH_TICKS: usize = 60
            PUSH_SPEED: f32 = 2.0

        def heading_points(heading: Geometry::Quat, x: f32, z: f32) -> bool:
            forward: Geometry::Vec3 = Geometry::quat_rotate(heading, Geometry::Vec3{x: 0.0, y: 0.0, z: 1.0})
            forward.x * x + forward.z * z > 0.99 and ElisaMath::fabsf(forward.y) < 0.01

        def facing_test() -> i32:
            identity: Geometry::Quat = Geometry::quat_identity()
            east: Geometry::Quat = facing(2.0, 0.0, identity)
            south: Geometry::Quat = facing(0.0, -3.0, identity)
            diagonal: Geometry::Quat = facing(1.0, 1.0, identity)
            kept: Geometry::Quat = facing(0.0001, 0.0, east)
            return 131 if not heading_points(east, 1.0, 0.0) or not heading_points(south, 0.0, -1.0) or not heading_points(diagonal, 0.7071, 0.7071) or not heading_points(kept, 1.0, 0.0)
            0

        def fraction(value: f32) -> f32:
            value - ElisaMath::floorf(value)

        def walker_gone(walker: Walker&) -> bool:
            catch probe_progress(walker.rig):
                ok: false
                RenderScene::RenderSceneError.UnknownHandle: true
                error failure: false

        def walker_test(session: mutable RuntimeServices::Session&, guide: Guide&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            facing_status: i32 = facing_test()
            return facing_status if facing_status != 0
            before: usize = RenderScene::instance_count()
            spawn: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::POST_A_X, y: Tune::START_Y, z: WalkerCheck::OPEN_Z}
            character: PhysicsRuntime::CharacterHandle = try create_character_at(session, spawn, Tune::STANDING_HALF_HEIGHT)
            actor: Guide = Guide{character: character, body: guide.body, mesh: guide.mesh,
                outbound: true, door_body: guide.door_body, door_box: guide.door_box,
                door_open: guide.door_open}
            _ = try settle(session)
            start: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            walker: mutable Walker = try create_walker(start)
            return 132 if not walker.loaded or RenderScene::instance_count() != before + 1
            agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
            goal: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::POST_B_X, y: 0.0, z: WalkerCheck::OPEN_Z}
            _ = try plan_guide(session, actor, &agent, goal)
            state: mutable NavAgent::State = NavAgent::State.Moving
            for _ in 0..<600 |session, actor, agent, walker, state|:
                break if state != NavAgent::State.Moving
                state <- try step_guide(session, actor, agent)
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())
                moved: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                _ = try update_walker(&walker, moved, state == NavAgent::State.Moving)
            arrived: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            walked: f32 = NavRoute::horizontal_distance(start.position, arrived.position)
            expected: f32 = walked / WalkTune::STRIDE
            return 133 if state != NavAgent::State.Arrived or ElisaMath::fabsf(walker.cycles - expected) > 0.15
            played: f32 = try RenderScene::animation_progress(walker.rig)
            return 134 if not near_progress(played, fraction(walker.cycles))
            return 135 if not heading_points(walker.heading, 1.0, 0.0)
            # Pushed while not walking, the rig follows but holds its pose.
            north: Geometry::Vec3 = Geometry::Vec3{x: 0.0, y: 0.0, z: 1.0}
            for _ in 0..<WalkerCheck::PUSH_TICKS |session, character, walker, north|:
                try RuntimeServices::physics_character_move(session, character, north, WalkerCheck::PUSH_SPEED, 0.0, false)
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())
                pushed: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
                _ = try update_walker(&walker, pushed, false)
            held: f32 = try RenderScene::animation_progress(walker.rig)
            return 136 if held != played or walker.last.z < arrived.position.z + 0.5
            try release_walker(&walker)
            try RuntimeServices::physics_character_destroy(session, character)
            return 137 if walker.loaded or RenderScene::instance_count() != before or not walker_gone(&walker)
            0
