# Durable beacon checks for the character-course self-test and the relaunch
# smoke, included in the CharacterCourse private section after `links_test.inc`.
        const module BeaconTune:
            LIVE_ENTITIES: usize = 9
            RENDERS: usize = 6
            CYCLES: usize = 3
            LIT_HEIGHT: f32 = 1.5
            UNLIT_HEIGHT: f32 = 1.4
            HANDOFF_ATTEMPTS: i64 = 4
            POSE_TOLERANCE: f32 = 0.01

        def handoff_checkpoint() -> CourseProgress::Checkpoint:
            CourseProgress::Checkpoint{phase: CourseProgress::PHASE_PLAYING,
                position: Geometry::Vec3{x: -3.0, y: Tune::START_Y, z: 0.5},
                crouched: false, attempts: BeaconTune::HANDOFF_ATTEMPTS}

        def stale_rejected(beacons: CourseBeacons::Beacons&, stale: World::EntityRef) -> bool:
            can Global.Read, Global.Write:
                outcome: i32 = catch CourseBeacons::announce(beacons.world, stale, 0):
                    value: 0
                    WorldEvents::EventError.InvalidEntity: 1
                    error failure: 2
                return outcome == 1

            # A rejected load must leave the same live World and lamps in place.
        def unchanged(beacons: CourseBeacons::Beacons&, lamp: World::EntityRef, lit: usize) -> bool:
            World::world_is_live(beacons.world, lamp) and CourseBeacons::lit_count(beacons) == lit and CourseBeacons::live_entities(beacons) == BeaconTune::LIVE_ENTITIES

        def write_raw(version: i64) -> bool:
            bytes: mutable u8[16] = zeroed
            for index in 0..<16 |bytes|:
                bytes[index] <- 200
            catch UserData::write_payload(CourseBeacons::KEY, version, &bytes[0], 16):
                ok: true
                error failure: false

        def write_snapshot(snapshot: PrefabScene::SceneSnapshot&) -> bool:
            catch PrefabSceneCodec::write_user_data(CourseBeacons::KEY, snapshot):
                ok: true
                error failure: false

        def renders_bounded(baseline: usize) -> bool:
            RenderScene::instance_count() == baseline + BeaconTune::RENDERS

        # Corrupt, wrong-version, unknown-asset, foreign-scene and absent-package saves are
        # all refused without touching the live beacons.
        def rejected_loads_test(beacons: mutable CourseBeacons::Beacons&, baseline: usize) -> i32 error[RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                live_lamp: World::EntityRef = CourseBeacons::lamp_reference(beacons, 0)
                good: PrefabScene::SceneSnapshot = CourseBeacons::capture(beacons)
                return 140 if not write_raw(1)
                corrupt: i32 = try CourseBeacons::load(beacons)
                return 140 if corrupt != CourseBeacons::LOAD_CORRUPT or not unchanged(beacons, live_lamp, 2)
                return 141 if not write_raw(2)
                wrong: i32 = try CourseBeacons::load(beacons)
                return 141 if wrong != CourseBeacons::LOAD_WRONG_VERSION or not unchanged(beacons, live_lamp, 2)
                unknown: mutable PrefabScene::SceneSnapshot = good
                unknown.links[1].overrides.records[2].visual.material.low <- 99
                return 142 if not write_snapshot(&unknown)
                foreign_asset: i32 = try CourseBeacons::load(beacons)
                return 142 if foreign_asset != CourseBeacons::LOAD_UNKNOWN_ASSET or not unchanged(beacons, live_lamp, 2)
                foreign: mutable PrefabScene::SceneSnapshot = good
                foreign.scene_id <- 41
                return 143 if not write_snapshot(&foreign)
                foreign_scene: i32 = try CourseBeacons::load(beacons)
                return 143 if foreign_scene != CourseBeacons::LOAD_REJECTED or not unchanged(beacons, live_lamp, 2) or not renders_bounded(baseline)
                # A shelf-known material whose cooked package is not shipped: the
                # new rows are built before the old ones go, so the load fails
                # whole and no extra rows or streams are left behind.
                unknown.links[1].overrides.records[2].visual.material.low <- CourseBeacons::ABSENT_MATERIAL
                return 189 if not write_snapshot(&unknown)
                absent_package: i32 = try CourseBeacons::load(beacons)
                return 189 if absent_package != CourseBeacons::LOAD_ASSET_FAILED or not unchanged(beacons, live_lamp, 2)
                return 189 if not renders_bounded(baseline) or AssetStream::stream_live() != 0
                return 0

        def beacons_test() -> i32 error[RenderScene::RenderSceneError, Save::SaveError, UserData::UserDataError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                baseline: usize = RenderScene::instance_count()
                beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                    value: value
                    error failure: return 131
                started: i32 = catch CourseBeacons::start(&beacons):
                    value: value
                    error failure: -1
                return 131 if started != 0
                return 131 if not beacons.ready or CourseBeacons::lit_count(beacons) != 0 or CourseBeacons::live_entities(beacons) != BeaconTune::LIVE_ENTITIES or not renders_bounded(baseline)
                _ = catch UserData::remove_payload(CourseBeacons::KEY):
                    ok: true
                    error failure: false
                missing: i32 = try CourseBeacons::load(&beacons)
                return 132 if missing != CourseBeacons::LOAD_MISSING or not beacons.ready
                first: usize = try CourseBeacons::update(&beacons, CourseBeacons::anchor(0))
                last: usize = try CourseBeacons::update(&beacons, CourseBeacons::anchor(2))
                again: usize = try CourseBeacons::update(&beacons, CourseBeacons::anchor(2))
                return 133 if first != 1 or last != 1 or again != 0 or CourseBeacons::lit_count(beacons) != 2 or beacons.announced != 2
                return 134 if CourseBeacons::lamp_height(beacons, 0) < BeaconTune::LIT_HEIGHT or CourseBeacons::lamp_height(beacons, 1) > BeaconTune::UNLIT_HEIGHT
                saved_lamp: World::EntityRef = CourseBeacons::lamp_reference(beacons, 0)
                return 135 if not CourseBeacons::save(beacons)
                _ = try CourseBeacons::update(&beacons, CourseBeacons::anchor(1))
                return 136 if CourseBeacons::lit_count(beacons) != 3
                loaded: i32 = try CourseBeacons::load(&beacons)
                return 137 if loaded != 0 or CourseBeacons::lit_count(beacons) != 2 or CourseBeacons::lit(beacons, 1) or CourseBeacons::lamp_height(beacons, 0) < BeaconTune::LIT_HEIGHT or CourseBeacons::lamp_height(beacons, 1) > BeaconTune::UNLIT_HEIGHT
                return 138 if World::world_is_live(beacons.world, saved_lamp) or not stale_rejected(beacons, saved_lamp)
                return 139 if CourseBeacons::live_entities(beacons) != BeaconTune::LIVE_ENTITIES or not renders_bounded(baseline)
                rejected: i32 = try rejected_loads_test(&beacons, baseline)
                return rejected if rejected != 0
                for _ in 0..<BeaconTune::CYCLES |beacons|:
                    return 144 if not CourseBeacons::save(beacons)
                    cycled: i32 = try CourseBeacons::load(&beacons)
                    return 144 if cycled != 0 or CourseBeacons::lit_count(beacons) != 2 or CourseBeacons::live_entities(beacons) != BeaconTune::LIVE_ENTITIES or not renders_bounded(baseline)
                reset: i32 = try CourseBeacons::reset(&beacons)
                return 145 if reset != 0 or CourseBeacons::lit_count(beacons) != 0 or not renders_bounded(baseline)
                # Hand the relaunch smoke beacons 0 and 2 lit plus a checkpoint.
                _ = try CourseBeacons::update(&beacons, CourseBeacons::anchor(0))
                _ = try CourseBeacons::update(&beacons, CourseBeacons::anchor(2))
                return 146 if not CourseBeacons::save(beacons)
                checkpoint: CourseProgress::Checkpoint = handoff_checkpoint()
                try CourseProgress::save(&checkpoint)
                try CourseBeacons::release(&beacons)
                return 147 if RenderScene::instance_count() != baseline
                return 0

        def pose_matches(pose: PhysicsRuntime::BodyPose, position: Geometry::Vec3) -> bool:
            dx: f32 = ElisaMath::fabsf(pose.position.x - position.x)
            dy: f32 = ElisaMath::fabsf(pose.position.y - position.y)
            dz: f32 = ElisaMath::fabsf(pose.position.z - position.z)
            dx <= BeaconTune::POSE_TOLERANCE and dy <= BeaconTune::POSE_TOLERANCE and dz <= BeaconTune::POSE_TOLERANCE

        def handoff_beacons_restored(beacons: CourseBeacons::Beacons&) -> bool:
            lit_ends: bool = CourseBeacons::lit(beacons, 0) and CourseBeacons::lit(beacons, 2)
            lit_ends and not CourseBeacons::lit(beacons, 1) and CourseBeacons::lamp_height(beacons, 2) >= BeaconTune::LIT_HEIGHT

        # Second process: the saves written by `beacons_test` come back, a
        # restart clears the run, and the saves are removed.
        def relaunch_test(session: mutable RuntimeServices::Session&,
            character: PhysicsRuntime::CharacterHandle) -> ProgressOutcome error[RuntimeServices::RuntimeServicesError, PhysicsRuntime::PhysicsError, RenderScene::RenderSceneError] can[Global.Read, Global.Write]:
            can Global.Read, Global.Write:
                expected: CourseProgress::Checkpoint = handoff_checkpoint()
                loaded: CourseProgress::LoadResult = CourseProgress::load()
                return ProgressOutcome{status: 148, character: character} if loaded.status != CourseProgress::LoadStatus.Loaded or loaded.checkpoint.attempts != expected.attempts or loaded.checkpoint.phase != expected.phase or loaded.checkpoint.crouched
                restored: PhysicsRuntime::CharacterHandle = try apply_checkpoint(session, character, loaded.checkpoint)
                pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, restored)
                return ProgressOutcome{status: 149, character: restored} if not pose_matches(pose, expected.position)
                baseline: usize = RenderScene::instance_count()
                beacons: mutable CourseBeacons::Beacons = catch CourseBeacons::create():
                    value: value
                    error failure: return ProgressOutcome{status: 150, character: restored}
                started: i32 = catch CourseBeacons::start(&beacons):
                    value: value
                    error failure: -1
                return ProgressOutcome{status: 150, character: restored} if started != 0
                beacons_loaded: i32 = try CourseBeacons::load(&beacons)
                return ProgressOutcome{status: 150, character: restored} if beacons_loaded != 0 or not handoff_beacons_restored(beacons)
                reset: i32 = try CourseBeacons::reset(&beacons)
                restarted: PhysicsRuntime::CharacterHandle = try restart_character(session, restored)
                start_pose: PhysicsRuntime::BodyPose = try RuntimeServices::physics_character_pose(session, restarted)
                start: Geometry::Vec3 = Geometry::Vec3{x: Tune::START_X, y: Tune::START_Y, z: Tune::START_Z}
                return ProgressOutcome{status: 151, character: restarted} if reset != 0 or CourseBeacons::lit_count(beacons) != 0 or not pose_matches(start_pose, start)
                try CourseBeacons::release(&beacons)
                return ProgressOutcome{status: 152, character: restarted} if RenderScene::instance_count() != baseline
                _ = catch UserData::remove_payload(CourseBeacons::KEY):
                    ok: true
                    error failure: false
                _ = catch UserData::remove_blob(CourseProgress::key()):
                    ok: true
                    error failure: false
                return ProgressOutcome{status: 0, character: restarted}
