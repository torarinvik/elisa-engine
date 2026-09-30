# Run: blender --background --factory-startup --python tools/fbx_glb_reference.py -- IN.fbx OUT.json
# Imports an FBX take with Blender's own importer and writes each bone head's
# world position (converted to glTF's Y-up axes) at a few frames, with the
# seconds since the take's first frame, for scripts/fbx_glb_smoke.py to
# compare with the GLB that native/fbx_to_glb.c bakes.
import json
import sys

import bpy

args = sys.argv[sys.argv.index("--") + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=args[0])
scene = bpy.context.scene
rig = next(o for o in scene.objects if o.type == "ARMATURE")
action = rig.animation_data.action if rig.animation_data else None
first, last = (int(action.frame_range[0]), int(action.frame_range[1])) if action else (scene.frame_start, scene.frame_end)
fps = scene.render.fps / scene.render.fps_base
frames = sorted({first, first + (last - first) // 3, first + 2 * (last - first) // 3, last})
out = {"fps": fps, "frames": []}
for frame in frames:
    scene.frame_set(frame)
    joints = {}
    for bone in rig.pose.bones:
        p = rig.matrix_world @ bone.head
        joints[bone.name] = [p.x, p.z, -p.y]
    out["frames"].append({"seconds": (frame - first) / fps, "joints": joints})
with open(args[1], "w") as handle:
    json.dump(out, handle)
