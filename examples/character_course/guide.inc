# The guide yard, included in the CharacterCourse private section. A second
# Jolt character patrols between two posts on a Detour route around the guide
# wall, and two skinned rigs animate beside the posts. Every axis-aligned
# course box is staged for the navmesh as it is created (`add_static_box`);
# the tilted ramp is not, so no route may pass under or over it.
        const module GuideTune:
            OBSTACLE_HALF_HEIGHT: f32 = 0.6
            OBSTACLE_HALF_THICKNESS: f32 = 0.25
            WALL_X: f32 = -10.0
            WALL_Z: f32 = -5.25
            WALL_HALF_DEPTH: f32 = 1.75
            PEN_X: f32 = 11.5
            PEN_Z: f32 = -5.25
            PEN_HALF_WIDTH: f32 = 1.5
            PEN_HALF_DEPTH: f32 = 1.25
            POST_A_X: f32 = -13.0
            POST_B_X: f32 = -7.0
            POST_Z: f32 = -5.5
            SPEED: f32 = 2.5
            QUERY_EXTENT: f32 = 1.0
            RIG_Z: f32 = -6.5
            RIG_SCALE: f32 = 0.5
            RIG_A_SPEED: f32 = 1.0
            RIG_B_SPEED: f32 = 0.5
            MAX_ANIMATION_STEP: f32 = 0.25

        struct Guide:
            character: mutable PhysicsRuntime::CharacterHandle
            body: mutable RenderScene::InstanceHandle
            mesh: mutable NavMesh::Handle
            # True while walking toward post B.
            outbound: mutable bool

        # Two instances of one skinned package with independent playback.
        struct Rigs:
            loaded: mutable bool
            first: mutable RenderScene::InstanceHandle
            second: mutable RenderScene::InstanceHandle

        def guide_post(outbound: bool) -> Geometry::Vec3:
            Geometry::Vec3{x: GuideTune::POST_B_X if outbound else GuideTune::POST_A_X,
                y: 0.0, z: GuideTune::POST_Z}

        def query_extents() -> Geometry::Vec3:
            Geometry::Vec3{x: GuideTune::QUERY_EXTENT, y: GuideTune::QUERY_EXTENT,
                z: GuideTune::QUERY_EXTENT}

        def is_axis_aligned(rotation: Geometry::Quat) -> bool:
            rotation.x == 0.0 and rotation.y == 0.0 and rotation.z == 0.0

        def add_obstacle(session: RuntimeServices::Session&, x: f32, z: f32,
            half_x: f32, half_z: f32) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            try add_static_box(session,
                Geometry::Vec3{x: x, y: GuideTune::OBSTACLE_HALF_HEIGHT, z: z},
                Geometry::Vec3{x: half_x, y: GuideTune::OBSTACLE_HALF_HEIGHT, z: half_z},
                Geometry::quat_identity(), box_color(0.55, 0.36, 0.62))

        # The wall the guide walks around, and a closed pen nothing can enter.
        # Both are shorter than the capsule, so Recast keeps them solid.
        def create_guide_yard(session: RuntimeServices::Session&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            thickness: f32 = GuideTune::OBSTACLE_HALF_THICKNESS
            try add_obstacle(session, GuideTune::WALL_X, GuideTune::WALL_Z,
                thickness, GuideTune::WALL_HALF_DEPTH)
            try add_obstacle(session, GuideTune::PEN_X,
                GuideTune::PEN_Z - GuideTune::PEN_HALF_DEPTH, GuideTune::PEN_HALF_WIDTH, thickness)
            try add_obstacle(session, GuideTune::PEN_X,
                GuideTune::PEN_Z + GuideTune::PEN_HALF_DEPTH, GuideTune::PEN_HALF_WIDTH, thickness)
            try add_obstacle(session, GuideTune::PEN_X - GuideTune::PEN_HALF_WIDTH,
                GuideTune::PEN_Z, thickness, GuideTune::PEN_HALF_DEPTH)
            try add_obstacle(session, GuideTune::PEN_X + GuideTune::PEN_HALF_WIDTH,
                GuideTune::PEN_Z, thickness, GuideTune::PEN_HALF_DEPTH)

        # Bake the staged course and put the guide at post A.
        def create_guide(session: RuntimeServices::Session&) -> Guide error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            mesh: NavMesh::Handle = try NavMesh::bake(NavMesh::default_profile())
            start: Geometry::Vec3 = guide_post(false)
            character: PhysicsRuntime::CharacterHandle = try create_character_at(session,
                Geometry::Vec3{x: start.x, y: Tune::START_Y, z: start.z}, Tune::STANDING_HALF_HEIGHT)
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, character)
            body: RenderScene::InstanceHandle = try RenderScene::create(RenderScene::Primitive.Sphere,
                Geometry::Transform{position: pose.position, rotation: Geometry::quat_identity(),
                    scale: Geometry::Vec3{x: Tune::CHARACTER_RADIUS * 2.0,
                        y: (Tune::STANDING_HALF_HEIGHT + Tune::CHARACTER_RADIUS) * 2.0,
                        z: Tune::CHARACTER_RADIUS * 2.0}},
                box_color(0.86, 0.5, 0.9))
            Guide{character: character, body: body, mesh: mesh, outbound: true}

        # Ask for a route from the guide's position to `goal`.
        def plan_guide(session: RuntimeServices::Session&, guide: Guide&,
            agent: mutable NavAgent::Agent&, goal: Geometry::Vec3) -> NavAgent::State error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, NavMesh::NavMeshError]:
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
            route: NavRoute::Route = try NavMesh::find_route(guide.mesh, pose.position, goal, query_extents())
            NavAgent::assign(agent, route)

        # One fixed step: steer from the current pose and walk that way.
        def step_guide(session: RuntimeServices::Session&, guide: Guide&,
            agent: mutable NavAgent::Agent&) -> NavAgent::State error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
            steering: NavAgent::Steering = NavAgent::steer(agent, pose.position)
            try RuntimeServices::physics_character_move(session, guide.character,
                steering.direction, GuideTune::SPEED, 0.0, false)
            steering.state

        # Patrol: turn around on arrival and replan from wherever the guide
        # stopped when it is stuck, stranded or pushed off its route.
        def patrol_guide(session: RuntimeServices::Session&, guide: mutable Guide&,
            agent: mutable NavAgent::Agent&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, NavMesh::NavMeshError]:
            state: NavAgent::State = try step_guide(session, guide, agent)
            if state != NavAgent::State.Moving:
                guide.outbound <- not guide.outbound if state == NavAgent::State.Arrived
                _ = try plan_guide(session, guide, agent, guide_post(guide.outbound))

        def show_guide(session: RuntimeServices::Session&, guide: Guide&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError]:
            pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, guide.character)
            try update_avatar(guide.body, pose, false)

        def create_rigs() -> Rigs error[RenderScene::RenderSceneError]:
            scale: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::RIG_SCALE,
                y: GuideTune::RIG_SCALE, z: GuideTune::RIG_SCALE}
            white: RenderScene::Color = box_color(1.0, 1.0, 1.0)
            first: RenderScene::InstanceHandle = try RenderScene::create_mesh("rigs/guide_rig.pkg",
                Geometry::Transform{position: Geometry::Vec3{x: GuideTune::POST_A_X, y: 0.0, z: GuideTune::RIG_Z},
                    rotation: Geometry::quat_identity(), scale: scale}, white)
            second: RenderScene::InstanceHandle = try RenderScene::create_mesh("rigs/guide_rig.pkg",
                Geometry::Transform{position: Geometry::Vec3{x: GuideTune::POST_B_X, y: 0.0, z: GuideTune::RIG_Z},
                    rotation: Geometry::quat_identity(), scale: scale}, white)
            try RenderScene::play_animation(first, "lift", true, GuideTune::RIG_A_SPEED, 0.0)
            try RenderScene::play_animation(second, "lift", true, GuideTune::RIG_B_SPEED, 0.0)
            Rigs{loaded: true, first: first, second: second}

        def no_rigs() -> Rigs:
            Rigs{loaded: false, first: RenderScene::empty_instance_handle(),
                second: RenderScene::empty_instance_handle()}

        # Play keeps going without the rigs if their package is missing.
        def optional_rigs() -> Rigs:
            catch create_rigs():
                value: value
                error failure: no_rigs()

        def advance_rigs(rigs: Rigs&, seconds: f32) -> void error[RenderScene::RenderSceneError]:
            if rigs.loaded:
                step: f32 = GuideTune::MAX_ANIMATION_STEP if seconds > GuideTune::MAX_ANIMATION_STEP else seconds
                try RenderScene::advance_animation(rigs.first, step)
                try RenderScene::advance_animation(rigs.second, step)
