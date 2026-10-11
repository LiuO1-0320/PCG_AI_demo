"""Final binary readback, boundary rejection, real local nav, PIE API failures and screenshots."""
import base64,json,re,time,traceback
from c2_mcp import Client,HERE,MAP,SCENE,ACTOR,APP,ASSET,SLATE,field,similar,own_map
report=HERE/'final-proof.json'
if report.exists():raise RuntimeError('Evidence exists')
c=Client();e={'checks':[],'cases':[],'images':[]}
def check(n,v,d=None):
    e['checks'].append({'name':n,'passed':bool(v),'detail':d});print(n+(': PASS' if v else ': FAIL'),flush=True)
    if not v:raise AssertionError(n+': '+str(d))
def action(text='Align Corridor Connection'):
    check('actual button '+text,c.button(assembly,text) is True)
    return c.props(assembly,['LastResult','Connections'])
def reject(n):
    tc=c.transform(corridor);tb=c.transform(b);ta=c.transform(a)
    ids=c.props(assembly,['Connections'])['Connections'];v=action();r=v['LastResult']
    check(n+' rejection/restoration',not field(r,'bSucceeded') and similar(tc,c.transform(corridor)) and similar(tb,c.transform(b)) and similar(ta,c.transform(a)),r)
    check(n+' raw reservation kept/fresh failure',len(v['Connections'])==1 and similar(field(ids[0],'Id'),field(v['Connections'][0],'Id')) and field(v['Connections'][0],'ValidationStatus')=='Failed',v)
    e['cases'].append({'name':n,'result':r})
try:
    own_map(c)
    a=c.find('P03C2_RoomA')[0];b=c.find('P03C2_RoomB')[0];corridor=c.find('P03C2_Corridor')[0];assembly=c.find('P03C2_Assembly')[0];rig=c.find('P03C2_TestRig')[0]
    e['after_restart']=action('Revalidate Connections')
    check('new binary preserves record and physics',field(e['after_restart']['LastResult'],'bSucceeded') and len(e['after_restart']['Connections'])==1,e['after_restart'])
    c.door(a,'North',bAutoCenter=False,ManualOffset=9999);reject('illegal room socket offset')
    check('illegal offset original retained',field(c.props(a,['NorthDoor'])['NorthDoor'],'ManualOffset')==9999)
    c.door(a,'North',bAutoCenter=False,ManualOffset=250)
    c.set(corridor,{'DoorWidth':600});reject('illegal corridor opening width')
    check('illegal corridor dimension original retained',c.props(corridor,['DoorWidth'])['DoorWidth']==600)
    c.set(corridor,{'DoorWidth':200})
    c.move(a,0,0,yaw=45);reject('illegal fixed room rotation');c.move(a,0,0)
    c.set(assembly,{'bCheckNavigation':False})
    c.set(corridor,{'CorridorWidth':200,'CorridorHeight':250})
    v=action();check('full inner opening boundary has real walls/ceiling/no blockage',field(v['LastResult'],'bSucceeded') and all(field(p,'bPassed') for p in field(v['LastResult'],'Probes')),v)
    c.set(corridor,{'CorridorWidth':500,'CorridorHeight':350})
    v=action();check('restore500x350 reuses one reservation',field(v['LastResult'],'bSucceeded') and len(v['Connections'])==1,v)
    check('no residual room/corridor components',len(c.call(ACTOR,'get_components',{'actor':a}))==18 and len(c.call(ACTOR,'get_components',{'actor':b}))==18 and len(c.call(ACTOR,'get_components',{'actor':corridor}))==13)
    c.set(assembly,{'bCheckNavigation':True})
    check('explicit local navigation rebuild',c.button(rig,'Build Local Navigation') is True)
    e['nav_build']=c.props(rig,['NavigationBuildEvidence'])
    for attempt in range(8):
        time.sleep(2);v=action('Revalidate Connections');r=v['LastResult']
        if field(r,'Navigation') in ('Passed','Failed'):break
    e['navigation']=r
    check('final true NavigationSystem path',field(r,'bSucceeded') and field(r,'Navigation')=='Passed' and not field(r,'bNavigationPartial') and len(field(r,'NavigationPathPoints'))>=2,r)
    e['final_connection']=v
    check('save final C2 map',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    c.call(APP,'StartPIE',{'options':{'bSimulate':False,'playMode':'PlayMode_InViewPort','warmupSeconds':1,
        'startTransform':{'location':{'x':0,'y':0,'z':150},'rotation':{'pitch':0,'yaw':0,'roll':0},'scale':{'x':1,'y':1,'z':1}}}})
    check('final PIE actually running',c.call(APP,'IsPIERunning') is True)
    time.sleep(1)
    logs=(HERE/'p03c2-editor-final.log').read_text(encoding='utf-8',errors='replace')
    e['runtime_log']=[l for l in logs.splitlines() if 'P03C2_RUNTIME_' in l or 'ROGUELITE_DEV_START' in l]
    check('native invalid actor/endpoint/side/id cases rejected',any('InvalidNativeInputsRejected=1' in l for l in e['runtime_log']),e['runtime_log'])
    check('native runtime static guard and collision success',any('P03C2_RUNTIME_RESULT Passed=1' in l for l in e['runtime_log']),e['runtime_log'])
    check('no legacy actors',any('ROGUELITE_DEV_START' in l and 'LegacyActorCount=0' in l for l in e['runtime_log']))
    c.call(APP,'StopPIE');check('final PIE stopped',c.call(APP,'IsPIERunning') is False)
    annotations={'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':{'refPath':'/Script/Engine.Actor'},'maxLabels':0}
    for name,loc,pitch,yaw in (('assembly-overview',(1000,-6500,4200),-38,65),('seam-A-C',(800,250,130),0,0),('corridor-to-B',(1500,250,130),0,0)):
        value=c.call(APP,'CaptureViewport',{'captureTransform':{'location':dict(zip(('x','y','z'),loc)),'rotation':{'pitch':pitch,'yaw':yaw,'roll':0},'scale':{'x':1,'y':1,'z':1}},'annotations':annotations,'bShowUI':False})
        data=base64.b64decode(value['image']['data'],validate=True);check('actual PNG '+name,data.startswith(b'\x89PNG\r\n\x1a\n'))
        path=HERE/(name+'.png');path.write_bytes(data);e['images'].append({'path':str(path),'bytes':len(data)})
    c.call(APP,'SelectActors',{'actors':[assembly]});time.sleep(.3)
    s=c.call(SLATE,'Snapshot',{'ref':'','maxDepth':1,'bIncludeSourceLocations':False});root=re.search(r'window.*\[ref=([^\]]+)\]',s).group(1)
    value=c.call(SLATE,'Screenshot',{'ref':root});data=base64.b64decode(value['data'],validate=True)
    path=HERE/'assembly-details.png';path.write_bytes(data);e['images'].append({'path':str(path),'bytes':len(data)})
    check('C2 remains saved/PIE stopped',not c.call(ASSET,'is_dirty',{'asset_path':MAP}) and not c.call(APP,'IsPIERunning'))
    e['completed']=True
except Exception:
    e['error']=traceback.format_exc();raise
finally:
    e['calls']=c.calls
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print(json.dumps({'completed':e.get('completed',False),'checks':len(e['checks']),'calls':len(c.calls)}))
