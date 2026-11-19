"""Generate the court painter's standard from original geometry and vector heraldry.

Run with ordinary Python, Pillow and NumPy. The UE import script consumes both
files; generated intermediates stay in Saved, and the packaged assets stand alone.
"""
import json
import math
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageDraw

TARGET = Path(__file__).resolve().parents[2] / 'Saved/CourtArtSource'
TARGET.mkdir(parents=True, exist_ok=True)


def make_mesh():
    # UE dimensions in metres. glTF local axes (UE X, UE Z, UE Y).
    cols, rows = 40, 19
    # Real torn lower edge: unequal swallowtails, an off-center split, nicks.
    points = [(0,.30),(.05,.22),(.10,.15),(.125,.20),(.15,.09),(.20,.03),
              (.225,.00),(.25,.18),(.275,.14),(.35,.40),(.40,.49),(.45,.58),
              (.475,.68),(.50,.57),(.55,.49),(.60,.43),(.65,.28),(.675,.35),
              (.70,.21),(.75,.12),(.775,.09),(.80,.19),(.825,.13),(.875,.25),
              (.90,.41),(.925,.31),(.95,.35),(1,.42)]
    contour = np.array(points)
    positions, uvs, indices = [], [], []
    for j in range(rows+1):
        v = j/rows
        for i in range(cols+1):
            u = i/cols
            tip = float(np.interp(u,contour[:,0],contour[:,1]))
            top = 1.95 - .014 * (math.sin(3*math.pi*u)**2)
            height = top + (tip-top)*v
            width = (u-.5)*(1-.033*v) + .012*v*v*math.sin(5*u+v)
            fold = (.012+.018*v) * math.sin(6*math.pi*u + .55*v)
            fold += .014 * math.sin(2*math.pi*u-.6*v) + .008*v*v*math.sin(13*u-2*v)
            positions.append((fold,height,width))
            uvs.append((u,1-height/1.95))
    for j in range(rows):
        for i in range(cols):
            a=j*(cols+1)+i; b=a+1; c=a+cols+1; d=c+1
            # Two small through-tears in the fabric, separate from the silhouette.
            if (i,j) in ((3,13),(4,14),(34,15),(34,16)):
                indices.extend((a,b,c) if i<10 else (b,d,c)); continue
            indices.extend((a,b,c,b,d,c))
    p=np.array(positions,dtype='<f4'); triangles=np.array(indices).reshape(-1,3)
    normals=np.zeros_like(p)
    for a,b,c in triangles:
        n=np.cross(p[b]-p[a],p[c]-p[a]); normals[[a,b,c]]+=n
    normals /= np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-9)
    data=bytearray(); views=[]; accessors=[]
    def emit(array, ctype, kind, count, target, low=None, high=None):
        data.extend(b'\x00'*(-len(data)%4)); start=len(data); data.extend(array.tobytes())
        views.append(dict(buffer=0,byteOffset=start,byteLength=len(data)-start,target=target))
        acc=dict(bufferView=len(views)-1,componentType=ctype,type=kind,count=count)
        if low is not None: acc.update(min=low,max=high)
        accessors.append(acc); return len(accessors)-1
    pos=emit(p,5126,'VEC3',len(p),34962,p.min(axis=0).tolist(),p.max(axis=0).tolist())
    norm=emit(normals,5126,'VEC3',len(p),34962)
    uv=emit(np.array(uvs,dtype='<f4'),5126,'VEC2',len(p),34962)
    idx=emit(np.array(indices,dtype='<u2'),5123,'SCALAR',len(indices),34963)
    name='SM_TornStandard'
    doc=dict(asset=dict(version='2.0',generator='CastleDemo original court standard'),scene=0,
        scenes=[dict(nodes=[0])],nodes=[dict(mesh=0,name=name)],meshes=[dict(name=name,primitives=[dict(
        attributes=dict(POSITION=pos,NORMAL=norm,TEXCOORD_0=uv),indices=idx,mode=4)])],
        buffers=[dict(byteLength=len(data))],bufferViews=views,accessors=accessors)
    js=json.dumps(doc,separators=(',',':')).encode(); js+=b' '*(-len(js)%4)
    data.extend(b'\x00'*(-len(data)%4))
    out=bytearray(struct.pack('<4sII',b'glTF',2,28+len(js)+len(data)))
    out+=struct.pack('<I4s',len(js),b'JSON')+js
    out+=struct.pack('<I4s',len(data),b'BIN\x00')+data
    (TARGET/(name+'.glb')).write_bytes(out)
    print(f'{len(p)} vertices, {len(indices)//3} triangles')


