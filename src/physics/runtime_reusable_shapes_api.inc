        struct ShapeHandle:
            private:
                world_generation: u64
                slot: u32
                generation: u64

        const MAX_COMPOUND_CHILDREN: usize = 16

        struct CompoundChild:
            shape: ShapeHandle
            position: Geometry::Vec3
            rotation: Geometry::Quat

        struct BodyInstanceDesc:
            kind: BodyKind
            position: Geometry::Vec3
            mass: f32
            sensor: bool

        private def shape_create_compound_for_generation(world_generation: u64,
            children: CompoundChild&, child_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.InvalidArgument if child_count == 0 or child_count > MAX_COMPOUND_CHILDREN
            created: ShapeHandle =
                slots: mutable u32[MAX_COMPOUND_CHILDREN] = zeroed
                generations: mutable u64[MAX_COMPOUND_CHILDREN] = zeroed
                positions: mutable Geometry::Vec3[MAX_COMPOUND_CHILDREN] = zeroed
                rotations: mutable Geometry::Quat[MAX_COMPOUND_CHILDREN] = zeroed
                for index in 0..<child_count:
                    child: CompoundChild = children[index]
                    raise PhysicsError.StaleWorld if child.shape.world_generation != world_generation
                    raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(child.position) or not Geometry::quat_is_finite(child.rotation) or Geometry::quat_length(child.rotation) <= 0.0
                    slots[index] <- child.shape.slot
                    generations[index] <- child.shape.generation
                    positions[index] <- child.position
                    rotations[index] <- Geometry::quat_normalize(child.rotation)
                slot: mutable u32 = 0
                generation: mutable u64 = 0
                status: i32 = elisa_physics_v1_create_compound_shape(world_generation,
                    &slots[0], &generations[0], &positions[0], &rotations[0], child_count.u32(),
                    &slot, &generation)
                try check_status(status)
                ShapeHandle{world_generation: world_generation, slot: slot, generation: generation}
            created

        def shape_create(world: World&, shape: BodyShape,
            dimensions: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_shape(world.generation,
                shape_code(shape), dimensions.x, dimensions.y, dimensions.z,
                &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: world.generation, slot: slot, generation: generation}

        def shape_create_compound(world: World&, children: CompoundChild&,
            child_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            shape_create_compound_for_generation(world.generation, children, child_count)

        # Array inputs are copied and cooked before this function returns.
        def shape_create_triangle_mesh(world: World&, vertices: Geometry::Vec3&,
            vertex_count: usize, indices: u32&, index_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.InvalidArgument if vertex_count == 0 or index_count == 0 or vertex_count > Status::MAX_MESH_VERTICES or index_count > Status::MAX_MESH_INDICES
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_mesh_shape(world.generation,
                Status::MESH_SHAPE_TRIANGLE_MESH, vertices, vertex_count.u32(), indices, index_count.u32(), &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: world.generation, slot: slot, generation: generation}

        def shape_create_convex_hull(world: World&, vertices: Geometry::Vec3&,
            vertex_count: usize, indices: u32&, index_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.InvalidArgument if vertex_count == 0 or index_count == 0 or vertex_count > Status::MAX_MESH_VERTICES or index_count > Status::MAX_MESH_INDICES
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_mesh_shape(world.generation,
                Status::MESH_SHAPE_CONVEX_HULL, vertices, vertex_count.u32(), indices, index_count.u32(), &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: world.generation, slot: slot, generation: generation}

        def mesh_shape_create(world: World&, shape: MeshShape, asset_path: cstr,
            scale: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            kind: i32 = match shape:
                MeshShape.ConvexHull: Status::MESH_SHAPE_CONVEX_HULL
                MeshShape.TriangleMesh: Status::MESH_SHAPE_TRIANGLE_MESH
            status: i32 = elisa_physics_v1_create_mesh_shape_from_asset(world.generation,
                kind, asset_path, scale.x, scale.y, scale.z, &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: world.generation, slot: slot, generation: generation}

        def shape_destroy(world: World&, shape: ShapeHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if shape.world_generation != world.generation
            try check_status(elisa_physics_v1_destroy_shape(world.generation,
                shape.slot, shape.generation))

        def body_create_with_shape(world: World&, description: BodyInstanceDesc,
            shape: ShapeHandle) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if shape.world_generation != world.generation
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body_with_shape(world.generation,
                kind_code(description.kind), description.position.x, description.position.y,
                description.position.z, description.mass, 1 if description.sensor else 0,
                shape.slot, shape.generation, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: world.generation, slot: slot, generation: generation}

        def service_shape_create(state: ServiceState&, shape: BodyShape,
            dimensions: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_shape(state.generation,
                shape_code(shape), dimensions.x, dimensions.y, dimensions.z,
                &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: state.generation, slot: slot, generation: generation}

        def service_shape_create_compound(state: ServiceState&, children: CompoundChild&,
            child_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            shape_create_compound_for_generation(state.generation, children, child_count)

        def service_shape_create_triangle_mesh(state: ServiceState&,
            vertices: Geometry::Vec3&, vertex_count: usize, indices: u32&,
            index_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.InvalidArgument if vertex_count == 0 or index_count == 0 or vertex_count > Status::MAX_MESH_VERTICES or index_count > Status::MAX_MESH_INDICES
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_mesh_shape(state.generation,
                Status::MESH_SHAPE_TRIANGLE_MESH, vertices, vertex_count.u32(), indices, index_count.u32(), &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: state.generation, slot: slot, generation: generation}

        def service_shape_create_convex_hull(state: ServiceState&,
            vertices: Geometry::Vec3&, vertex_count: usize, indices: u32&,
            index_count: usize) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.InvalidArgument if vertex_count == 0 or index_count == 0 or vertex_count > Status::MAX_MESH_VERTICES or index_count > Status::MAX_MESH_INDICES
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_mesh_shape(state.generation,
                Status::MESH_SHAPE_CONVEX_HULL, vertices, vertex_count.u32(), indices, index_count.u32(), &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: state.generation, slot: slot, generation: generation}

        def service_mesh_shape_create(state: ServiceState&, shape: MeshShape,
            asset_path: cstr, scale: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            kind: i32 = match shape:
                MeshShape.ConvexHull: Status::MESH_SHAPE_CONVEX_HULL
                MeshShape.TriangleMesh: Status::MESH_SHAPE_TRIANGLE_MESH
            status: i32 = elisa_physics_v1_create_mesh_shape_from_asset(state.generation,
                kind, asset_path, scale.x, scale.y, scale.z, &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: state.generation, slot: slot, generation: generation}

        def service_shape_destroy(state: ServiceState&, shape: ShapeHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if shape.world_generation != state.generation
            try check_status(elisa_physics_v1_destroy_shape(state.generation,
                shape.slot, shape.generation))

        def service_body_create_with_shape(state: ServiceState&,
            description: BodyInstanceDesc, shape: ShapeHandle) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if shape.world_generation != state.generation
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body_with_shape(state.generation,
                kind_code(description.kind), description.position.x, description.position.y,
                description.position.z, description.mass, 1 if description.sensor else 0,
                shape.slot, shape.generation, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: state.generation, slot: slot, generation: generation}
