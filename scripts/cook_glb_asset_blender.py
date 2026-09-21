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


def retarget_clips(armature, source_armature, actions):
    """Bake world-space bone motion without copying source object transforms.

    Joint names identify corresponding bones, not interchangeable action
    channels: the rigs may have different object scales and rest poses.
    Rotation constraints preserve target bone lengths; root translation is
    transferred in world units. Visual baking converts this into target-local
    pose keys while leaving the target object's unit conversion untouched.
    """
    source_armature.animation_data_create()
    for track in source_armature.animation_data.nla_tracks:
        track.mute = True
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    baked = []
    for action in actions:
        source_armature.animation_data.action = action
        if action.slots:
            source_armature.animation_data.action_slot = action.slots[0]
        armature.animation_data_clear()
        for bone in armature.pose.bones:
            bone.matrix_basis.identity()
            rotation = bone.constraints.new("COPY_ROTATION")
            rotation.target = source_armature
            rotation.subtarget = bone.name
            rotation.target_space = "WORLD"
            rotation.owner_space = "WORLD"
            if bone.parent is None:
                location = bone.constraints.new("COPY_LOCATION")
                location.target = source_armature
                location.subtarget = bone.name
                location.target_space = "WORLD"
                location.owner_space = "WORLD"
        start, end = (int(round(value)) for value in action.frame_range)
        bpy.context.scene.frame_set(start)
        bpy.ops.nla.bake(frame_start=start, frame_end=end, step=1,
            only_selected=False, visual_keying=True, clear_constraints=True,
            clear_parents=False, use_current_action=False, clean_curves=False,
            bake_types={"POSE"})
        result = armature.animation_data.action
        if result is None:
            raise RuntimeError("could not bake animation clip: " + clip_name(action))
        result.name = "GLB|" + clip_name(action)
        result.use_fake_user = True
        baked.append(result)
    armature.animation_data_clear()
    return baked


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
        previous_actions = set(bpy.data.actions)
        source_objects = imported(bpy.ops.import_scene.fbx, options.animation_source)
        source_armature = armature_of(source_objects, "animation FBX")
        source_bones = {bone.name for bone in source_armature.data.bones}
        missing = sorted(bones - source_bones)
        if missing:
            raise RuntimeError("animation rig is missing GLB joint names: " + ", ".join(missing))
        actions = [action for action in bpy.data.actions
            if action not in previous_actions and action.frame_range[1] - action.frame_range[0] >= 1.0]
        if not 1 <= len(actions) <= 8:
            raise RuntimeError(f"animation FBX must provide 1 to 8 non-static clips; found {len(actions)}")
        names = [clip_name(action) for action in actions]
        if any(not name for name in names) or len(set(names)) != len(names):
            raise RuntimeError("animation FBX has empty or duplicate clip names")
        for action in actions:
            action.use_fake_user = True
        actions = retarget_clips(armature, source_armature, actions)

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
