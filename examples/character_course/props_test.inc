# Whole-World save checks for the course props, included in the
# CharacterCourse private section after `beacons_test.inc`. Codes 218-226 run
# in the self-test; 227-229 run in the relaunch process.
        const module PropsTune:
            SETTLE_STEPS: usize = 90
            CYCLES: usize = 3
            TOLERANCE: f32 = 0.01
            DROP: f32 = 0.2

        def props_handoff(index: usize) -> Geometry::Vec3:
            Geometry::Vec3{x: -9.0 + index.f32(), y: 0.3, z: 5.5}

        def props_near(a: Geometry::Vec3, b: Geometry::Vec3) -> bool:
            dx: f32 = ElisaMath::fabsf(a.x - b.x)
            dy: f32 = ElisaMath::fabsf(a.y - b.y)
            dz: f32 = ElisaMath::fabsf(a.z - b.z)
            dx <= PropsTune::TOLERANCE and dy <= PropsTune::TOLERANCE and dz <= PropsTune::TOLERANCE

        def props_body_at(session: RuntimeServices::Session&, props: CourseProps::Props&, index: usize, expected: Geometry::Vec3) -> bool:
            pose: PhysicsRuntime::BodyPose = catch WorldPhysics::body_pose(session, props.physics, CourseProps::crate_reference(props, index)):
                value: value
                error failure: return false
            props_near(pose.position, expected)

        def props_counts(props: CourseProps::Props&, baseline: usize) -> bool:
            live: bool = World::world_live_count(props.world) == CourseProps::COUNT
            bound: bool = WorldRendering::binding_count(props.render) == CourseProps::COUNT and WorldPhysics::binding_count(props.physics) == CourseProps::COUNT
            live and bound and RenderScene::instance_count() == baseline + CourseProps::COUNT

        # Every crate sits at `expected[i]` in the World, in its render row
        # (with render ID base + i) and in its body.
        def props_restored(session: RuntimeServices::Session&, props: mutable CourseProps::Props&, expected: Geometry::Vec3[CourseProps::COUNT]) -> bool:
            view: mutable RenderSnapshot::Snapshot = RenderSnapshot::Snapshot()
            extracted: bool = catch WorldRendering::extract(&props.world, &props.render, &view):
                ok: true
                error failure: false
            return false if not extracted or RenderSnapshot::snapshot_count(&view) != CourseProps::COUNT
            for index in 0..<CourseProps::COUNT |all: bool = true, props, view| -> all:
                entry: RenderSnapshot::Entry = RenderSnapshot::snapshot_entry(&view, index)
                row_ok: bool = entry.render_id == CourseProps::render_base() + index.i64() and props_near(entry.transform.position, expected[index])
                world_ok: bool = props_near(CourseProps::crate_pose(props, index).position, expected[index])
                all <- false if not row_ok or not world_ok or not props_body_at(session, props, index, expected[index])

        def props_rewrite(bytes: u8&, length: usize) -> bool:
            catch UserData::write_payload(CourseProps::KEY, 1, bytes, length):
                ok: true
                error failure: false

        def props_read_raw(bytes: mutable u8&, length: mutable usize&) -> bool:
            version: mutable i64 = 0
            catch UserData::read_payload(CourseProps::KEY, &version, bytes, CourseProps::CAPACITY, length):
                ok: true
                error failure: false

        def props_settle(session: mutable RuntimeServices::Session&, props: mutable CourseProps::Props&) -> void error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError]:
            for _ in 0..<PropsTune::SETTLE_STEPS |session, props|:
                _ = try RuntimeServices::physics_fixed_step(session, Runtime::fixed_step_seconds())
                try CourseProps::sync(session, props)

        # Corrupt and garbage payloads in a valid UserData envelope reach
        # WorldSaveLoad and are rejected with the live props untouched.
        def props_rejected_test(session: RuntimeServices::Session&, props: mutable CourseProps::Props&, baseline: usize) -> i32 error[RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            bytes: mutable u8[CourseProps::CAPACITY] = zeroed
            length: mutable usize = 0
            return 221 if not props_read_raw(&bytes[0], &length) or length == 0
            live_crate: World::EntityRef = CourseProps::crate_reference(props, 0)
            displaced: Geometry::Vec3 = CourseProps::crate_pose(props, 0).position
            bytes[30] <- bytes[30] ^ 1u8
            return 221 if not props_rewrite(&bytes[0], length)
            corrupt: i32 = try CourseProps::load(session, props)
            intact: bool = World::world_is_live(props.world, live_crate) and props_near(CourseProps::crate_pose(props, 0).position, displaced)
            return 221 if corrupt != CourseProps::LOAD_REJECTED or not intact or not props_counts(props, baseline)
            garbage: mutable u8[16] = zeroed
            for index in 0..<16 |garbage|:
                garbage[index] <- 200
            return 222 if not props_rewrite(&garbage[0], 16)
            rejected: i32 = try CourseProps::load(session, props)
            return 222 if rejected != CourseProps::LOAD_REJECTED or not World::world_is_live(props.world, live_crate) or not props_counts(props, baseline)
            bytes[30] <- bytes[30] ^ 1u8
            return 223 if not props_rewrite(&bytes[0], length)
            0

        def props_test(session: mutable RuntimeServices::Session&) -> i32 error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            baseline: usize = RenderScene::instance_count()
            _ = catch UserData::remove_payload(CourseProps::KEY):
                ok: true
                error failure: false
            props: mutable CourseProps::Props = catch CourseProps::create(session):
                value: value
                error failure: return 218
            return 218 if not props_counts(props, baseline)
            spawn_y: f32 = CourseProps::crate_pose(props, 0).position.y
            missing: i32 = try CourseProps::load(session, &props)
            return 219 if missing != CourseProps::LOAD_MISSING or not props_counts(props, baseline)
            try props_settle(session, &props)
            return 220 if CourseProps::crate_pose(props, 0).position.y > spawn_y - PropsTune::DROP
            saved: mutable Geometry::Vec3[CourseProps::COUNT] = zeroed
            for index in 0..<CourseProps::COUNT |saved, props|:
                saved[index] <- CourseProps::crate_pose(props, index).position
            return 220 if not CourseProps::save(props)
            # Knock crate 0 away in the World only; a load must bring it back.
            old_crate: World::EntityRef = CourseProps::crate_reference(props, 0)
            moved: bool = catch World::world_set_transform(&props.world, old_crate, Geometry::transform_translate(Geometry::transform_identity(), props_handoff(0))):
                ok: true
                error failure: false
            return 220 if not moved
            rejected: i32 = try props_rejected_test(session, &props, baseline)
            return rejected if rejected != 0
            loaded: i32 = try CourseProps::load(session, &props)
            return 224 if loaded != 0 or World::world_is_live(props.world, old_crate) or not props_counts(props, baseline)
            return 224 if not props_restored(session, &props, saved)
            for _ in 0..<PropsTune::CYCLES |session, props|:
                return 225 if not CourseProps::save(props)
                cycled: i32 = try CourseProps::load(session, &props)
                return 225 if cycled != 0 or not props_counts(props, baseline)
            # Hand the relaunch a World with the crates at known poses.
            for index in 0..<CourseProps::COUNT |props|:
                _ = catch World::world_set_transform(&props.world, CourseProps::crate_reference(props, index), Geometry::transform_translate(Geometry::transform_identity(), props_handoff(index))):
                    ok: true
                    error failure: false
            return 226 if not CourseProps::save(props)
            try CourseProps::release(session, &props)
            return 226 if RenderScene::instance_count() != baseline or WorldPhysics::binding_count(props.physics) != 0
            0

        # Second process: the World saved by `props_test` comes back with
        # rows and bodies at the handoff poses, then the save is removed.
        def props_relaunch_test(session: mutable RuntimeServices::Session&) -> i32 error[RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            baseline: usize = RenderScene::instance_count()
            props: mutable CourseProps::Props = catch CourseProps::create(session):
                value: value
                error failure: return 227
            loaded: i32 = try CourseProps::load(session, &props)
            return 227 if loaded != 0 or not props_counts(props, baseline)
            expected: mutable Geometry::Vec3[CourseProps::COUNT] = zeroed
            for index in 0..<CourseProps::COUNT |expected|:
                expected[index] <- props_handoff(index)
            return 228 if not props_restored(session, &props, expected)
            try CourseProps::release(session, &props)
            return 229 if RenderScene::instance_count() != baseline
            _ = catch UserData::remove_payload(CourseProps::KEY):
                ok: true
                error failure: false
            0
