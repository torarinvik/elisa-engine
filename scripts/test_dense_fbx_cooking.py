"""End-to-end dense FBX sampling and unsupported-rate rejection with Blender."""
import base64
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from cook_glb_asset import blender_executable

ROOT=Path(__file__).resolve().parents[1]
FIXTURE='''import bpy,sys
from pathlib import Path
folder=Path(sys.argv[sys.argv.index('--')+1])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.object.armature_add();rig=bpy.context.object
bpy.ops.mesh.primitive_cube_add(size=1);mesh=bpy.context.object
mesh.parent=rig;modifier=mesh.modifiers.new('Skin','ARMATURE');modifier.object=rig
name=rig.data.bones[0].name;group=mesh.vertex_groups.new(name=name);group.add(list(range(len(mesh.data.vertices))),1.0,'REPLACE')
bone=rig.pose.bones[name];bone.location=(0,0,0);bone.keyframe_insert('location',frame=1)
bone.location=(.1,0,0);bone.keyframe_insert('location',frame=121)
scene=bpy.context.scene;scene.frame_start=1;scene.frame_end=121
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);mesh.select_set(True);bpy.context.view_layer.objects.active=rig
for rate in [120,240]:
 scene.render.fps=rate;scene.render.fps_base=1
 bpy.ops.export_scene.fbx(filepath=str(folder/f'{rate}.fbx'),use_selection=True,add_leaf_bones=False,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,axis_forward='-Z',axis_up='Y')
'''

def main():
    with tempfile.TemporaryDirectory(prefix='Elisa dense animation test ') as directory:
        folder=Path(directory);fixture=folder/'fixture.py';fixture.write_text(FIXTURE)
        subprocess.run([blender_executable(),'--background','--factory-startup','--python-exit-code','1','--python',str(fixture),'--',str(folder)],check=True)
        cooker=ROOT/'scripts/cook_fbx_asset.py'
        for rate in (120,240):
            output=folder/f'{rate}.pkg'
            result=subprocess.run([sys.executable,str(cooker),str(folder/f'{rate}.fbx'),'--asset-path',f'test/{rate}.fbx','--output',str(output)],capture_output=True,text=True)
            if rate==240:
                assert result.returncode!=0 and 'authored rate exceeds the 120 Hz' in result.stdout+result.stderr,result.stdout+result.stderr
                assert not output.exists()
                continue
            assert result.returncode==0,result.stdout+result.stderr
            fields=dict(line.split('=',1) for line in output.read_text().splitlines() if '=' in line)
            assert fields['animation_clips']=='1' and fields['animation_0_sample_rate']=='120' and fields['animation_0_frames']=='121'
            assert abs(float(fields['animation_0_duration_seconds'])-1)<1e-6
            values=struct.unpack('<'+str(len(base64.b64decode(fields['animation_0_samples_b64']))//4)+'f',base64.b64decode(fields['animation_0_samples_b64']))
            count=int(fields['skin_joints']);stride=count*10
            assert any(abs(values[-stride+joint*10]-values[joint*10])>.05 for joint in range(count))
    print('Dense FBX tests pass: 120 Hz samples/duration/motion retained; 240 Hz rejected without output.')

if __name__=='__main__':
    main()
