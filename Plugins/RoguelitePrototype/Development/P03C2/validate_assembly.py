"""C2-owned fixtures only; does not call historical map-reset scripts."""
import argparse,json,traceback
from c2_mcp import Client,HERE,MAP,SCENE,ACTOR,ASSET,field,similar,own_map
parser=argparse.ArgumentParser();parser.add_argument('--run-name',default='validation');args=parser.parse_args()
report=HERE/(args.run_name+'.json')
if report.exists() or not args.run_name.replace('-','').isalnum():raise RuntimeError('Existing/invalid evidence')
c=Client();e={'task':'P03-C2','checks':[],'cases':[]}
def check(name,condition,detail=None):
    e['checks'].append({'name':name,'passed':bool(condition),'detail':detail});print(name+(': PASS' if condition else ': FAIL'),flush=True)
    if not condition:raise AssertionError(name+': '+str(detail))
def own(name,cls,x,y):
    found=c.find(name);check('unique '+name,len(found)<=1)
    return found[0] if found else c.call(SCENE,'add_to_scene_from_class',{'actor_type':{'refPath':cls},'name':name,
        'xform':{'location':{'x':x,'y':y,'z':0},'rotation':{'pitch':0,'yaw':0,'roll':0},'scale':{'x':1,'y':1,'z':1}}})
def records(reg=None):return c.props(reg or assembly,['Connections'])['Connections']
def signature(reg=None):
    return [{k:field(v,k) for k in ('Id','RoomA','SocketA','RoomB','SocketB','Corridor','EndpointA','EndpointB','Mode','bActive')} for v in records(reg)]
def act(text='Align Corridor Connection',reg=None):
    obj=reg or assembly;check('actual button '+text,c.button(obj,text) is True)
    return c.props(obj,['LastResult'])['LastResult']
def setup(reg=None,aa=None,bb=None,cc=None,sa='North',sb='South',end='Start',mode='StraightCorridor'):
    c.set(reg or assembly,{'RoomA':aa or a,'RoomB':bb or b,'Corridor':cc or corridor,'SocketA':sa,'SocketB':sb,
        'CorridorEndpointA':end,'ConnectionMode':mode,'bCheckNavigation':False})
def success(name,reg=None,aa=None,bb=None,cc=None):
    aa=aa or a;bb=bb or b;cc=cc or corridor;reg=reg or assembly
    fixed=c.transform(aa);r=act(reg=reg)
    check(name+' geometry/capsule',all(field(r,k) for k in ('bSucceeded','bGeometryPassed','bCapsulePassed')),r)
    check(name+' both thresholds/normals',field(r,'SocketDistance')<=.1 and field(r,'SecondSocketDistance')<=.1
        and field(r,'NormalDot')<-.99999 and field(r,'SecondNormalDot')<-.99999,r)
    check(name+' fixed A unchanged',similar(fixed,c.transform(aa)))
    check(name+' one logical reservation',len(records(reg))==1,records(reg))
    check(name+' all actual probes passed',len(field(r,'Probes'))>=59 and all(field(p,'bPassed') for p in field(r,'Probes')),r)
    check(name+' Scale1',all(similar(field(c.transform(v),'scale'),{'x':1,'y':1,'z':1}) for v in (aa,bb,cc)))
    e['cases'].append({'name':name,'result':r,'record':records(reg),'transform_c':c.transform(cc),'transform_b':c.transform(bb)})
    return r
def failure(name,reg=None,text='Align Corridor Connection'):
    reg=reg or assembly;before_c=c.transform(corridor);before_b=c.transform(b);before_a=c.transform(a)
    topology=signature();other=signature(reg);r=act(text,reg)
    check(name+' rejected with fresh diagnostic',not field(r,'bSucceeded') and bool(field(r,'Message')),r)
    check(name+' both transforms/A restored',similar(before_c,c.transform(corridor)) and similar(before_b,c.transform(b)) and similar(before_a,c.transform(a)))
    check(name+' authoritative topology unchanged',similar(topology,signature()) and similar(other,signature(reg)),records())
    check(name+' rollback flags',field(r,'bTargetRestoredOnFailure') and field(r,'bCorridorRestoredOnFailure') and field(r,'bReservationsUnchangedOnFailure'),r)
    e['cases'].append({'name':name,'result':r,'topology':signature(),'before_c':before_c,'before_b':before_b})
    return r
