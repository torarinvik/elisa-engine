"""Run with Blender --background --python to check animation unit preservation."""
import sys
from pathlib import Path

import bpy
from mathutils import Quaternion

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cook_glb_asset_blender import retarget_clips


def rig(name, scale, sideways=False):
    data = bpy.data.armatures.new(name)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    root = data.edit_bones.new('Root')
    root.head = (0, 0, 0)
    root.tail = (0, 0, 1 / scale)
    child = data.edit_bones.new('Child')
    child.parent = root
    child.head = root.tail
    child.tail = (1 / scale, 0, 1 / scale) if sideways else (0, 0, 2 / scale)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.scale = (scale,) * 3
    obj.select_set(False)
    return obj


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    source = rig('Source', 1.0)
    target = rig('Target', 0.01, sideways=True)
    actions = []
    for index, name in enumerate(('Walk', 'Backpedal')):
        source.animation_data_clear()
        for frame in (1, 3, 5):
            source.scale = (1, 1, 1)
            source.keyframe_insert('scale', frame=frame)
            source.location = (2, 0, 0)
            source.keyframe_insert('location', frame=frame)
            root = source.pose.bones['Root']
            root.location = ((frame - 1) * (0.1 if index == 0 else -0.1), 0, 0)
            root.keyframe_insert('location', frame=frame)
            child = source.pose.bones['Child']
            child.rotation_mode = 'QUATERNION'
            child.rotation_quaternion = Quaternion((0, 1, 0), frame * (0.1 + index * 0.1))
            child.keyframe_insert('rotation_quaternion', frame=frame)
        action = source.animation_data.action
        action.name = name
        action.use_fake_user = True
        actions.append(action)
    baked = retarget_clips(target, source, actions)
    assert len(baked) == 2
    for original, result in zip(actions, baked):
        source.animation_data.action = original
        source.animation_data.action_slot = original.slots[0]
        target.animation_data_create()
        target.animation_data.action = result
        target.animation_data.action_slot = result.slots[0]
        for frame in (1, 2, 3, 4, 5):
            bpy.context.scene.frame_set(frame)
            assert max(abs(s - 0.01) for s in target.scale) < 1e-7
            assert target.location.length < 1e-7
            for name in ('Root', 'Child'):
                src = source.matrix_world @ source.pose.bones[name].matrix
                dst = target.matrix_world @ target.pose.bones[name].matrix
                assert abs(src.to_quaternion().dot(dst.to_quaternion())) > 0.99999
                if name == 'Root':
                    assert (src.translation - dst.translation).length < 1e-5
            child = target.pose.bones['Child']
            length = (target.matrix_world @ child.tail - target.matrix_world @ child.head).length
            assert abs(length - 1.0) < 1e-5, length
        curves = [curve for layer in result.layers for strip in layer.strips
            for slot in result.slots for curve in strip.channelbag(slot).fcurves]
        assert curves and all(curve.data_path.startswith('pose.bones[') for curve in curves)
    print('GLB retarget regression passed: scale, rest orientation, root motion, two clips.')


if __name__ == '__main__':
    main()
