# Door, ramp, drop-link and raycast checks for the character course
# self-test, included in the CharacterCourse private section after
# `guide_test.inc`.
        const module LinksTune:
            WALKER_X: f32 = -3.2
            WALKER_Z: f32 = 1.5
            SUMMIT_X: f32 = 5.5
            SUMMIT_Y: f32 = 2.0
            DROP_GOAL_X: f32 = 10.0
            # The walk down the ramp and round the platform is far longer.
            MAX_LINK_ROUTE: f32 = 8.0
            DOOR_TICKS: usize = 15

        def summit() -> Geometry::Vec3:
            Geometry::Vec3{x: LinksTune::SUMMIT_X, y: LinksTune::SUMMIT_Y, z: GuideTune::LINK_Z}

        def drop_goal() -> Geometry::Vec3:
            Geometry::Vec3{x: LinksTune::DROP_GOAL_X, y: 0.0, z: GuideTune::LINK_Z}

        def pen_center() -> Geometry::Vec3:
            Geometry::Vec3{x: GuideTune::PEN_X, y: 0.0, z: GuideTune::PEN_Z}

        def has_link(route: NavRoute::Route&) -> bool:
            for index in 0..<NavRoute::MAX_POINTS |linked: bool = false| -> linked:
                break if index >= route.count
                linked <- true if NavRoute::starts_link(route, index)

        def last_point(route: NavRoute::Route&) -> Geometry::Vec3:
            return Geometry::vec3_zero() if route.count == 0
            route.points[route.count - 1]

        def route_between(guide: Guide&, start: Geometry::Vec3, goal: Geometry::Vec3) -> NavRoute::Route error[NavMesh::NavMeshError]:
            try NavMesh::find_route(guide.mesh, start, goal, query_extents())

        # The guide walks into the pen once its door opens, and is shut in
        # when the door closes behind it on the way out.
        def door_test(session: mutable RuntimeServices::Session&, guide: mutable Guide&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
                try set_door(session, guide, true)
                inward: NavAgent::State = try plan_guide(session, guide, &agent, pen_center())
                return 125 if inward != NavAgent::State.Moving or agent.route.status != NavRoute::Status.Found
                entered: GuideWalk = try walk_guide(session, guide, &agent, 1800)
                return 126 if entered.state != NavAgent::State.Arrived or not inside_pen(entered.final)
                _ = try plan_guide(session, guide, &agent, guide_post(true))
                _ = try walk_guide(session, guide, &agent, LinksTune::DOOR_TICKS)
                try set_door(session, guide, false)
                _ = try plan_guide(session, guide, &agent, guide_post(true))
                return 127 if agent.route.status != NavRoute::Status.Unreachable
                shut_in: GuideWalk = try walk_guide(session, guide, &agent, 600)
                return 128 if shut_in.state != NavAgent::State.Stranded or not inside_pen(shut_in.final)
                return 0

            # A second character climbs the staged ramp to the summit and takes
            # the one-way drop link down to the floor.
        def platform_test(session: mutable RuntimeServices::Session&, guide: Guide&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, NavMesh::NavMeshError]:
            spawn: Geometry::Vec3 = Geometry::Vec3{x: LinksTune::WALKER_X, y: Tune::START_Y, z: LinksTune::WALKER_Z}
            floor_start: Geometry::Vec3 = Geometry::Vec3{x: LinksTune::WALKER_X, y: 0.0, z: LinksTune::WALKER_Z}
            up: NavRoute::Route = try route_between(guide, floor_start, summit())
            return 120 if up.status != NavRoute::Status.Found or last_point(&up).y < 1.5 or has_link(&up)
            character: PhysicsRuntime::CharacterHandle = try create_character_at(session, spawn, Tune::STANDING_HALF_HEIGHT)
            walker: Guide = Guide{character: character, body: guide.body, mesh: guide.mesh,
                outbound: true, door_body: guide.door_body, door_box: guide.door_box,
                door_open: guide.door_open}
            _ = try settle(session)
            agent: mutable NavAgent::Agent = NavAgent::Agent(NavAgent::default_tuning())
            _ = try plan_guide(session, walker, &agent, summit())
            climbed: GuideWalk = try walk_guide(session, walker, &agent, 900)
            return 121 if climbed.state != NavAgent::State.Arrived or climbed.final.y < 1.8
            _ = try plan_guide(session, walker, &agent, drop_goal())
            length: f32 = NavRoute::length(&agent.route)
            return 122 if agent.route.status != NavRoute::Status.Found or not has_link(&agent.route) or length > LinksTune::MAX_LINK_ROUTE
            dropped: GuideWalk = try walk_guide(session, walker, &agent, 900)
            return 123 if dropped.state != NavAgent::State.Arrived or dropped.final.y > 0.5 or NavRoute::horizontal_distance(dropped.final, drop_goal()) > 0.5
            back: NavRoute::Route = try route_between(guide, drop_goal(), summit())
            return 124 if back.status != NavRoute::Status.Found or has_link(&back) or NavRoute::length(&back) < LinksTune::MAX_LINK_ROUTE
            try RuntimeServices::physics_character_destroy(session, character)
            0

        def links_test(session: mutable RuntimeServices::Session&, guide: mutable Guide&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError, NavMesh::NavMeshError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                platform: i32 = try platform_test(session, guide)
                return platform if platform != 0
                door: i32 = try door_test(session, guide)
                return door if door != 0
                # The guide wall stops a ray between the posts; open floor does not.
                blocked: NavMesh::RayHit = try NavMesh::raycast(guide.mesh, guide_post(false), guide_post(true), query_extents())
                return 129 if not blocked.hit or blocked.fraction <= 0.0 or blocked.fraction >= 1.0
                open_start: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::POST_A_X, y: 0.0, z: 5.5}
                open_end: Geometry::Vec3 = Geometry::Vec3{x: GuideTune::POST_B_X, y: 0.0, z: 5.5}
                clear: NavMesh::RayHit = try NavMesh::raycast(guide.mesh, open_start, open_end, query_extents())
                return 130 if clear.hit or clear.fraction != 1.0
                return 0