def door(room,side,enabled=True,offset=0):c.door(room,side,bEnabled=enabled,bAutoCenter=offset==0,ManualOffset=offset,DoorWidth=200,DoorHeight=250)
def ray(start,end):return c.call(SCENE,'trace_world',{'start':dict(zip(('x','y','z'),start)),'end':dict(zip(('x','y','z'),end))})
try:
    own_map(c)
    a=c.find('P03C2_RoomA')[0];b=c.find('P03C2_RoomB')[0];corridor=c.find('P03C2_Corridor')[0];assembly=c.find('P03C2_Assembly')[0]
    setup();success('fresh different-size centered connection')
    original_id=field(records()[0],'Id')
    success('repeat align');check('repeat reuses GUID',similar(original_id,field(records()[0],'Id')))
    socket=c.props(corridor,['StartSocket','EndSocket'])
    check('corridor derived reservation cache',all(field(v,'bConnected') and similar(field(v,'ConnectionId'),original_id) for v in socket.values()),socket)

    door(a,'North',offset=250);door(b,'South',offset=-300)
    c.set(a,{'WallThickness':40});c.set(b,{'WallThickness':35,'FloorThickness':35});c.set(corridor,{'WallThickness':25,'FloorThickness':30})
    success('nonzero offsets and different thicknesses')
    check('offset positions not room centers',field(field(c.transform(corridor),'location'),'y')==250 and field(field(c.transform(b),'location'),'y')==550)
    c.set(corridor,{'CorridorLength':2500})
    r=act('Revalidate Connections');check('edited length marks invalid without movement',not field(r,'bSucceeded') and not field(records()[0],'bGeometryValid'),r)
    success('length2500 re-align');check('length re-align reuses GUID',similar(original_id,field(records()[0],'Id')))
    c.set(corridor,{'CorridorLength':2000});success('length2000 restored')

    for yaw,sb in ((90,'East'),(180,'North'),(270,'West')):
        act('Disconnect Corridor Connection')
        c.move(a,0,0,yaw=yaw);door(b,sb,offset=150);setup(sb=sb)
        success('quarter turn '+str(yaw)+' socket '+sb)
    act('Disconnect Corridor Connection')
    c.move(a,0,0);setup();success('yaw0 restored')
    setup(end='End');success('End-to-A reversed endpoint mapping')
    check('mapping recorded',field(records()[0],'EndpointA')=='End' and field(records()[0],'EndpointB')=='Start')
    setup();success('Start-to-A restored')
    # Turn off unused test openings before the later nav build.
    for side in ('North','East','West'):door(b,side,enabled=False)

    door(a,'North',enabled=False);failure('closed socket');door(a,'North',offset=250)
    c.door(b,'South',DoorWidth=220);failure('incompatible opening');door(b,'South',offset=-300)
    c.move(b,*[field(field(c.transform(b),'location'),axis) for axis in ('x','y')],scale=2);failure('illegal RoomB scale')
    # Restore a legal current transform; explicit Align will recompute placement.
    c.move(b,9000,5000);success('restore legal RoomB scale')
    c.move(corridor,9000,7000,yaw=45);failure('illegal corridor rotation');c.move(corridor,9000,7000);success('restore legal corridor rotation')

    obstacle=own('P03C2_OccupancyRoom','/Script/RoguelitePrototype.RogueliteParametricRoom',20000,20000)
    c.set(obstacle,{'RoomLength':200,'RoomWidth':200,'RoomHeight':300})
    loc=field(c.transform(corridor),'location');c.move(obstacle,loc['x'],loc['y'])
    failure('parameter room overlap');c.move(obstacle,20000,20000);success('overlap removed')

    cube=c.call(SCENE,'add_to_scene_from_asset',{'asset_path':'/Engine/BasicShapes/Cube.Cube','name':'P03C2_Blocker',
        'xform':{'location':{'x':2020,'y':270,'z':100},'rotation':{'pitch':0,'yaw':0,'roll':0},'scale':{'x':.1,'y':.1,'z':.1}}})
    check('owned blocker created',cube is not None)
    center=field(c.transform(corridor),'location');c.move(cube,center['x'],center['y']+20,100,scale=.1)
    check('centerline ray misses off-center blocker',ray((900,250,125),(3200,250,125)) is None)
    c.move(corridor,15000,0);c.move(b,20000,5000)
    r=failure('post-move collision rollback')
    check('actual failed Pawn sweep recorded',any('PawnCapsule' in field(p,'Name') and not field(p,'bPassed') for p in field(r,'Probes')),r)
    c.move(cube,25000,25000,100,scale=.1);success('blocked path recovered')
    before_poi=c.transform(cube);c.set(b,{'ManualPOIActors':[cube]});failure('manual POI move protection')
    check('manual POI transform unchanged',similar(before_poi,c.transform(cube)));c.set(b,{'ManualPOIActors':[]})

    other=own('P03C2_OtherAssembly','/Script/RoguelitePrototype.RogueliteRoomAssembly',25000,0)
    other_room=own('P03C2_OtherRoom','/Script/RoguelitePrototype.RogueliteParametricRoom',25000,10000)
    door(other_room,'South');setup(other,bb=other_room,mode='Direct')
    failure('Direct cannot steal corridor room socket',other,text='Align Selected Rooms')
    door(b,'North');setup(other,aa=b,bb=other_room,mode='Direct')
    r=act('Align Selected Rooms',other);check('new Direct branch geometry/capsule regression',field(r,'bSucceeded') and field(r,'bCapsulePassed'),r)
    failure('refuse moving RoomB connected subgraph')
    act('Disconnect Selected Rooms',other);check('branch reservation released',not records(other));door(b,'North',enabled=False);c.move(other_room,25000,10000)
    success('main connection after branch release')
    other_c=own('P03C2_OtherCorridor','/Script/RoguelitePrototype.RogueliteParametricCorridor',-25000,10000)
    door(other_room,'North');setup(other,aa=other_room,bb=obstacle,cc=corridor)
    door(obstacle,'South');failure('corridor reservation cannot be stolen',other)

    # C1 regression on a fresh, unconnected actor, not the accepted old C1 map.
    c.move(other_c,-25000,10000)
    for values,label in (({'CorridorLength':1500},'length'),({'CorridorWidth':600},'width'),({'CorridorHeight':450},'height')):
        c.set(other_c,values);p=c.props(other_c,['StartSocket','EndSocket','CorridorLength','ValidationWarnings'])
        check('C1 '+label+' sockets/13 components',not p['ValidationWarnings'] and len(c.call(ACTOR,'get_components',{'actor':other_c}))==13
            and abs(field(field(p['EndSocket'],'DoorSocket'),'LocalPosition')['x']-p['CorridorLength']/2)<.001,p)
        check('C1 '+label+' actual Check Passage',c.button(other_c,'Check Passage') is True)
        check('C1 '+label+' native sweep',c.props(other_c,['bPassageTestPassed'])['bPassageTestPassed'])
    check('C1 floor collision',abs(ray((-25000,10000,100),(-25000,10000,-100))-100)<.1)
    check('C1 side collision',abs(ray((-25000,10000,100),(-25000,10400,100))-300)<.1)
    check('C1 ceiling collision',abs(ray((-25000,10000,100),(-25000,10000,500))-350)<.1)

    setup();success('final main connected state')
    before_c=c.transform(corridor);before_b=c.transform(b)
    act('Disconnect Corridor Connection');act('Disconnect Corridor Connection')
    check('disconnect idempotent and no actor moves',not records() and similar(before_c,c.transform(corridor)) and similar(before_b,c.transform(b)))
    p=c.props(corridor,['StartSocket','EndSocket']);check('derived free socket state',all(not field(v,'bConnected') for v in p.values()),p)
    success('reconnect after release')
    check('final save only C2 map',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    check('final map clean',c.call(ASSET,'is_dirty',{'asset_path':MAP}) is False)
    e['final']={'connections':records(),'a':c.transform(a),'corridor':c.transform(corridor),'b':c.transform(b)}
    e['completed']=True
except Exception:
    e['error']=traceback.format_exc();raise
finally:
    e['calls']=c.calls
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print(json.dumps({'completed':e.get('completed',False),'checks':len(e['checks']),'passed':sum(x['passed'] for x in e['checks']),'cases':len(e['cases']),'calls':len(c.calls)}))
