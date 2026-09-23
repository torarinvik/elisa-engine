"""Blender-side static or skinned GLB conversion and compatible-rig clip transfer."""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bpy
from cook_gltf_animation import MAX_CLIPS
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


def retarget_clips(armature, source_armature, actions):
    """Transfer each clip as world-space motion relative to both rest poses.

    Joint names identify corresponding bones, not interchangeable action
    channels: the rigs may have different object scales, rest postures and
    bone axes. For every joint the source's world rotation away from its own
    rest pose is applied on top of the target rest orientation after mapping
    the target's full rest rotation onto the source's. Root translation
    transfers in world units scaled by rig height. Keys are written as
    target-local pose channels, leaving the target object's unit conversion
    untouched.
    """
    source_armature.animation_data_create()
    for track in source_armature.animation_data.nla_tracks:
        track.mute = True
    source_world = source_armature.matrix_world
    target_world = armature.matrix_world
    source_rotation = source_world.to_quaternion()
    target_rotation = target_world.to_quaternion()
    target_scale = target_world.to_scale()
    ordered = bone_order(armature)
    rest_source = {}
    rest_target = {}
    align = {}
    for bone in ordered:
        rest_source[bone.name] = source_rotation @ source_armature.data.bones[bone.name].matrix_local.to_quaternion()
        rest_target[bone.name] = target_rotation @ bone.bone.matrix_local.to_quaternion()
        align[bone.name] = rest_source[bone.name] @ rest_target[bone.name].inverted()
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
    parser.add_argument("--max-triangles", type=int,
        help="coarsely reduce static meshes before bounded FBX conversion")
    return parser.parse_args(argv)


def triangle_count(mesh):
    return sum(max(polygon.loop_total - 2, 0) for polygon in mesh.polygons)


def stage_static_reduction(meshes, max_triangles):
    if max_triangles is None:
        return
    if not 1 <= max_triangles <= 1000000:
        raise RuntimeError("max-triangles must be in [1, 1000000]")
    source_triangles = sum(triangle_count(mesh.data) for mesh in meshes)
    staged_limit = max(1, int(max_triangles * 0.90))
    if source_triangles <= staged_limit:
        return
    ratio = staged_limit / source_triangles
    for mesh in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        mesh.select_set(True)
        bpy.context.view_layer.objects.active = mesh
        modifier = mesh.modifiers.new(name="Elisa bounded GLB staging", type="DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = ratio
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    staged_triangles = sum(triangle_count(mesh.data) for mesh in meshes)
    attempts = 0
    while staged_triangles > max_triangles and attempts < 3:
        previous_triangles = staged_triangles
        ratio = max_triangles * 0.90 / staged_triangles
        for mesh in meshes:
            bpy.ops.object.select_all(action="DESELECT")
            mesh.select_set(True)
            bpy.context.view_layer.objects.active = mesh
            modifier = mesh.modifiers.new(name="Elisa bounded GLB staging", type="DECIMATE")
            modifier.decimate_type = "COLLAPSE"
            modifier.ratio = ratio
            modifier.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        staged_triangles = sum(triangle_count(mesh.data) for mesh in meshes)
        attempts += 1
        if staged_triangles >= previous_triangles:
            break
    if staged_triangles > max_triangles:
        raise RuntimeError("Blender could not reduce the static GLB to its triangle budget")
    print(f"Reduced static GLB for bounded conversion: {source_triangles} -> {staged_triangles} triangles")


def recalculate_static_normals(meshes):
    for mesh in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        mesh.select_set(True)
        bpy.context.view_layer.objects.active = mesh
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        mesh.data.validate(verbose=False, clean_customdata=True)
        mesh.data.update()


def main():
    options = parse_arguments()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    glb_objects = imported(bpy.ops.import_scene.gltf, options.source)
    armatures = [item for item in glb_objects if item.type == "ARMATURE"]
    if not armatures:
        if options.animation_source is not None:
            raise RuntimeError("an animation source requires a skinned GLB armature")
        meshes = [item for item in glb_objects if item.type == "MESH"]
        if not meshes:
            raise RuntimeError("static GLB has no mesh")
        stage_static_reduction(meshes, options.max_triangles)
        if options.max_triangles is not None:
            recalculate_static_normals(meshes)
        # Carry each node's scene transform into its FBX object transform
        # before dropping camera, light and empty helper nodes.
        for mesh in meshes:
            world = mesh.matrix_world.copy()
            mesh.parent = None
            mesh.matrix_world = world
        for item in list(bpy.context.scene.objects):
            if item not in meshes:
                bpy.data.objects.remove(item, do_unlink=True)
        bpy.ops.object.select_all(action="DESELECT")
        for mesh in meshes:
            mesh.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        options.output.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.export_scene.fbx(
            filepath=str(options.output),
            use_selection=True,
            object_types={"MESH"},
            apply_scale_options="FBX_SCALE_ALL",
            add_leaf_bones=False,
            bake_anim=False,
            path_mode="STRIP",
            embed_textures=False,
        )
        print(f"Exported {len(meshes)} static GLB mesh(es) to {options.output}")
        return
    if len(armatures) != 1:
        raise RuntimeError(f"GLB must contain at most one armature; found {len(armatures)}")
    if options.max_triangles is not None:
        raise RuntimeError("max-triangles is not supported for skinned GLB meshes")
    armature = armatures[0]
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
        if not 1 <= len(actions) <= MAX_CLIPS:
            raise RuntimeError(f"animation FBX must provide 1 to {MAX_CLIPS} non-static clips; found {len(actions)}")
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