def make_tapestry():
    # Emblem is drawn as original vector shapes, not sampled from a found crest.
    w,h=768,1536
    ink=Image.new('L',(w,h)); d=ImageDraw.Draw(ink)
    def xy(points): return [(round(x*w),round(y*h)) for x,y in points]
    def line(points,fill=225,width=4): d.line(xy(points),fill=fill,width=width,joint='curve')
    def poly(points,fill=225): d.polygon(xy(points),fill=fill)
    def ellipse(x0,y0,x1,y1,fill): d.ellipse(xy([(x0,y0),(x1,y1)]),fill=fill)
    # Selvedges, broken diamonds and repeating angular embroidery under the hem.
    for x in (.055,.945):
        line([(x,.02),(x,.99)],180,4)
        for k in range(16):
            y=.09+k*.054
            line([(x,y-.010),(x+.012,y),(x,y+.010),(x-.012,y),(x,y-.010)],158,2)
    for y in (.030,.065): line([(.035,y),(.965,y)],202,5)
    for k in range(15):
        x=.068+k*.061
        line([(x-.022,.048),(x,.038),(x+.022,.048),(x,.057),(x-.022,.048)],186,3)
    # Gothic pointed niche with a quatrefoil finial and a split shield below.
    outline=[(.50,.128),(.42,.160),(.32,.192),(.23,.244),(.207,.280),
             (.207,.405),(.23,.442),(.31,.478),(.414,.495),(.50,.525),
             (.586,.495),(.69,.478),(.77,.442),(.793,.405),(.793,.280),
             (.77,.244),(.68,.192),(.58,.160),(.50,.128)]
    line(outline,221,10)
    line([(.5+(x-.5)*.934,.324+(y-.324)*.945) for x,y in outline],154,3)
    for sign in (-1,1):
        def mirror(points): return [(.5+sign*x,y) for x,y in points]
        for y in (.299,.385):
            poly(mirror([(.324,y),(.347,y-.009),(.370,y),(.347,y+.009)]),164)
        # Crossed long brushes: broad bristles and clear handle points at a distance.
        poly(mirror([(.125,.247),(.116,.265),(-.153,.444),(-.170,.452),(-.156,.433),(.093,.258)]),217)
        poly(mirror([(.100,.261),(.092,.247),(.104,.236),(.130,.224),
            (.155,.208),(.153,.229),(.146,.247),(.132,.257),(.115,.263)]),245)
        line(mirror([(.097,.260),(.117,.269)]),99,3)
    # Split halo around the sun-like painter's mark.
    ellipse(.455,.175,.545,.219,221);ellipse(.464,.180,.536,.215,0)
    for x,y in [(.50,.173),(.437,.194),(.563,.194)]:
        poly([(x,y-.008),(x+.008,y),(x,y+.008),(x-.008,y)],213)
    for x in (.298,.353,.647,.702):
        y=.548+abs(x-.5)*.11
        line([(x,y),(x,.672-abs(x-.5)*.15)],169,3)
        poly([(x,.688-abs(x-.5)*.15),(x-.009,.674-abs(x-.5)*.15),
              (x,.665-abs(x-.5)*.15),(x+.009,.674-abs(x-.5)*.15)],185)
    # Deterministic fibre variation, oxidised thread, wear and soft watermarks.
    rng=np.random.default_rng(1830)
    def clouds(a,b):
        arr=(rng.random((b,a))*255).astype(np.uint8)
        return np.asarray(Image.fromarray(arr).resize((w,h), Image.Resampling.BICUBIC),dtype=np.float32)/255
    y,x=np.mgrid[0:h,0:w]; u=x/w; v=y/h
    blot=clouds(12,18); micro=rng.normal(0,1,(h,w)).astype(np.float32)
    wear=clouds(55,110); warp=np.sin(2*np.pi*x/3.7); weft=np.sin(2*np.pi*y/4.2)
    weave=.9*warp+1.1*weft+1.0*micro
    jac=(np.cos(37*np.pi*u+11*np.pi*v)*np.cos(37*np.pi*u-11*np.pi*v)>.84)
    velvet=np.array([102,36,55])+(16*(blot[...,None]-.45)+weave[...,None]+jac[...,None]*2)
    # An old facing and oxidation along edges, without pre-baked lighting or folds.
    edge=np.clip((.085-np.minimum(u,1-u))/.085,0,1)
    velvet=velvet*(1-.23*edge[...,None]) + np.array([10,9,7])*np.maximum(0,v-.59)[...,None]
    motif=np.asarray(ink,dtype=np.float32)/255
    coverage=np.clip(.72+.26*wear + .09*micro,0,1)
    coverage*=np.where(wear>.81,.36,1.0)
    gold=np.array([181,148,92])+14*(blot[...,None]-.5)+weave[...,None]*1.25
    a=(motif*coverage)[...,None]; pixels=velvet*(1-a)+gold*a
    img=Image.fromarray(pixels.clip(0,255).astype(np.uint8)).resize((512,1024),Image.Resampling.LANCZOS)
    img.save(TARGET/'T_TornStandardWeave.png')


if __name__=='__main__':
    make_mesh(); make_tapestry()
