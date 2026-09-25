        struct BodyDesc:
            kind: BodyKind
            shape: BodyShape
            position: Geometry::Vec3
            rotation: Geometry::Quat
            # Box: half-extents. Sphere: x is radius. Capsule: x is radius,
            # y is the half-height of its straight cylindrical section.
            dimensions: Geometry::Vec3
            mass: f32
            sensor: bool
            collision_layer: u32

        struct BodyPose:
            position: Geometry::Vec3
            rotation: Geometry::Quat
