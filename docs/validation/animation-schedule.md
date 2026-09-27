# Render-scene animation schedule

`RenderSceneAnimationSchedule` advances Elisa-selected Wicked clip instances as
one bounded frame update. Each active instance receives a unique positive
`AnimationKey`; the schedule and its native handles stay private to
`RenderScene`. Playback registration happens only after the backend accepts the
clip, duplicate keys are rejected, and the fixed-size table allocates no memory
while advancing. Pause/resume gates the whole set. `destroy` removes the native
instance and its schedule entry together; callers that destroy an instance
through another API must `forget` its key first.

The scheduler rejects negative, non-finite, and greater-than-one-second frame
deltas before touching any instance. An update reports how many tracked
instances it advanced. A backend error stops that update at the failing entry;
instances earlier in the table may already have advanced.

The SDL3/Metal render-scene smoke verifies two independent animated/morphed
instances advance from one scheduler call, paused updates leave both poses
unchanged, duplicate keys and invalid deltas are rejected, removing one player
does not stop the other, and destroy clears each schedule entry. Run it with:

```sh
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
ELISA_COMPILER_BIN=../Elisa-compiler/bin/elisac-stage1 \
WICKED_ROOT=../amazing-labyrinth-wickedengine \
WICKED_BUILD=../amazing-labyrinth-wickedengine/build-elisa-sdl3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/render_scene_native_smoke.py
```

`RenderScene::animation_pose_from_clip` samples an `AnimationAssets` skeleton
and clip in Elisa and copies the resulting model transforms into the same fixed
`AnimationPose` payload. Morph weights remain independently editable before the
checked double-buffered submission. The native smoke samples an endpoint pose,
submits it through Wicked, and checks that an out-of-range tick maps to
`RenderSceneError.InvalidValue`.

The scheduler currently advances Wicked clip players already loaded by a
rendered instance. Feeding freshly sampled Elisa poses from the scheduler each
frame, and sharing one source asset among animated clones, remain open work.
