# The guide's animated body, included in the CharacterCourse private section
# after `guide.inc`. A skinned rig stands at the guide character's feet and
# turns to face its planar travel. Its looping clip advances by the distance
# walked rather than by frame time: a stranded or arrived guide holds its
# pose, and a slowed guide slows its stride. Elisa decides all of this; the
# engine only plays the clip and places the instance.
        const module WalkTune:
            # Metres walked per clip cycle.
            STRIDE: f32 = 1.25
            # Length of the rig's `lift` clip (make_rigs.py).
            CLIP_SECONDS: f32 = 1.0
            # Planar travel below this keeps the previous heading.
            MIN_TRAVEL: f32 = 0.001
            SCALE: f32 = 0.5

        struct Walker:
            loaded: mutable bool
            rig: mutable RenderScene::InstanceHandle
            # Feet position at the previous update.
            last: mutable Geometry::Vec3
            heading: mutable Geometry::Quat
            # Clip cycles played since creation.
            cycles: mutable f32

        # The yaw that turns +Z toward the planar direction (dx, dz), or
        # `previous` when there is no planar travel.
        def facing(dx: f32, dz: f32, previous: Geometry::Quat) -> Geometry::Quat:
            length: f32 = Geometry::sqrt_f32(dx * dx + dz * dz)
            return previous if length < WalkTune::MIN_TRAVEL
            x: f32 = dx / length
            z: f32 = dz / length
            return Geometry::Quat{x: 0.0, y: 1.0, z: 0.0, w: 0.0} if z < -0.9999
            Geometry::quat_normalize(Geometry::Quat{x: 0.0, y: x, z: 0.0, w: 1.0 + z})

        def feet(pose: PhysicsRuntime::BodyPose) -> Geometry::Vec3:
            Geometry::Vec3{x: pose.position.x,
                y: pose.position.y - Tune::STANDING_HALF_HEIGHT - Tune::CHARACTER_RADIUS,
                z: pose.position.z}

        def walker_transform(position: Geometry::Vec3, heading: Geometry::Quat) -> Geometry::Transform:
            Geometry::Transform{position: position, rotation: heading,
                scale: Geometry::Vec3{x: WalkTune::SCALE, y: WalkTune::SCALE, z: WalkTune::SCALE}}

        def create_walker(pose: PhysicsRuntime::BodyPose) -> Walker error[RenderScene::RenderSceneError]:
            start: Geometry::Vec3 = feet(pose)
            rig: RenderScene::InstanceHandle = try RenderScene::create_mesh("rigs/guide_rig.pkg",
                walker_transform(start, Geometry::quat_identity()), box_color(0.86, 0.5, 0.9))
            try RenderScene::play_animation(rig, "lift", true, 1.0, 0.0)
            Walker{loaded: true, rig: rig, last: start, heading: Geometry::quat_identity(), cycles: 0.0}

        def no_walker() -> Walker:
            Walker{loaded: false, rig: RenderScene::empty_instance_handle(),
                last: Geometry::vec3_zero(), heading: Geometry::quat_identity(), cycles: 0.0}

        # Play keeps going with the plain guide body if the package is missing.
        def optional_walker(session: RuntimeServices::Session&, guide: Guide&) -> Walker:
            pose: PhysicsRuntime::BodyPose = catch RuntimeServices::physics_character_pose(session, guide.character):
                value: value
                error failure: return no_walker()
            catch create_walker(pose):
                value: value
                error failure: no_walker()

        # Follow `pose`; advance the clip only while the agent is walking.
        # Returns the clip cycles this update played.
        def update_walker(walker: mutable Walker&, pose: PhysicsRuntime::BodyPose,
            walking: bool) -> f32 error[RenderScene::RenderSceneError]:
            return 0.0 if not walker.loaded
            now: Geometry::Vec3 = feet(pose)
            dx: f32 = now.x - walker.last.x
            dz: f32 = now.z - walker.last.z
            travelled: f32 = Geometry::sqrt_f32(dx * dx + dz * dz)
            walker.heading <- facing(dx, dz, walker.heading)
            played: f32 = travelled / WalkTune::STRIDE if walking else 0.0
            if played > 0.0:
                try RenderScene::advance_animation(walker.rig, played * WalkTune::CLIP_SECONDS)
            try RenderScene::update_transform(walker.rig, walker_transform(now, walker.heading))
            walker.last <- now
            walker.cycles <- walker.cycles + played
            played

        def release_walker(walker: mutable Walker&) -> void error[RenderScene::RenderSceneError]:
            if walker.loaded:
                try RenderScene::destroy(walker.rig)
                walker.loaded <- false
