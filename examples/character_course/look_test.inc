# Mouse-look checks for the character-course self-test, included in the
# CharacterCourse private section after `pad_test.inc`. Pointer deltas are
# synthetic; the relative-mode request runs against the live window.
        def look_close(a: f32, b: f32) -> bool:
            ElisaMath::fabsf(a - b) < 0.0001

        def look_test() -> i32:
            still: CourseLook::Look = CourseLook::Look()
            offset: Geometry::Vec3 = Geometry::Vec3{x: Tune::CAMERA_OFFSET_X, y: Tune::CAMERA_OFFSET_Y, z: Tune::CAMERA_OFFSET_Z}
            fixed: Geometry::Vec3 = CourseLook::rotate(still, offset)
            return 240 if not look_close(fixed.x, offset.x) or not look_close(fixed.z, offset.z)
            # A quarter turn to the left of the +x forward axis faces -z.
            quarter: CourseLook::Look = CourseLook::turn(still, -CourseLook::Tune::PI / 2.0 / CourseLook::Tune::SENSITIVITY)
            forward: Geometry::Vec3 = CourseLook::rotate(quarter, Geometry::Vec3{x: 1.0, y: 0.0, z: 0.0})
            return 241 if not look_close(forward.x, 0.0) or not look_close(forward.z, -1.0)
            # The camera keeps the same bearing to the movement axis at any yaw.
            eye: Geometry::Vec3 = CourseLook::rotate(quarter, offset)
            return 242 if not look_close(eye.x * forward.x + eye.z * forward.z, offset.x) or not look_close(eye.y, offset.y)
            # Long drags wrap into [-pi, pi] instead of growing without bound.
            spun: mutable CourseLook::Look = still
            for _ in 0..<400 |spun|:
                spun <- CourseLook::turn(spun, 97.0)
            return 243 if spun.yaw > CourseLook::Tune::PI or spun.yaw < -CourseLook::Tune::PI
            # Relative mode either engages and releases or is refused cleanly.
            engaged: Application::RelativeMouse = Application::set_relative_mouse(true)
            return 244 if engaged == Application::RelativeMouse.Enabled and not Application::relative_mouse()
            return 245 if engaged == Application::RelativeMouse.NotRunning
            _ = Application::set_relative_mouse(false)
            return 246 if Application::relative_mouse()
            # With the pointer free, queued motion never turns the camera.
            drained: CourseLook::Look = CourseLook::drain(still, false)
            return 247 if drained.yaw != 0.0
            # Reduced motion turns half as far for the same drag.
            gentle: CourseLook::Look = CourseLook::turn_at(still, 100.0, true)
            full: CourseLook::Look = CourseLook::turn_at(still, 100.0, false)
            return 248 if not look_close(gentle.yaw * 2.0, full.yaw) or full.yaw == 0.0
            0
