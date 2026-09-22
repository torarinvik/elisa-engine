"""Blender-side GLB import, compatible-rig clip transfer, and FBX export."""

import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector


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


def bone_order(armature):
    """Pose bones sorted so every parent precedes its children."""
    ordered = []
    pending = list(armature.pose.bones)
    placed = set()
    while pending:
        remaining = []
        for bone in pending:
            if bone.parent is None or bone.parent.name in placed:
                ordered.append(bone)
                placed.add(bone.name)
            else:
                remaining.append(bone)
        if len(remaining) == len(pending):
            raise RuntimeError("armature bone hierarchy is cyclic")
        pending = remaining
    return ordered


def rest_directions(armature, rotation):
    """World-space rest direction of each joint toward its children.

    Imported GLB joints carry no authored tails, so Blender's bone axes are a
    heuristic there; the joint positions are authored on both rigs and give a
    posture the two can be compared by. Leaves inherit their parent's direction.
    """
    directions = {}
    for bone in bone_order(armature):
        data = bone.bone
        children = [child for child in data.children]
        if children:
            mean = sum((child.head_local for child in children), Vector((0.0, 0.0, 0.0))) / len(children)
            offset = mean - data.head_local
            direction = rotation @ offset if offset.length > 1.0e-6 else None
        else:
            direction = None
        if direction is None:
            direction = directions[bone.parent.name] if bone.parent is not None else rotation @ Vector((0.0, 1.0, 0.0))
        directions[bone.name] = direction.normalized()
    return directions


def retarget_clips(armature, source_armature, actions):
    """Transfer each clip as world-space motion relative to both rest poses.

    Joint names identify corresponding bones, not interchangeable action
    channels: the rigs may have different object scales, rest postures and
    bone axes. For every joint the source's world rotation away from its own
    rest pose is applied on top of the target's rest pose, after a per-joint
    alignment that turns the target's rest posture (for example a T-pose)
    into the source's (for example an A-pose) using the authored joint
    positions. Root translation transfers in world units scaled by rig
    height. Keys are written as target-local pose channels, leaving the
    target object's unit conversion untouched.
    """
    source_armature.animation_data_create()
    for track in source_armature.animation_data.nla_tracks:
        track.mute = True
    source_world = source_armature.matrix_world
    target_world = armature.matrix_world
    source_rotation = source_world.to_quaternion()
    target_rotation = target_world.to_quaternion()
    target_scale = target_world.to_scale()
    source_directions = rest_directions(source_armature, source_rotation)
    target_directions = rest_directions(armature, target_rotation)
    ordered = bone_order(armature)
    rest_source = {}
    rest_target = {}
    align = {}
    for bone in ordered:
        rest_source[bone.name] = source_rotation @ source_armature.data.bones[bone.name].matrix_local.to_quaternion()
        rest_target[bone.name] = target_rotation @ bone.bone.matrix_local.to_quaternion()
        align[bone.name] = target_directions[bone.name].rotation_difference(source_directions[bone.name])
    root = ordered[0]
    source_root_rest = (source_world @ source_armature.data.bones[root.name].matrix_local).to_translation()
    source_height = max((source_world @ Matrix.Translation(bone.head_local)).to_translation().z
        for bone in source_armature.data.bones)
    target_height = max((target_world @ Matrix.Translation(bone.head_local)).to_translation().z
        for bone in armature.data.bones)
    height_ratio = target_height / source_height if source_height > 1.0e-6 else 1.0
    armature.animation_data_create()
    for bone in ordered:
        bone.rotation_mode = "QUATERNION"
    baked = []
    for action in actions:
        source_armature.animation_data.action = action
        if action.slots:
            source_armature.animation_data.action_slot = action.slots[0]
        result = bpy.data.actions.new("GLB|" + clip_name(action))
        result.use_fake_user = True
        armature.animation_data.action = result
        start, end = (int(round(value)) for value in action.frame_range)
        for frame in range(start, end + 1):
            bpy.context.scene.frame_set(frame)
            posed = {}
            for bone in ordered:
                name = bone.name
                source_pose = source_world @ source_armature.pose.bones[name].matrix
                delta = source_pose.to_quaternion() @ rest_source[name].inverted()
                world_rotation = delta @ align[name] @ rest_target[name]
                rotation = (target_rotation.inverted() @ world_rotation).to_matrix().to_4x4()
                rest_local = bone.bone.matrix_local
                if bone.parent is None:
                    travel = source_pose.to_translation() - source_root_rest
                    local_travel = target_rotation.inverted() @ (travel * height_ratio)
                    head = rest_local.to_translation() + Vector(
                        (local_travel.x / target_scale.x, local_travel.y / target_scale.y, local_travel.z / target_scale.z))
                    parent_chain = Matrix.Identity(4)
                else:
                    parent_chain = posed[bone.parent.name] @ bone.parent.bone.matrix_local.inverted()
                    head = (parent_chain @ rest_local).to_translation()
                pose = Matrix.Translation(head) @ rotation
                posed[name] = pose
                bone.matrix_basis = (parent_chain @ rest_local).inverted() @ pose
                bone.keyframe_insert("rotation_quaternion", frame=frame)
                if bone.parent is None:
                    bone.keyframe_insert("location", frame=frame)
        baked.append(result)
    armature.animation_data.action = None
    for bone in ordered:
        bone.matrix_basis.identity()
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
        if not 1 <= len(actions) <= 16:
            raise RuntimeError(f"animation FBX must provide 1 to 16 non-static clips; found {len(actions)}")
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
