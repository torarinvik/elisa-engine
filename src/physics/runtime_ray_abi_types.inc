        struct NativeRayHit:
            entity: u64
            position_x: f32
            position_y: f32
            position_z: f32
            normal_x: f32
            normal_y: f32
            normal_z: f32
            distance: f32

        struct NativeRayHitBuffer:
            hits: NativeRayHit[16]
            count: u32
