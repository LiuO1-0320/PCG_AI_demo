"""Build actor-local C2 navigation, reload this map and run the opt-in native PIE probe."""
import argparse,json,time,traceback
from c2_mcp import Client,HERE,MAP,SCENE,APP,ASSET,field,similar,own_map
p=argparse.ArgumentParser();p.add_argument('--run-name',default='navigation-runtime');args=p.parse_args()
report=HERE/(args.run_name+'.json')
if report.exists() or not args.run_name.replace('-','').isalnum():raise RuntimeError('Existing/invalid evidence')
c=Client();e={'task':'P03-C2','checks':[]}
def check(name,value,detail=None):
    e['checks'].append({'name':name,'passed':bool(value),'detail':detail});print(name+(': PASS' if value else ': FAIL'),flush=True)
    if not value:raise AssertionError(name+': '+str(detail))
def lookup():
    global assembly,rig,a,b,corridor
    assembly=c.find('P03C2_Assembly')[0];rig=c.find('P03C2_TestRig')[0]
    a=c.find('P03C2_RoomA')[0];b=c.find('P03C2_RoomB')[0];corridor=c.find('P03C2_Corridor')[0]
def revalidate():
    check('actual Revalidate button',c.button(assembly,'Revalidate Connections') is True)
    return c.props(assembly,['LastResult','Connections'])
try:
    own_map(c);lookup()
    c.set(assembly,{'bCheckNavigation':True})
    check('actual local Nav Build button',c.button(rig,'Build Local Navigation') is True)
    e['nav_build']=c.props(rig,['NavigationBuildEvidence'])
    e['nav_attempts']=[]
    for attempt in range(12):
        time.sleep(2)
        v=revalidate();r=v['LastResult'];e['nav_attempts'].append(v)
        print('Navigation attempt '+str(attempt)+' '+field(r,'Navigation')+' '+field(r,'NavigationMessage'),flush=True)
        if field(r,'Navigation') in ('Passed','Failed'):break
    e['navigation']=r
    check('geometry remains valid after nav build',field(r,'bGeometryPassed') and field(r,'bCapsulePassed'),r)
    check('navigation available or explicitly unavailable',field(r,'Navigation') in ('Passed','Unavailable'),r)
    if field(r,'Navigation')=='Passed':
        check('real complete NavigationSystem path',not field(r,'bNavigationPartial') and len(field(r,'NavigationPathPoints'))>=2,r)
    before={'id':field(v['Connections'][0],'Id'),'a':c.transform(a),'b':c.transform(b),'corridor':c.transform(corridor),
        'room_params':c.props(b,['RoomLength','RoomWidth','RoomHeight','SouthDoor']),
        'corridor_params':c.props(corridor,['CorridorLength','CorridorWidth','CorridorHeight','WallThickness','FloorThickness','DoorWidth','DoorHeight'])}
    check('save only owned map',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    check('clean before unload',c.call(ASSET,'is_dirty',{'asset_path':MAP}) is False)
    c.call(SCENE,'load_level',{'level_path':'/Engine/Maps/Templates/Template_Default'})
    check('C2 genuinely unloaded',c.call(SCENE,'get_current_level')!=MAP)
    c.call(SCENE,'load_level',{'level_path':MAP});own_map(c);lookup()
    state=c.props(assembly,['Connections','LastResult']);e['reload_before_revalidate']=state
    check('reservation survives reload',len(state['Connections'])==1 and similar(field(state['Connections'][0],'Id'),before['id']),state)
    check('reload invalidates cached success',field(state['Connections'][0],'ValidationStatus')=='NotChecked' and not field(state['Connections'][0],'bGeometryValid'),state)
    check('reload transforms preserved',all(similar(before[k],c.transform(obj)) for k,obj in (('a',a),('b',b),('corridor',corridor))))
    check('reload authoring parameters preserved',similar(before['room_params'],c.props(b,list(before['room_params']))) and similar(before['corridor_params'],c.props(corridor,list(before['corridor_params']))))
    reloaded=revalidate();e['reload_revalidation']=reloaded
    check('reload actual collision validation',field(reloaded['LastResult'],'bSucceeded') and field(reloaded['LastResult'],'bCapsulePassed'),reloaded)
    e['dependencies']=c.call(ASSET,'get_dependencies',{'asset_path':MAP})
    check('no old Demo/Gameplay asset dependency',not any('/Game/Demo' in str(d) or 'CastleDemo' in str(d) for d in e['dependencies']),e['dependencies'])
    e['pie_start_return']=c.call(APP,'StartPIE',{'options':{'bSimulate':False,'playMode':'PlayMode_InViewPort','warmupSeconds':1,
        'startTransform':{'location':{'x':0,'y':0,'z':150},'rotation':{'pitch':0,'yaw':0,'roll':0},'scale':{'x':1,'y':1,'z':1}}}})
    check('real PIE running',c.call(APP,'IsPIERunning') is True)
    time.sleep(1)
    logs=(HERE/'p03c2-editor.log').read_text(encoding='utf-8',errors='replace')
    e['runtime_log']=[line for line in logs.splitlines() if 'P03C2_RUNTIME_' in line or 'ROGUELITE_DEV_START' in line]
    check('native Runtime deferred-spawn/sweep/guard passed',any('P03C2_RUNTIME_RESULT Passed=1' in line for line in e['runtime_log']),e['runtime_log'])
    check('independent GameMode no legacy actors',any('ROGUELITE_DEV_START' in line and 'LegacyActorCount=0' in line for line in e['runtime_log']),e['runtime_log'])
    e['pie_stop_return']=c.call(APP,'StopPIE')
    check('real PIE stopped',c.call(APP,'IsPIERunning') is False)
    own_map(c)
    check('only C2 final save',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    check('C2 final clean',c.call(ASSET,'is_dirty',{'asset_path':MAP}) is False)
    e['completed']=True
except Exception:
    e['error']=traceback.format_exc();raise
finally:
    e['calls']=c.calls
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print(json.dumps({'completed':e.get('completed',False),'checks':len(e['checks']),'calls':len(c.calls)}))
