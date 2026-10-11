"""Own a single temporary C2 editor; never operate user editor processes."""
import argparse,json,socket,subprocess,time
from c2_mcp import Client,HERE,ROOT,MAP,SCENE,ACTOR,OBJECT,ASSET,APP,SLATE,LOGS
parser=argparse.ArgumentParser();parser.add_argument('--suffix',default='');args=parser.parse_args()
if args.suffix and not args.suffix.replace('-','').isalnum():raise RuntimeError('Invalid suffix')
suffix=('-'+args.suffix) if args.suffix else ''
report=HERE/('editor-session'+suffix+'.json');stop=HERE/('stop-editor'+suffix+'.json')
if report.exists() or stop.exists():raise RuntimeError('Session exists; inspect before retry')
with socket.socket() as probe:
    if probe.connect_ex(('127.0.0.1',8001))==0:raise RuntimeError('Port occupied')
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
command=['D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',str(ROOT/'testfordemo.uproject'),MAP,'-unattended','-nosplash','-nosound','-nop4','-ModelContextProtocolStartServer','-ModelContextProtocolPort=8001','-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1',f'-abslog={HERE / ("p03c2-editor"+suffix+".log")}']
p=subprocess.Popen(command,startupinfo=startup,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
print('Owned C2 editor PID='+str(p.pid),flush=True)
try:
    deadline=time.monotonic()+240
    while time.monotonic()<deadline:
        if p.poll() is not None:raise RuntimeError('Editor exited before MCP')
        try:
            with socket.create_connection(('127.0.0.1',8001),timeout=1):break
        except OSError:time.sleep(2)
    else:raise TimeoutError('MCP startup')
    c=Client()
    e={'pid':p.pid,'command':command,'toolsets':c.rpc('tools/call',{'name':'list_toolsets','arguments':{}}),'schemas':{ts:c.rpc('tools/call',{'name':'describe_toolset','arguments':{'toolset_name':ts}}) for ts in (SCENE,ACTOR,OBJECT,ASSET,APP,SLATE,LOGS)}}
    assert c.call(SCENE,'get_current_level')==MAP and not c.call(APP,'IsPIERunning')
    e.update(ready=True,calls=c.calls)
    with report.open('x',encoding='utf-8') as f:json.dump(e,f,ensure_ascii=False,indent=2)
    print('Official MCP schemas ready',flush=True)
    while not stop.exists():
        if p.poll() is not None:raise RuntimeError('Owned editor exited unexpectedly')
        time.sleep(1)
finally:
    if p.poll() is None:p.terminate();p.wait(timeout=45)
    with (HERE/('editor-closed'+suffix+'.json')).open('x',encoding='utf-8') as f:json.dump({'pid':p.pid,'returncode':p.returncode,'only_owned_pid_closed':True},f)
    print('Owned C2 editor closed',flush=True)
