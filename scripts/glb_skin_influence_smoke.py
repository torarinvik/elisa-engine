#!/usr/bin/env python3
"""Exercise the real loader with two primitives and paired eight-slot weights."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/skin-influence-smoke'


def fixture():
    binary = bytearray()
    views, accessors, primitives = [], [], []
    def add(values, code, component, kind, count):
        while len(binary) % 4:
            binary.append(0)
        payload = struct.pack('<' + code * len(values), *values)
        views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(payload)))
        binary.extend(payload)
        accessors.append(dict(bufferView=len(views)-1, componentType=component, count=count, type=kind))
        return len(accessors)-1
    for total in (0.99, 1.01):
        attributes = dict(POSITION=add([0,0,0, 1,0,0, 0,1,0], 'f', 5126, 'VEC3', 3))
        for index in range(2):
            attributes[f'JOINTS_{index}'] = add([0]*12, 'B', 5121, 'VEC4', 3)
            attributes[f'WEIGHTS_{index}'] = add([total/8]*12, 'f', 5126, 'VEC4', 3)
        primitives.append(dict(attributes=attributes, mode=4))
    document = dict(asset=dict(version='2.0'), nodes=[dict(mesh=0, skin=0), dict(name='joint')],
                    meshes=[dict(primitives=primitives)], skins=[dict(joints=[1])],
                    buffers=[dict(byteLength=len(binary))], bufferViews=views, accessors=accessors)
    return document, binary


def write(name, document, binary):
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    binary += b'\0' * (-len(binary) % 4)
    result = struct.pack('<4sII', b'glTF', 2, 28+len(encoded)+len(binary))
    result += struct.pack('<I4s', len(encoded), b'JSON') + encoded
    result += struct.pack('<I4s', len(binary), b'BIN\0') + binary
    (WORK / name).write_bytes(result)


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    document, binary = fixture()
    write('valid.glb', document, binary)
    for name, key, value in [('unpaired.glb', 'WEIGHTS_1', None), ('third.glb', 'JOINTS_2', 1)]:
        malformed = copy.deepcopy(document)
        attributes = malformed['meshes'][0]['primitives'][0]['attributes']
        if value is None:
            del attributes[key]
        else:
            attributes[key] = value
        write(name, malformed, binary)
    compiler = os.environ.get('ELISA_COMPILER_BIN', str(Path.home()/'.elisac/elisac-stage1'))
    runtime = os.environ.get('ELISA_RUNTIME_OBJECT', str(Path.home()/'.elisac/stage1/build/runtime/elisacore_runtime.o'))
    for optimization in ('O0', 'O2'):
        obj = WORK / f'probe-{optimization}.o'
        exe = WORK / f'probe-{optimization}'
        subprocess.run([compiler, '-emit', 'obj', '-'+optimization, '-o', str(obj), str(ROOT/'test/glb_skin_influences.elisa')], check=True)
        subprocess.run(['clang', '-Wl,-dead_strip', str(obj), runtime, '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)
    print('glb skin influence smoke: paired eight-slot, two-primitive normalization and refusals pass O0/O2')


if __name__ == '__main__':
    main()
