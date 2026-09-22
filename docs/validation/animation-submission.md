# Animation submission validation

`native/animation_submission_bridge.h` is a narrow Wicked adapter for Elisa
poses. Each handle owns two bounded pose buffers, validates every matrix and
morph weight, applies the active buffer to armature transforms and mesh morph
targets, rejects a second submission while the first is in flight, and refuses
retirement until the caller marks the submission complete. The handle carries
owner and generation checks so a pose cannot cross bridge instances or outlive
its logical animation resource.

`RenderSceneAnimationSubmission` exposes that route without exposing native
objects: `RenderScene::AnimationPose()` owns private fixed-size bone and morph
arrays, setters accept engine `Geometry` values, and `submit_animation_pose` /
`complete_animation_pose` use the existing generation-checked
`RenderScene::InstanceHandle`. Skinned cooked meshes create the native bridge
handle lazily, so ordinary instances pay no submission allocation.

Evidence: the SDL3/Wicked gate creates a v3 cooked one-joint skinned mesh,
submits a bone transform through the public Elisa API, rejects an in-flight
second submission, verifies completion, and destroys the instance. The native
bridge probe independently submits a morph weight and checks Wicked's morph
target state and owner validation. Multi-instance render bounds and
Elisa-driven runtime scheduling remain open R08 work.
