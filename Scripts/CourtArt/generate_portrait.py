"""Generate original torn portrait geometry and patina. The old portrait texture is reused in UE."""
import json
import math
from pathlib import Path
import random
import struct

from PIL import Image, ImageFilter

TARGET = Path(__file__).resolve().parents[2] / 'Saved/CourtArtSource'
TARGET.mkdir(parents=True, exist_ok=True)
GOLD, WOOD, LINEN, CANVAS = range(4)
MATERIALS = ('Gilt', 'Timber', 'Linen', 'Canvas')


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


class Mesh:
    def __init__(self):
        self.slots = [[] for _ in MATERIALS]

    def triangle(self, material, positions, uv):
        # The model is mounted against a wall, with its finished face toward local -X.
        positions = [(depth, z, width) for depth, width, z in positions]
        axis = cross(sub(positions[1], positions[0]), sub(positions[2], positions[0]))
        length = math.sqrt(sum(x*x for x in axis))
        if length > 1e-9:
            normal = tuple(x/length for x in axis)
            self.slots[material].extend((p, normal, t) for p, t in zip(positions, uv))

    def quad(self, material, points, uv):
        for indices in ((0, 1, 2), (0, 2, 3)):
            self.triangle(material, [points[i] for i in indices], [uv[i] for i in indices])

    def save(self, name):
        data, views, accessors, primitives = bytearray(), [], [], []

        def emit(rows, kind, arity):
            start = len(data)
            for row in rows:
                data.extend(struct.pack('<'+'f'*arity, *row))
            views.append(dict(buffer=0, byteOffset=start, byteLength=len(data)-start, target=34962))
            spec = dict(bufferView=len(views)-1, componentType=5126, count=len(rows), type=kind)
            if kind == 'VEC3':
                spec['min'] = [min(row[i] for row in rows) for i in range(arity)]
                spec['max'] = [max(row[i] for row in rows) for i in range(arity)]
            accessors.append(spec)
            return len(accessors)-1

        for i, slot in enumerate(self.slots):
            if not slot:
                continue
            p, n, uv = zip(*slot)
            primitives.append(dict(attributes=dict(POSITION=emit(p, 'VEC3', 3), NORMAL=emit(n, 'VEC3', 3),
                                  TEXCOORD_0=emit(uv, 'VEC2', 2)), material=i, mode=4))
        doc = dict(asset=dict(version='2.0', generator='CastleDemo original splintered gilt frame'), scene=0,
                   scenes=[dict(nodes=[0])], nodes=[dict(mesh=0, name=name)],
                   meshes=[dict(name=name, primitives=primitives)], materials=[dict(name=m) for m in MATERIALS],
                   buffers=[dict(byteLength=len(data))], bufferViews=views, accessors=accessors)
        js = json.dumps(doc, separators=(',', ':')).encode()
        js += b' '*(-len(js)%4)
        binary = struct.pack('<4sII', b'glTF', 2, 28+len(js)+len(data))
        binary += struct.pack('<I4s', len(js), b'JSON')+js
        binary += struct.pack('<I4s', len(data), b'BIN\x00')+data
        (TARGET/(name+'.glb')).write_bytes(binary)
        print(name, sum(map(len, self.slots))//3, 'triangles')


def moulding(mesh, a, b, breaks=(False, False), jitter=0):
    """Extrude a stepped, convex moulding around a timber core with splintered end grain."""
    dy, dz = sub(b, a)
    length = math.hypot(dy, dz)
    uy, uz = dy/length, dz/length
    # Transverse position, protrusion from wall, face material going to the next vertex.
    profile = [(-.11,.034,WOOD),(-.11,-.018,GOLD),(-.10,-.062,WOOD),(-.08,-.067,GOLD),
               (-.061,-.103,GOLD),(-.039,-.108,GOLD),(-.008,-.092,WOOD),(.023,-.073,WOOD),
               (.043,-.073,GOLD),(.059,-.1,GOLD),(.074,-.095,GOLD),(.088,-.054,GOLD),
               (.103,-.047,GOLD),(.11,-.02,WOOD),(.11,.034,WOOD)]
    broken = [0,.044,-.022,.016,-.067,.035,-.041,.052,-.049,.067,-.029,.032,-.021,.018,0]
    rings = []
    for j, end in enumerate((a, b)):
        ring = []
        for i, (across, depth, _) in enumerate(profile):
            fault = broken[(i+jitter)%len(broken)] if breaks[j] else 0
            ring.append((depth, end[0]-uz*across+uy*fault, end[1]+uy*across+uz*fault))
        rings.append(ring)
    for i, (_, _, mat) in enumerate(profile):
        k = (i+1)%len(profile)
        mesh.quad(mat, (rings[0][i], rings[0][k], rings[1][k], rings[1][i]),
                  ((0,i*.23),(0,k*.23),(length/.91,k*.23),(length/.91,i*.23)))
    for j, ring in enumerate(rings):
        mid = tuple(sum(coord)/len(ring) for coord in zip(*ring))
        for i in range(len(ring)):
            span = (ring[(i+1)%len(ring)], ring[i]) if j else (ring[i], ring[(i+1)%len(ring)])
            mesh.triangle(LINEN if breaks[j] and i%3 else WOOD, (mid,*span), ((.5,.5),(i/len(ring),0),((i+1)/len(ring),0)))


def rosette(mesh, y, z, radius):
    n = 20
    center = (-.123,y,z)
    def lobe(i, scale, d):
        angle = 2*math.pi*i/n
        r = radius*scale*(1 if i%4 in (1,2) else .73)
        return d,y+r*math.cos(angle),z+r*math.sin(angle)
    for i in range(n):
        a,b,c,d = lobe(i,1,-.103),lobe(i+1,1,-.103),lobe(i+1,.66,-.14),lobe(i,.66,-.14)
        mesh.quad(GOLD,(a,b,c,d), ((0,0),(1,0),(1,1),(0,1)))
        mesh.triangle(GOLD,(center,d,c), ((.5,.5),(1,0),(0,1)))


def silhouette_triangles(points):
    def side(p, q, r):
        return (q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0])
    left = list(range(len(points)))
    while len(left) > 3:
        for j, b in enumerate(left):
            a, c = left[j-1], left[(j+1)%len(left)]
            p, q, r = points[a], points[b], points[c]
            if side(p,q,r) <= 0:
                continue
            if any(min(side(p,q,points[k]),side(q,r,points[k]),side(r,p,points[k])) >= -1e-9
                   for k in left if k not in (a,b,c)):
                continue
            yield p,q,r
            del left[j]
            break
        else:
            raise ValueError('Could not triangulate torn canvas')
    yield tuple(points[k] for k in left)


