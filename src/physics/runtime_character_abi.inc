        @callconv(c)
        extern elisa_physics_v1_create_character(world_generation: u64,
            position_x: f32, position_y: f32, position_z: f32,
            rotation_x: f32, rotation_y: f32, rotation_z: f32, rotation_w: f32,
            radius: f32, cylinder_half_height: f32, mass: f32,
            max_slope_angle: f32, gravity_factor: f32, collision_layer: u32,
            slot: mutable u32&, body_generation: mutable u64&) -> i32

        @callconv(c)
        extern elisa_physics_v1_character_move(world_generation: u64,
            slot: u32, body_generation: u64,
            direction_x: f32, direction_y: f32, direction_z: f32,
            movement_speed: f32, jump_speed: f32, control_in_air: i32) -> i32

        @callconv(c)
        extern elisa_physics_v1_character_info(world_generation: u64,
            slot: u32, body_generation: u64, ground_state: mutable i32&,
            ground_x: mutable f32&, ground_y: mutable f32&, ground_z: mutable f32&,
            normal_x: mutable f32&, normal_y: mutable f32&, normal_z: mutable f32&,
            velocity_x: mutable f32&, velocity_y: mutable f32&,
            velocity_z: mutable f32&) -> i32

        @callconv(c)
        extern elisa_physics_v1_character_set_capsule(world_generation: u64,
            slot: u32, body_generation: u64, radius: f32,
            cylinder_half_height: f32, changed: mutable i32&) -> i32
