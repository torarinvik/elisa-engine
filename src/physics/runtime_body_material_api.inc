        def service_body_set_material(state: ServiceState&, body: BodyHandle,
            material: PhysicsMaterial) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_set_material(state.generation, body,
                material.friction, material.restitution)

        def body_set_material(world: World&, body: BodyHandle,
            material: PhysicsMaterial) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_set_material(world.generation, body,
                material.friction, material.restitution)
