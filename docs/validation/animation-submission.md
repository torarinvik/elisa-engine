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

Evidence: the SDL3/Wicked gate creates two independent v3 cooked one-joint
skinned meshes from the same source package, submits separate bone transforms
through the public Elisa API, advances Wicked's armature update, and verifies
each bone's state and bounds independently. It rejects an in-flight second
submission, verifies explicit completion for both instances, and destroys both
instances. The native bridge probe independently submits a morph weight and
checks Wicked's morph-target state and owner validation. The clip-player and
Elisa-sampled pose schedules are now implemented and covered separately in
[`animation-schedule.md`](animation-schedule.md).

## Animated clones and device-consumed completion (2026-10-02)

`RenderScene::create_animated_mesh_instance(source, transform, color)` clones
a skinned or morphed cooked instance. Wicked deforms per mesh component, so
each clone gets its own mesh, armature and bones, while the decoded cooked
asset (geometry, joints, clips, morph defaults) is held once through a
`shared_ptr` that every clone retains. The asset outlives a destroyed source.
Static or non-animated sources are rejected with `InvalidValue`.

A submitted pose now records the device frame that will read it
(`GetFrameCount() + 1`). `complete_animation_pose` returns `AssetPending`
until that frame has been submitted, then waits on the GPU if the frame has
not finished, so the in-flight buffer is released only after device
consumption. `RenderScene::animation_pose_state` reports Idle, InFlight or
Consumed.

Evidence: render smoke group 236 (`test/render_scene_animation_clone_native.elisa`)
checks one shared asset with distinct entities, meshes, armatures and bones;
static-clone rejection; separate poses moving separate bones and bounds after
a pump; `AssetPending` before any frame; asset ownership after the source and
then the first clone are destroyed; and instance-count cleanup. Negative
controls: not retaining the asset fails 236 case 2; skipping the consumption
wait fails group 227 case 130.
