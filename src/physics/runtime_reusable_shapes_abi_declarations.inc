        @callconv(c)
        extern elisa_physics_v1_create_shape(world_generation: u64, kind: i32,
            dimension_x: f32, dimension_y: f32, dimension_z: f32,
            slot: mutable u32&, shape_generation: mutable u64&) -> i32

        @callconv(c)
        extern elisa_physics_v1_create_mesh_shape_from_asset(world_generation: u64, kind: i32,
            asset_path: cstr, scale_x: f32, scale_y: f32, scale_z: f32,
            slot: mutable u32&, shape_generation: mutable u64&) -> i32

        @callconv(c)
        extern elisa_physics_v1_create_mesh_shape(world_generation: u64, kind: i32,
            vertices: Geometry::Vec3&, vertex_count: u32, indices: u32&, index_count: u32,
            slot: mutable u32&, shape_generation: mutable u64&) -> i32

        @callconv(c)
        extern elisa_physics_v1_destroy_shape(world_generation: u64,
            slot: u32, shape_generation: u64) -> i32

        @callconv(c)
        extern elisa_physics_v1_create_body_with_shape(world_generation: u64,
            kind: i32, position_x: f32, position_y: f32, position_z: f32,
            mass: f32, sensor: i32, shape_slot: u32, shape_generation: u64,
            body_slot: mutable u32&, body_generation: mutable u64&) -> i32
