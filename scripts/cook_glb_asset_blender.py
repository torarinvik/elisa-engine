"""Blender-side GLB import, compatible-rig clip transfer, and FBX export."""

import argparse
from pathlib import Path
import sys

import bpy


def imported(importer, path):
    before = set(bpy.data.objects)
    importer(filepath=str(path))
    return [item for item in bpy.data.objects if item not in before]


def armature_of(objects, label):
    armatures = [item for item in objects if item.type == "ARMATURE"]
    if len(armatures) != 1:
        raise RuntimeError(f"{label} must contain exactly one armature; found {len(armatures)}")
    return armatures[0]


def clip_name(action):
    return action.name.split("|")[-1].strip()


def parse_arguments():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--animation-source", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(argv)


def main():
    options = parse_arguments()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    glb_objects = imported(bpy.ops.import_scene.gltf, options.source)
    armature = armature_of(glb_objects, "GLB")
    bones = {bone.name for bone in armature.data.bones}
    meshes = [item for item in glb_objects if item.type == "MESH" and item.parent == armature and
        len(item.vertex_groups) > 0]
    if not meshes:
        raise RuntimeError("GLB armature has no skinned child mesh")
    if any(group.name not in bones for mesh in meshes for group in mesh.vertex_groups):
        raise RuntimeError("GLB skinned mesh references a vertex group absent from its armature")

    actions = []
    if options.animation_source is not None:
        source_objects = imported(bpy.ops.import_scene.fbx, options.animation_source)
        source_armature = armature_of(source_objects, "animation FBX")
        source_bones = {bone.name for bone in source_armature.data.bones}
        missing = sorted(bones - source_bones)
        if missing:
            raise RuntimeError("animation rig is missing GLB joint names: " + ", ".join(missing))
        actions = [action for action in bpy.data.actions
            if action.frame_range[1] - action.frame_range[0] >= 1.0]
        if not 1 <= len(actions) <= 8:
            raise RuntimeError(f"animation FBX must provide 1 to 8 non-static clips; found {len(actions)}")
        names = [clip_name(action) for action in actions]
        if any(not name for name in names) or len(set(names)) != len(names):
            raise RuntimeError("animation FBX has empty or duplicate clip names")
        for action in actions:
            action.use_fake_user = True

    keep_objects = {armature, *meshes}
    for item in list(bpy.context.scene.objects):
        if item not in keep_objects:
            bpy.data.objects.remove(item, do_unlink=True)

    if actions:
        armature.animation_data_create()
        armature.animation_data.action = None
        for track in list(armature.animation_data.nla_tracks):
            armature.animation_data.nla_tracks.remove(track)
        for action in actions:
            name = clip_name(action)
            track = armature.animation_data.nla_tracks.new()
            track.name = name
            strip = track.strips.new(name, int(round(action.frame_range[0])), action)
            if action.slots:
                strip.action_slot = action.slots[0]
            strip.blend_type = "REPLACE"

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = armature
    options.output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=str(options.output),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        apply_scale_options="FBX_SCALE_ALL",
        add_leaf_bones=False,
        bake_anim=bool(actions),
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=bool(actions),
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
        path_mode="STRIP",
        embed_textures=False,
    )
    print(f"Exported {len(meshes)} GLB skinned mesh(es) and {len(actions)} animation clip(s) to {options.output}")


if __name__ == "__main__":
    main()
