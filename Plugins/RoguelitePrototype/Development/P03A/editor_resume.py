"""Resume only this task's saved map after recompilation, owning a fresh editor PID."""
import json
import socket
import subprocess
import time
from mcp_client import Client, ROOT, HERE, MAP, SCENE, APP

report = HERE / "editor-ground-session.json"
stop = HERE / "stop-editor-ground.json"
if report.exists() or stop.exists() or not (HERE / "editor-session.json").exists():
    raise RuntimeError("Resume checkpoint invalid/already used")
with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", 8001)) == 0:
        raise RuntimeError("Port occupied")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = ["D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(ROOT / "testfordemo.uproject"), MAP,
    "-unattended", "-nosplash", "-nosound", "-nop4", "-ModelContextProtocolStartServer", "-ModelContextProtocolPort=8001",
    "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1", f"-abslog={HERE / 'p03a-editor-ground.log'}"]
process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("Owned ground-check editor PID=" + str(process.pid), flush=True)
try:
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Editor startup exited")
        try:
            with socket.create_connection(("127.0.0.1", 8001), timeout=1):
                break
        except OSError:
            time.sleep(2)
    else:
        raise TimeoutError("MCP startup")
    c = Client()
    if c.call(SCENE, "get_current_level") != MAP or c.call(APP, "IsPIERunning"):
        raise RuntimeError("Resume map incorrect")
    evidence = {"pid": process.pid, "command": command, "ready": True,
        "toolsets": c.rpc("tools/call", {"name": "list_toolsets", "arguments": {}}),
        "actors": {name: c.find(name) for name in ("P03A_RoomA", "P03A_RoomB", "P03A_Assembly")}, "calls": c.calls}
    if not all(len(actors) == 1 for actors in evidence["actors"].values()):
        raise RuntimeError("Owned fixtures missing/ambiguous")
    with report.open("x", encoding="utf-8") as file:
        json.dump(evidence, file, ensure_ascii=False, indent=2)
    print("Saved P03A map resumed; ready for ground support tests", flush=True)
    while not stop.exists():
        if process.poll() is not None:
            raise RuntimeError("Editor unexpectedly exited")
        time.sleep(1)
finally:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=30)
    with (HERE / "editor-ground-closed.json").open("x", encoding="utf-8") as file:
        json.dump({"pid": process.pid, "returncode": process.returncode, "only_owned_pid_closed": True}, file)
    print("Owned ground-check editor closed", flush=True)
