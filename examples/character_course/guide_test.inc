# Navigation and animation checks for the character course self-test,
# included in the CharacterCourse private section after `guide.inc`.
        struct GuideWalk:
            state: NavAgent::State
            ticks: usize
            # False if the guide ever crossed the wall's line inside its span.
            clear_of_wall: bool
            final: Geometry::Vec3

        def settle(session: mutable RuntimeServices::Session&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            for _ in 0..<Tune::INITIAL_STEP_COUNT |session|:
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())

        # Step the physics world with the guide steering until its agent
        # stops moving or `ticks` run out.
        def walk_guide(session: mutable RuntimeServices::Session&, guide: Guide&,
            agent: mutable NavAgent::Agent&, ticks: usize) -> GuideWalk error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            state: mutable NavAgent::State = NavAgent::state(agent)
            used: mutable usize = 0
            clear: mutable bool = true
            start: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
            previous: mutable Geometry::Vec3 = start.position
            wall_end: f32 = GuideTune::WALL_Z + GuideTune::WALL_HALF_DEPTH
            for _ in 0..<ticks |session, guide, agent, state, used, clear, previous, wall_end|:
                break if state != NavAgent::State.Moving
                state <- try step_guide(session, guide, agent)
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())
                used <- used + 1
                pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
                crossed: bool = (previous.x - GuideTune::WALL_X) * (pose.position.x - GuideTune::WALL_X) <= 0.0
                clear <- false if crossed and pose.position.z < wall_end
                previous <- pose.position
            GuideWalk{state: state, ticks: used, clear_of_wall: clear, final: previous}

        def route_passes_wall_end(agent: NavAgent::Agent&) -> bool:
            wall_end: f32 = GuideTune::WALL_Z + GuideTune::WALL_HALF_DEPTH
            for index in 0..<NavRoute::MAX_POINTS |passes: bool = false| -> passes:
                break if index >= agent.route.count
                passes <- true if agent.route.points[index].z > wall_end

        def inside_pen(point: Geometry::Vec3) -> bool:
            ElisaMath::fabsf(point.x - GuideTune::PEN_X) < GuideTune::PEN_HALF_WIDTH and ElisaMath::fabsf(point.z - GuideTune::PEN_Z) < GuideTune::PEN_HALF_DEPTH

        def probe_route(mesh: NavMesh::Handle) -> void error[NavMesh::NavMeshError]:
            _ = try NavMesh::find_route(mesh, guide_post(false), guide_post(true), query_extents())

        def is_stale(mesh: NavMesh::Handle) -> bool:
            catch probe_route(mesh):
                ok: false
                NavMesh::NavMeshError.StaleHandle: true
                error failure: false

        def guide_test(session: mutable RuntimeServices::Session&, guide: mutable Guide&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, NavMesh::NavMeshError]:
            return 100 if not NavMesh::live(guide.mesh) or NavMesh::live_count() != 1
            _ = try settle(session)
            agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
            outbound: NavAgent::State = try plan_guide(session, guide, &agent, guide_post(true))
            return 101 if outbound != NavAgent::State.Moving or agent.route.status != NavRoute::Status.Found
            return 102 if not route_passes_wall_end(&agent)
            # Fifteen seconds is about three times the walk.
            walk: GuideWalk = try walk_guide(session, guide, &agent, 900)
            return 103 if walk.state != NavAgent::State.Arrived
            return 104 if not walk.clear_of_wall or NavRoute::horizontal_distance(walk.final, guide_post(true)) > 0.5
            walked: f32 = walk.ticks.f32() * GuideTune::SPEED * Runtime::fixed_step_seconds()
            length: f32 = NavRoute::length(&agent.route)
            return 105 if walked < length - 1.0 or walked > length * 1.5 + 1.0
            # The pen is closed: the guide stops at the nearest reachable point.
            pen: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::PEN_X, y: 0.0, z: GuideTune::PEN_Z}
            toward_pen: NavAgent::State = try plan_guide(session, guide, &agent, pen)
            return 106 if toward_pen != NavAgent::State.Moving or agent.route.status != NavRoute::Status.Unreachable
            stranded: GuideWalk = try walk_guide(session, guide, &agent, 1800)
            return 107 if stranded.state != NavAgent::State.Stranded
            return 108 if inside_pen(stranded.final)
            # A goal far above the course has no corridor at all.
            sky: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::POST_A_X, y: 40.0, z: GuideTune::POST_Z}
            toward_sky: NavAgent::State = try plan_guide(session, guide, &agent, sky)
            return 109 if toward_sky != NavAgent::State.Blocked
            snap: NavMesh::Snap = try NavMesh::nearest(guide.mesh,
                Geometry::Vec3{x: GuideTune::POST_A_X, y: 0.8, z: GuideTune::POST_Z}, query_extents())
            return 110 if not snap.found or ElisaMath::fabsf(snap.point.y) > 0.2
            # Unloading retires the handle; a rebake of the staged course
            # publishes a new one and the old one stays stale.
            old: NavMesh::Handle = guide.mesh
            try NavMesh::unload(old)
            return 111 if NavMesh::live(old) or NavMesh::live_count() != 0 or not is_stale(old)
            fresh: NavMesh::Handle = try NavMesh::bake(NavMesh::default_profile())
            return 112 if fresh.generation == old.generation or not NavMesh::live(fresh) or not is_stale(old)
            guide.mesh <- fresh
            home: NavAgent::State = try plan_guide(session, guide, &agent, guide_post(false))
            return 113 if home != NavAgent::State.Moving or agent.route.status != NavRoute::Status.Found
            0

        def probe_progress(handle: RenderScene::InstanceHandle) -> void error[RenderScene::RenderSceneError]:
            _ = try RenderScene::animation_progress(handle)

        def near_progress(value: f32, expected: f32) -> bool:
            ElisaMath::fabsf(value - expected) < 0.01

        # Two instances of one skinned package keep separate clocks.
        def rig_test() -> i32 error[RenderScene::RenderSceneError]:
            rigs: Rigs = catch create_rigs():
                value: value
                error failure: no_rigs()
            return 114 if not rigs.loaded
            try advance_rigs(rigs, 0.2)
            try advance_rigs(rigs, 0.2)
            first: f32 = try RenderScene::animation_progress(rigs.first)
            second: f32 = try RenderScene::animation_progress(rigs.second)
            return 115 if not near_progress(first, 0.4) or not near_progress(second, 0.2)
            # Seeking one leaves the other where it was.
            try RenderScene::play_animation_at(rigs.first, "lift", 0.9, 0.0, 0.0)
            sought: f32 = try RenderScene::animation_progress(rigs.first)
            untouched: f32 = try RenderScene::animation_progress(rigs.second)
            return 116 if not near_progress(sought, 0.9) or untouched != second
            try RenderScene::destroy(rigs.first)
            try RenderScene::advance_animation(rigs.second, 0.2)
            advanced: f32 = try RenderScene::animation_progress(rigs.second)
            return 117 if not near_progress(advanced, 0.3)
            gone: bool = catch probe_progress(rigs.first):
                ok: false
                RenderScene::RenderSceneError.UnknownHandle: true
                error failure: false
            return 118 if not gone
            try RenderScene::destroy(rigs.second)
            0
