"""Resume after verified core cases; C1 controls were initially below the visible Details area."""
import json,traceback
from c2_mcp import Client,HERE,MAP,ACTOR,SCENE,ASSET,field,similar,own_map
c=Client();e={'checks':[]};report=HERE/'validation-tail.json'
if report.exists():raise RuntimeError('Evidence exists')
def check(n,v,d=None):
    e['checks'].append({'name':n,'passed':bool(v),'detail':d});print(n+(': PASS' if v else ': FAIL'),flush=True)
    if not v:raise AssertionError(n+': '+str(d))
def action(text):
    check('button '+text,c.button(assembly,text) is True)
    return c.props(assembly,['LastResult','Connections'])
def ray(s,t):return c.call(SCENE,'trace_world',{'start':dict(zip(('x','y','z'),s)),'end':dict(zip(('x','y','z'),t))})
try:
    own_map(c)
    a=c.find('P03C2_RoomA')[0];b=c.find('P03C2_RoomB')[0];corridor=c.find('P03C2_Corridor')[0];assembly=c.find('P03C2_Assembly')[0]
    other=c.find('P03C2_OtherCorridor')[0]
    check('resume fixed A at zero',similar(field(c.transform(a),'location'),{'x':0,'y':0,'z':0}))
    check('resume main reservation one',len(c.props(assembly,['Connections'])['Connections'])==1)
    for values,label in (({'CorridorLength':1500},'length'),({'CorridorWidth':600},'width'),({'CorridorHeight':450},'height')):
        c.set(other,values);p=c.props(other,['StartSocket','EndSocket','CorridorLength','ValidationWarnings'])
        check('C1 '+label+' sockets/13 components',not p['ValidationWarnings'] and len(c.call(ACTOR,'get_components',{'actor':other}))==13 and abs(field(field(p['EndSocket'],'DoorSocket'),'LocalPosition')['x']-p['CorridorLength']/2)<.001,p)
        check('C1 '+label+' real Check Passage button',c.button(other,'Check Passage') is True)
        check('C1 '+label+' native Pawn sweep',c.props(other,['bPassageTestPassed'])['bPassageTestPassed'])
    check('C1 floor collision',abs(ray((-25000,10000,100),(-25000,10000,-100))-100)<.1)
    check('C1 side collision',abs(ray((-25000,10000,100),(-25000,10400,100))-300)<.1)
    check('C1 ceiling collision',abs(ray((-25000,10000,100),(-25000,10000,500))-350)<.1)
    r=action('Align Corridor Connection')['LastResult']
    check('final main 59 collision probes',field(r,'bSucceeded') and len(field(r,'Probes'))==59 and all(field(p,'bPassed') for p in field(r,'Probes')),r)
    tc=c.transform(corridor);tb=c.transform(b)
    action('Disconnect Corridor Connection');state=action('Disconnect Corridor Connection')
    check('idempotent disconnect and no actor movement',not state['Connections'] and similar(tc,c.transform(corridor)) and similar(tb,c.transform(b)))
    p=c.props(corridor,['StartSocket','EndSocket']);check('derived free endpoint reservation',all(not field(v,'bConnected') for v in p.values()),p)
    state=action('Align Corridor Connection');check('reconnect successful',field(state['LastResult'],'bSucceeded') and len(state['Connections'])==1,state)
    check('only C2 map saved',c.call(ASSET,'save_assets',{'asset_paths':[MAP]}) is True)
    check('C2 map clean',c.call(ASSET,'is_dirty',{'asset_path':MAP}) is False)
    e['final']=state;e['completed']=True
except Exception:
    e['error']=traceback.format_exc();raise
finally:
    e['calls']=c.calls
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print(json.dumps({'completed':e.get('completed',False),'checks':len(e['checks']),'calls':len(c.calls)}))
