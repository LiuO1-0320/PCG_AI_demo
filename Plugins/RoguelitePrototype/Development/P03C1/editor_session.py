"""Own one hidden temporary editor. Copy/save only a NEW C1 map; never overwrite old maps."""
import json
import socket
import subprocess
import time
from corridor_mcp import Client, HERE, ROOT, MAP, SOURCE_MAP, SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS

report = HERE / "editor-session.json"
stop = HERE / "stop-editor.json"
if report.exists() or stop.exists() or (ROOT / "Content/Roguelite/Maps/L_Roguelite_Corridor_Dev.umap").exists():
    raise RuntimeError("Existing C1 session/map: inspect instead of restarting or overwriting")
with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", 8001)) == 0:
        raise RuntimeError("Port occupied")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = ["D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(ROOT / "testfordemo.uproject"), SOURCE_MAP,
    "-unattended", "-nosplash", "-nosound", "-nop4", "-ModelContextProtocolStartServer", "-ModelContextProtocolPort=8001",
    "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1", f"-abslog={HERE / 'p03c1-editor.log'}"]
process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("Owned C1 editor PID=" + str(process.pid), flush=True)
try:
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Editor exited during startup")
        try:
            with socket.create_connection(("127.0.0.1", 8001), timeout=1):
                break
        except OSError:
            time.sleep(2)
    else:
        raise TimeoutError("Editor MCP startup")
    c = Client()
    evidence = {"pid": process.pid, "command": command,
        "toolsets": c.rpc("tools/call", {"name": "list_toolsets", "arguments": {}}),
        "schemas": {ts: c.rpc("tools/call", {"name": "describe_toolset", "arguments": {"toolset_name": ts}})
            for ts in (SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS)}}
    if c.call(SCENE, "get_current_level") != SOURCE_MAP or c.call(APP, "IsPIERunning"):
        raise RuntimeError("Unexpected source state")
    if c.call(ASSET, "is_dirty", {"asset_path": SOURCE_MAP}):
        raise RuntimeError("Source dirty; refuse duplication")
    evidence.update(calls=c.calls, ready=True)
    with report.open("x", encoding="utf-8") as f:
        json.dump(evidence, f, ensure_ascii=False, indent=2)
    print("C1 editor read-only discovery ready; inspect schemas before setup", flush=True)
    while not stop.exists():
        if process.poll() is not None:
            raise RuntimeError("Owned editor unexpectedly exited")
        time.sleep(1)
finally:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=45)
        except subprocess.TimeoutExpired:
            raise RuntimeError("Owned editor did not close; no other process terminated")
    with (HERE / "editor-closed.json").open("x", encoding="utf-8") as f:
        json.dump({"pid": process.pid, "returncode": process.returncode, "only_owned_pid_closed": True}, f)
    print("Owned C1 editor closed", flush=True)
