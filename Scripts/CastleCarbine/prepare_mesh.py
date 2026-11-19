"""Convert only Kenney's blaster-f from the original Blaster Kit 2.1 ZIP to the project GLB."""
import collections
import hashlib
import json
import pathlib
import struct
import sys
import zipfile


def linear(b):
    s = int(b, 16) / 255.0
    return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4


def color(hexcode):
    return [linear(hexcode[i:i+2]) for i in (0, 2, 4)]


def build(archive, output):
    with zipfile.ZipFile(archive) as kit:
        obj = kit.read('Models/OBJ format/blaster-f.obj')
        notice = kit.read('License.txt')
    print('Original blaster-f.obj SHA-256:', hashlib.sha256(obj).hexdigest())
    output.mkdir(parents=True, exist_ok=True)
    (output / 'Kenney_License.txt').write_bytes(notice)
    vertices, normals, uvs = [], [], []
    slots = collections.defaultdict(list)
    group = None
    for line in obj.decode().splitlines():
        cells = line.split()
        if not cells:
            continue
        key, fields = cells[0], cells[1:]
        if key in ('v', 'vt', 'vn'):
            {'v': vertices, 'vt': uvs, 'vn': normals}[key].append(tuple(map(float, fields)))
        elif key == 'g':
            group = fields[0]
        elif key == 'f':
            corners = [tuple(int(v) - 1 for v in corner.split('/')) for corner in fields]
            # UV columns in the original one-texture palette identify the authored metal panels.
            col = round(16 * uvs[corners[0][1]][0] - .5)
            slot = {1: 'Oxblood', 3: 'Iron', 5: 'Brass', 9: 'Pewter'}[col]
            if group == 'scope' and all(abs(normals[ni][2]) > .94 for _, _, ni in corners):
                slot = 'Glass'
            for i in range(1, len(corners)-1):
                slots[slot].extend((corners[0], corners[i], corners[i+1]))

    palette = {'Iron': ('443F49', .43, .54), 'Oxblood': ('623143', .12, .72),
               'Pewter': ('898783', .52, .47), 'Brass': ('A68857', .58, .45),
               'Glass': ('A89BD4', .22, .30)}
    binary = bytearray()
    views, accessors, primitives, materials = [], [], [], []

    def accessor(values, dims, fmt, component_type, target, bounds=False):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        for item in values:
            binary.extend(struct.pack('<' + fmt*dims, *item))
        view = len(views)
        views.append(dict(buffer=0, byteOffset=start, byteLength=len(binary)-start, target=target))
        record = dict(bufferView=view, componentType=component_type, count=len(values), type={1:'SCALAR',3:'VEC3'}[dims])
        if bounds:
            record.update(min=list(map(min,zip(*values))), max=list(map(max,zip(*values))))
        accessors.append(record)
        return len(accessors)-1

    for slot, (hexcolor, metallic, roughness) in palette.items():
        corners = slots[slot]
        assert corners, slot
        remap, positions, directions, indices = {}, [], [], []
        for vi, _, ni in corners:
            if (vi, ni) not in remap:
                remap[vi, ni] = len(positions)
                x, y, z = vertices[vi][:3]
                # Local +X is forward; the old sightline sits 1 cm above the camera's gun mount.
                positions.append((-.48*z, .48*(y-.065)+.01, .48*x))
                nx, ny, nz = normals[ni]
                directions.append((-nz, ny, nx))
            indices.append((remap[vi,ni],))
        attrs = {'POSITION': accessor(positions,3,'f',5126,34962,True),
                 'NORMAL': accessor(directions,3,'f',5126,34962)}
        primitives.append(dict(attributes=attrs,indices=accessor(indices,1,'H',5123,34963),material=len(materials)))
        mat = dict(name='Carbine_'+slot, pbrMetallicRoughness=dict(baseColorFactor=color(hexcolor)+[1],
                   metallicFactor=metallic, roughnessFactor=roughness))
        if slot == 'Glass':
            mat['emissiveFactor'] = [v*.65 for v in color(hexcolor)]
        materials.append(mat)
    name = 'SM_ArchiveCarbine'
    gltf = dict(asset={'version':'2.0','generator':'CastleDemo / Kenney Blaster Kit 2.1'}, scene=0,
                scenes=[dict(nodes=[0])],nodes=[dict(mesh=0,name=name)],meshes=[dict(name=name,primitives=primitives)],
                materials=materials,buffers=[dict(byteLength=len(binary))],bufferViews=views,accessors=accessors)
    js = json.dumps(gltf,separators=(',',':')).encode()
    js += b' '*((-len(js))%4)
    binary.extend(b'\x00'*((-len(binary))%4))
    payload=struct.pack('<I4s',len(js),b'JSON') + js + struct.pack('<I4s',len(binary),b'BIN\x00') + binary
    (output / (name+'.glb')).write_bytes(struct.pack('<4sII',b'glTF',2,12+len(payload))+payload)
    print(name, sum(len(v)//3 for v in slots.values()), 'triangles;', {k:len(v)//3 for k,v in slots.items()})


if __name__ == '__main__':
    build(pathlib.Path(sys.argv[1]), pathlib.Path(__file__).resolve().parents[2]/'SourceArt'/'CastleCarbine')
