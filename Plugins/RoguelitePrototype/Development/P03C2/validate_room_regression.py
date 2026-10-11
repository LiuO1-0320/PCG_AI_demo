"""P01/P02 regression on new C2 fixtures, leaving accepted maps untouched."""
import json,traceback
from c2_mcp import Client,HERE,MAP,SCENE,ACTOR,ASSET,similar,field,own_map
report=HERE/'room-regression.json'
if report.exists():raise RuntimeError('Evidence exists')
c=Client();e={'checks':[]}
def check(n,v,d=None):
    e['checks'].append({'name':n,'passed':bool(v),'detail':d});print(n+(': PASS' if v else ': FAIL'),flush=True)
    if not v:raise AssertionError(n+': '+str(d))
def ray(s,t):return c.call(SCENE,'trace_world',{'start':dict(zip(('x','y','z'),s)),'end':dict(zip(('x','y','z'),t))})
def close_all():
    for side in ('North','South','East','West'):c.door(room,side,bEnabled=False,bAutoCenter=True,ManualOffset=0,DoorWidth=200,DoorHeight=250)
try:
    own_map(c);check('fresh fixture absent',not c.find('P03C2_RegressionRoom'))
    room=c.call(SCENE,'add_to_scene_from_class',{'actor_type':{'refPath':'/Script/RoguelitePrototype.RogueliteParametricRoom'},'name':'P03C2_RegressionRoom',
        'xform':{'location':{'x':35000,'y':0,'z':0},'rotation':{'pitch':0,'yaw':0,'roll':0},'scale':{'x':1,'y':1,'z':1}}})
    close_all()
    poi=c.call(SCENE,'add_to_scene_from_asset',{'asset_path':'/Engine/BasicShapes/Cube.Cube','name':'P03C2_RegressionPOI',
        'xform':{'location':{'x':35850,'y':100,'z':100},'rotation':{'pitch':0,'yaw':25,'roll':0},'scale':{'x':1,'y':.8,'z':1.2}}})
    c.set(room,{'ManualPOIActors':[poi]});original=c.transform(poi)
    ids=c.call(ACTOR,'get_components',{'actor':room})
    check('initial 18 fixed room components',len(ids)==18)
    for length,width,height in ((2000,2000,400),(3000,2500,500),(3000,2500,650),(2000,2000,400)):
        c.set(room,{'RoomLength':length,'RoomWidth':width,'RoomHeight':height})
        check('P01 dimensions '+str((length,width,height))+' no component duplication',similar(ids,c.call(ACTOR,'get_components',{'actor':room})))
        check('P01 POI rigid transform unchanged',similar(original,c.transform(poi)))
        check('P01 east/west exterior collision',abs(ray((35000,0,100),(35000,width/2+100,100))-width/2)<.1)
        check('P01 north wall collision',abs(ray((35000,600,100),(35000+length/2+100,600,100))-length/2)<.1)
        check('P01 floor collision',abs(ray((35000,600,100),(35000,600,-100))-100)<.1)
    check('old enlarged wall collision removed',ray((36200,600,100),(36300,600,100)) is None)
    c.set(room,{'RoomLength':1000});warnings=c.props(room,['ValidationWarnings'])['ValidationWarnings']
    check('Bounds-based POI warning includes room/POI',any('P01_POI_OUT_OF_BOUNDS' in w and 'P03C2_RegressionPOI' in w and 'Room=' in w for w in warnings),warnings)
    check('shrink preserves POI transform',similar(original,c.transform(poi)))
    c.set(room,{'RoomLength':2000})
    for side,vec in (('North',(1,0)),('South',(-1,0)),('East',(0,1)),('West',(0,-1))):
        c.door(room,side,bEnabled=True)
        sx,sy=vec
        check('P02 '+side+' real opening',ray((35000+sx*950,sy*950,125),(35000+sx*1100,sy*1100,125)) is None)
        tangent=(-sy,sx)
        check('P02 '+side+' side wall collision',ray((35000+sx*950+tangent[0]*200,sy*950+tangent[1]*200,125),(35000+sx*1100+tangent[0]*200,sy*1100+tangent[1]*200,125)) is not None)
        check('P02 '+side+' lintel collision',ray((35000+sx*950,sy*950,300),(35000+sx*1100,sy*1100,300)) is not None)
        c.door(room,side,bEnabled=False)
        check('P02 '+side+' restored solid wall',ray((35000+sx*950,sy*950,125),(35000+sx*1100,sy*1100,125)) is not None)
    check('P02 final same 18 components',similar(ids,c.call(ACTOR,'get_components',{'actor':room})))
    check('P02 POI unchanged',similar(original,c.transform(poi)))
    check('save only new C2 map',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    e['completed']=True
except Exception:
    e['error']=traceback.format_exc();raise
finally:
    e['calls']=c.calls
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print(json.dumps({'completed':e.get('completed',False),'checks':len(e['checks']),'calls':len(c.calls)}))
