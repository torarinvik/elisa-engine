        private:
            def ray_query_targets(filter: PhysicsQueryFilter) -> u32:
                targets: mutable u32 = 0
                targets <- targets | Status::QUERY_SCENE_OBJECTS if filter.scene_objects
                targets <- targets | Status::QUERY_SCENE_COLLIDERS if filter.scene_colliders
                targets <- targets | Status::QUERY_PHYSICS_BODIES if filter.physics_bodies
                targets

            def raycast_generation(generation: u64, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32,
                layer_mask: u32) -> RayHit error[PhysicsError]:
                entity: mutable u64 = 0
                position_x: mutable f32 = 0.0
                position_y: mutable f32 = 0.0
                position_z: mutable f32 = 0.0
                normal_x: mutable f32 = 0.0
                normal_y: mutable f32 = 0.0
                normal_z: mutable f32 = 0.0
                distance: mutable f32 = 0.0
                hit: mutable i32 = 0
                try check_status(elisa_physics_v1_raycast(generation,
                    origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
                    max_distance, layer_mask, &entity, &position_x, &position_y,
                    &position_z, &normal_x, &normal_y, &normal_z, &distance, &hit))
                RayHit{
                    hit: hit != 0,
                    entity: entity,
                    position: Geometry::Vec3{x: position_x, y: position_y, z: position_z},
                    normal: Geometry::Vec3{x: normal_x, y: normal_y, z: normal_z},
                    distance: distance,
                }

            def copy_ray_hits(native: NativeRayHitBuffer&,
                buffer: mutable RayHitBuffer&) -> usize error[PhysicsError]:
                count: u32 = native.count
                raise PhysicsError.Capacity if count > MAX_RAY_HITS.u32()
                for index in 0..<count.usize() |buffer, native, index|:
                    hit: NativeRayHit = native.hits[index]
                    buffer.hits[index] <- RayHit{
                        hit: true,
                        entity: hit.entity,
                        position: Geometry::Vec3{x: hit.position_x,
                            y: hit.position_y, z: hit.position_z},
                        normal: Geometry::Vec3{x: hit.normal_x,
                            y: hit.normal_y, z: hit.normal_z},
                        distance: hit.distance,
                    }
                buffer.count <- count.usize()
                buffer.count

        public:
            def service_raycast(state: ServiceState&, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32,
                layer_mask: u32) -> RayHit error[PhysicsError]:
                raise PhysicsError.ApplicationUnavailable if not state.live
                raycast_generation(state.generation, origin, direction,
                    max_distance, layer_mask)

            def service_raycast_all(state: ServiceState&, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32, layer_mask: u32,
                buffer: mutable RayHitBuffer&) -> usize error[PhysicsError]:
                try service_raycast_all_filtered(state, origin, direction,
                    max_distance, layer_mask, PhysicsQueryFilter(), buffer)

            def service_raycast_all_filtered(state: ServiceState&,
                origin: Geometry::Vec3, direction: Geometry::Vec3,
                max_distance: f32, layer_mask: u32, filter: PhysicsQueryFilter,
                buffer: mutable RayHitBuffer&) -> usize error[PhysicsError]:
                raise PhysicsError.ApplicationUnavailable if not state.live
                native: mutable NativeRayHitBuffer = zeroed
                try check_status(elisa_physics_v1_raycast_all_filtered(state.generation,
                    origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
                    max_distance, layer_mask, ray_query_targets(filter), &native))
                try copy_ray_hits(native, buffer)

            def raycast(world: World&, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32,
                layer_mask: u32) -> RayHit error[PhysicsError]:
                raise PhysicsError.ApplicationUnavailable if not world.live
                raycast_generation(world.generation, origin, direction,
                    max_distance, layer_mask)

            def raycast_all(world: World&, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32, layer_mask: u32,
                buffer: mutable RayHitBuffer&) -> usize error[PhysicsError]:
                try raycast_all_filtered(world, origin, direction, max_distance,
                    layer_mask, PhysicsQueryFilter(), buffer)

            def raycast_all_filtered(world: World&, origin: Geometry::Vec3,
                direction: Geometry::Vec3, max_distance: f32, layer_mask: u32,
                filter: PhysicsQueryFilter, buffer: mutable RayHitBuffer&) -> usize error[PhysicsError]:
                raise PhysicsError.ApplicationUnavailable if not world.live
                native: mutable NativeRayHitBuffer = zeroed
                try check_status(elisa_physics_v1_raycast_all_filtered(world.generation,
                    origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
                    max_distance, layer_mask, ray_query_targets(filter), &native))
                try copy_ray_hits(native, buffer)
