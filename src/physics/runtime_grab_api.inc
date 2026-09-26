    private:
        def grab_kind_code(kind: GrabKind) -> i32:
            match kind:
                GrabKind.Fixed:
                    Status::GRAB_FIXED
                GrabKind.Point:
                    Status::GRAB_POINT

        def native_grab_begin(world_generation: u64, ray: Geometry::Ray3,
            kind: GrabKind, break_distance: f32) -> GrabHandle error[PhysicsError]:
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(ray.origin) or not Geometry::vec3_is_finite(ray.direction) or Geometry::vec3_length(ray.direction) <= 0.000001 or not finite(break_distance) or break_distance < 0.0
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_grab_begin(world_generation,
                ray.origin.x, ray.origin.y, ray.origin.z,
                ray.direction.x, ray.direction.y, ray.direction.z,
                grab_kind_code(kind), break_distance, &slot, &generation)
            try check_status(status)
            GrabHandle{world_generation: world_generation, slot: slot,
                generation: generation}

    public:
        def service_grab_begin(state: ServiceState&, ray: Geometry::Ray3,
            kind: GrabKind, break_distance: f32) -> GrabHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_grab_begin(state.generation, ray, kind, break_distance)

        def grab_begin(world: World&, ray: Geometry::Ray3,
            kind: GrabKind, break_distance: f32) -> GrabHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_grab_begin(world.generation, ray, kind, break_distance)

        def service_grab_update(state: ServiceState&, grab: GrabHandle,
            ray: Geometry::Ray3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if grab.world_generation != state.generation
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(ray.origin) or not Geometry::vec3_is_finite(ray.direction) or Geometry::vec3_length(ray.direction) <= 0.000001
            try check_status(elisa_physics_v1_grab_update(state.generation,
                grab.slot, grab.generation, ray.origin.x, ray.origin.y, ray.origin.z,
                ray.direction.x, ray.direction.y, ray.direction.z))

        def grab_update(world: World&, grab: GrabHandle,
            ray: Geometry::Ray3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if grab.world_generation != world.generation
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(ray.origin) or not Geometry::vec3_is_finite(ray.direction) or Geometry::vec3_length(ray.direction) <= 0.000001
            try check_status(elisa_physics_v1_grab_update(world.generation,
                grab.slot, grab.generation, ray.origin.x, ray.origin.y, ray.origin.z,
                ray.direction.x, ray.direction.y, ray.direction.z))

        def service_grab_end(state: ServiceState&, grab: GrabHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if grab.world_generation != state.generation
            try check_status(elisa_physics_v1_grab_end(state.generation,
                grab.slot, grab.generation))

        def grab_end(world: World&, grab: GrabHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if grab.world_generation != world.generation
            try check_status(elisa_physics_v1_grab_end(world.generation,
                grab.slot, grab.generation))
