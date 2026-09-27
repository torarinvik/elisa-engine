# The guide yard, included in the CharacterCourse private section. A second
# Jolt character patrols between two posts on a Detour route around the guide
# wall, and two skinned rigs animate beside the posts. Every course box,
# the tilted ramp included, is staged for the navmesh as it is created
# (`add_static_box`). The pen has a door: its doorway is a navmesh area that
# is forbidden while the door's body is in place. A one-way link drops from
# the summit platform's east edge to the floor.
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
            DOOR_AREA: u32 = 3
            DOOR_HALF_WIDTH: f32 = 0.6
            LINK_START_X: f32 = 7.5
            LINK_END_X: f32 = 9.0
            LINK_TOP_Y: f32 = 2.0
            LINK_Z: f32 = 2.5
            LINK_RADIUS: f32 = 0.4

        struct Guide:
            character: mutable PhysicsRuntime::CharacterHandle
            body: mutable RenderScene::InstanceHandle
            mesh: mutable NavMesh::Handle
            # True while walking toward post B.
            outbound: mutable bool
            # The pen door's physics body and render box, present while closed.
            door_body: mutable PhysicsRuntime::BodyHandle
            door_box: mutable RenderScene::InstanceHandle
            door_open: mutable bool

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

        def add_obstacle(session: RuntimeServices::Session&, x: f32, z: f32,
            half_x: f32, half_z: f32) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            try add_static_box(session,
                Geometry::Vec3{x: x, y: GuideTune::OBSTACLE_HALF_HEIGHT, z: z},
                Geometry::Vec3{x: half_x, y: GuideTune::OBSTACLE_HALF_HEIGHT, z: half_z},
                Geometry::quat_identity(), box_color(0.55, 0.36, 0.62))

        # The wall the guide walks around, a pen entered only through its
        # door, and the summit's drop link. The walls are shorter than the
        # capsule, so Recast keeps them solid.
        def create_guide_yard(session: RuntimeServices::Session&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            thickness: f32 = GuideTune::OBSTACLE_HALF_THICKNESS
            try add_obstacle(session, GuideTune::WALL_X, GuideTune::WALL_Z,
                thickness, GuideTune::WALL_HALF_DEPTH)
            try add_obstacle(session, GuideTune::PEN_X,
                GuideTune::PEN_Z - GuideTune::PEN_HALF_DEPTH, GuideTune::PEN_HALF_WIDTH, thickness)
            try add_obstacle(session, GuideTune::PEN_X,
                GuideTune::PEN_Z + GuideTune::PEN_HALF_DEPTH, GuideTune::PEN_HALF_WIDTH, thickness)
            # The west wall is two pieces either side of the doorway.
            door_x: f32 = GuideTune::PEN_X - GuideTune::PEN_HALF_WIDTH
            piece: f32 = (GuideTune::PEN_HALF_DEPTH - GuideTune::DOOR_HALF_WIDTH) * 0.5
            try add_obstacle(session, door_x,
                GuideTune::PEN_Z - GuideTune::PEN_HALF_DEPTH + piece, thickness, piece)
            try add_obstacle(session, door_x,
                GuideTune::PEN_Z + GuideTune::PEN_HALF_DEPTH - piece, thickness, piece)
            try NavMesh::mark_area(
                Geometry::Vec3{x: door_x - 0.4, y: -0.5, z: GuideTune::PEN_Z - GuideTune::DOOR_HALF_WIDTH},
                Geometry::Vec3{x: door_x + 0.4, y: 0.5, z: GuideTune::PEN_Z + GuideTune::DOOR_HALF_WIDTH},
                GuideTune::DOOR_AREA)
            try add_obstacle(session, GuideTune::PEN_X + GuideTune::PEN_HALF_WIDTH,
                GuideTune::PEN_Z, thickness, GuideTune::PEN_HALF_DEPTH)

            try NavMesh::add_link(
                Geometry::Vec3{x: GuideTune::LINK_START_X, y: GuideTune::LINK_TOP_Y, z: GuideTune::LINK_Z},
                Geometry::Vec3{x: GuideTune::LINK_END_X, y: 0.0, z: GuideTune::LINK_Z},
                GuideTune::LINK_RADIUS, false)

        def door_center() -> Geometry::Vec3:
            Geometry::Vec3{x: GuideTune::PEN_X - GuideTune::PEN_HALF_WIDTH,
                y: GuideTune::OBSTACLE_HALF_HEIGHT, z: GuideTune::PEN_Z}

        def door_half_extents() -> Geometry::Vec3:
            Geometry::Vec3{x: GuideTune::OBSTACLE_HALF_THICKNESS,
                y: GuideTune::OBSTACLE_HALF_HEIGHT, z: GuideTune::DOOR_HALF_WIDTH}

        # Queries on `mesh` may cross the doorway only while the door is open.
        def apply_door(mesh: NavMesh::Handle, open: bool) -> void error[NavMesh::NavMeshError]:
            try NavMesh::set_area(mesh, GuideTune::DOOR_AREA, open, 1.0)

        # Open removes the door's body and box; close puts them back. Either
        # way the doorway area follows, and agents must plan again.
        def set_door(session: RuntimeServices::Session&, guide: mutable Guide&,
            open: bool) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            if open and not guide.door_open:
                try RuntimeServices::physics_body_destroy(session, guide.door_body)
                try RenderScene::destroy(guide.door_box)
            elif not open and guide.door_open:
                guide.door_body <- try create_static_body(session, door_center(),
                    door_half_extents(), Geometry::quat_identity())
                guide.door_box <- try create_box_render(door_center(), door_half_extents(),
                    Geometry::quat_identity(), box_color(0.85, 0.7, 0.3))
            guide.door_open <- open
            try apply_door(guide.mesh, open)

        # Bake the staged course, close the pen door and put the guide at post A.
        def create_guide(session: RuntimeServices::Session&) -> Guide error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError]:
            mesh: NavMesh::Handle = try NavMesh::bake(NavMesh::default_profile())
            try apply_door(mesh, false)
            door_body: PhysicsRuntime::BodyHandle = try create_static_body(session, door_center(),
                door_half_extents(), Geometry::quat_identity())
            door_box: RenderScene::InstanceHandle = try create_box_render(door_center(),
                door_half_extents(), Geometry::quat_identity(), box_color(0.85, 0.7, 0.3))
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
            Guide{character: character, body: body, mesh: mesh, outbound: true,
                door_body: door_body, door_box: door_box, door_open: false}

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