def canvas_point(p, depth_bias=0):
    u,v = p
    curl = .059*max(0,v-.41)*math.sin(9*u+6*v) - .014*math.cos(3*u+2*v)
    return -.054+curl+depth_bias,(u-.5)*1.81,(.5-v)*2.60


def ribbon(mesh, a, b, width, material, depth=-.003):
    uy,vz=sub(b,a)
    scale=width/math.hypot(uy,vz)
    offset=(-vz*scale,uy*scale)
    extend=lambda p:(p[0]+offset[0],p[1]+offset[1])
    mesh.quad(material,[canvas_point(p,depth) for p in (a,b,extend(b),extend(a))],((0,0),(0,1),(1,1),(1,0)))


def portrait():
    mesh = Mesh()
    moulding(mesh,(-.988,-1.35),(-.988,1.35))
    moulding(mesh,(-.99,1.35),(.99,1.35))
    moulding(mesh,(.988,1.35),(.988,-.12),(False,True),4)
    moulding(mesh,(-.99,-1.35),(-.24,-1.35),(False,True),6)
    for y,z in ((-.988,-1.35),(-.988,1.35),(.988,1.35)):
        rosette(mesh,y,z,.122)
    # A row of discrete gesso beads rather than a single straight highlight on the long rails.
    for i in range(1,17):
        z = -1.29+i*.150
        if i not in (3,4,12):
            rosette(mesh,-.953,z,.021)
        if z > -.045 and i not in (11,15):
            rosette(mesh,.955,z,.021)
    for i in range(1,12):
        if i != 4:
            rosette(mesh,-.94+i*.153,1.312,.021)

    tear = [(1,.56),(.93,.55),(.902,.61),(.85,.591),(.83,.659),(.79,.627),(.746,.699),(.706,.673),
            (.686,.725),(.618,.710),(.586,.743),(.644,.787),(.572,.765),(.533,.855),(.509,.822),
            (.443,.921),(.408,.911),(.371,.978),(.312,.95),(.224,1),(.13,.986),(.08,1)]
    outline = [(0,0),(1,0),*tear,(0,1)]
    for uv in silhouette_triangles(outline):
        mesh.triangle(CANVAS, [canvas_point(point) for point in uv], uv)
    for i,(a,b) in enumerate(zip(tear,tear[1:])):
        ribbon(mesh,a,b,.0043 if i%3 else .0061,LINEN)
    branch = [(.618,.710),(.597,.656),(.607,.624),(.568,.593),(.58,.572),(.546,.541),(.555,.510)]
    for i,(a,b) in enumerate(zip(branch,branch[1:])):
        ribbon(mesh,a,b,(-.003 if i%2 else .005)/(1+i*.22),WOOD,-.009)
    mesh.save('SM_ShatteredCourtPortrait')

    rail = Mesh()
    moulding(rail,(-.55,0),(.55,.05),(True,True),8)
    for y in (-.37,-.18,.04,.228,.38):
        rosette(rail,y,.04,.024)
    rail.save('SM_DroppedFrameRail')


def patina():
    rng = random.Random(9432)
    n = 512
    def field(cells):
        image = Image.new('L',(cells,cells))
        image.putdata([rng.randrange(25,255) for _ in range(cells*cells)])
        return image.resize((n,n),Image.Resampling.BICUBIC)
    large = field(13).load()
    small = field(65).load()
    wood = Image.new('L',(n,n))
    values = []
    for y in range(n):
        for x in range(n):
            grain = 12*math.sin(y*.93+.55*math.cos(x*.051))+4*math.sin(y*.37+x*.009)
            wear = .72*large[x,y]+.20*small[x,y]+grain+rng.randrange(-18,19)
            if rng.random() < .015:
                wear *= .5
            values.append(int(max(0,min(255,wear))))
    wood.putdata(values)
    wood.filter(ImageFilter.UnsharpMask(radius=1.3,percent=35,threshold=2)).save(TARGET/'T_CourtFramePatina.png')


if __name__ == '__main__':
    portrait()
    patina()
