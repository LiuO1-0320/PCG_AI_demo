"""Owns one temporary editor; stops only its own child PID on an explicit stop file."""
import json
import socket
import subprocess
import time
from mcp_client import Client, ROOT, HERE, SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS, MAP

report = HERE / "editor-session.json"
stop = HERE / "stop-editor.json"
if report.exists() or stop.exists():
    raise RuntimeError("Session evidence exists; refusing overwrite")
with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", 8001)) == 0:
        raise RuntimeError("Port already occupied")
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = ["D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(ROOT / "testfordemo.uproject"),
    "/Game/Roguelite/Maps/L_Roguelite_Dev", "-unattended", "-nosplash", "-nosound", "-nop4",
    "-ModelContextProtocolStartServer", "-ModelContextProtocolPort=8001",
    "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1", f"-abslog={HERE / 'p03a-editor.log'}"]
process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("Owned temporary editor PID=" + str(process.pid), flush=True)
evidence = {"pid": process.pid, "command": command}
try:
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Editor startup exited: " + str(process.returncode))
        try:
            with socket.create_connection(("127.0.0.1", 8001), timeout=1):
                break
        except OSError:
            time.sleep(2)
    else:
        raise TimeoutError("Editor MCP startup")
    c = Client()
    evidence["toolsets"] = c.rpc("tools/call", {"name": "list_toolsets", "arguments": {}})
    evidence["schemas"] = {ts: c.rpc("tools/call", {"name": "describe_toolset", "arguments": {"toolset_name": ts}})
        for ts in (SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS)}
    if c.call(SCENE, "get_current_level") != "/Game/Roguelite/Maps/L_Roguelite_Dev":
        raise RuntimeError("Unexpected source map")
    if c.call(ASSET, "is_dirty", {"asset_path": "/Game/Roguelite/Maps/L_Roguelite_Dev"}):
        raise RuntimeError("Source map dirty; not copying")
    if c.call(ASSET, "exists", {"path": MAP}):
        raise RuntimeError("New assembly map already exists; refusing overwrite")
    if not c.call(ASSET, "duplicate", {"path": "/Game/Roguelite/Maps/L_Roguelite_Dev", "new_path": MAP}):
        raise RuntimeError("Map duplication failed")
    c.call(ASSET, "save_assets", {"asset_paths": [MAP]})
    c.call(SCENE, "load_level", {"level_path": MAP})
    if c.call(SCENE, "get_current_level") != MAP:
        raise RuntimeError("New assembly map not loaded")
    actors = {}
    for name, cls, x, y in (("P03A_RoomA", "RogueliteParametricRoom", 50000, 0),
        ("P03A_RoomB", "RogueliteParametricRoom", 55000, 5000),
        ("P03A_Assembly", "RogueliteRoomAssembly", 48000, 0)):
        actors[name] = c.call(SCENE, "add_to_scene_from_class", {"actor_type": {"refPath": "/Script/RoguelitePrototype." + cls},
            "name": name, "xform": {"location": {"x": x, "y": y, "z": 0}}})
    c.set(actors["P03A_RoomA"], {"RoomLength": 2000, "RoomWidth": 2000, "RoomHeight": 400})
    c.set(actors["P03A_RoomB"], {"RoomLength": 3000, "RoomWidth": 2500, "RoomHeight": 500})
    for name in ("P03A_RoomA", "P03A_RoomB"):
        for side in ("North", "South", "East", "West"):
            c.door(actors[name], side, bEnabled=True, bAutoCenter=True, ManualOffset=0, DoorWidth=200, DoorHeight=250)
    c.set(actors["P03A_Assembly"], {"RoomA": actors["P03A_RoomA"], "RoomB": actors["P03A_RoomB"]})
    c.call(ASSET, "save_assets", {"asset_paths": [MAP]})
    evidence.update(actors=actors, calls=c.calls, ready=True)
    with report.open("x", encoding="utf-8") as file:
        json.dump(evidence, file, ensure_ascii=False, indent=2)
    print("P03A new map ready; editor held for sequential tests", flush=True)
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
    with (HERE / "editor-session-closed.json").open("x", encoding="utf-8") as file:
        json.dump({"pid": process.pid, "returncode": process.returncode, "only_owned_pid_closed": True}, file)
    print("Owned P03A editor closed", flush=True)
