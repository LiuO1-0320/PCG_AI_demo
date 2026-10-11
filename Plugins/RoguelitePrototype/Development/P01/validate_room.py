"""P01 integration checks using discovered Epic MCP tools in a dedicated editor.

Only L_Roguelite_Dev is saved. Existing actors are preserved; retries reuse named
P01 actors rather than spawning duplicates. All tool calls are sequential.
"""
import argparse
import base64
import hashlib
import json
import math
import socket
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
EVIDENCE = Path(__file__).resolve().parent
MAP = "/Game/Roguelite/Maps/L_Roguelite_Dev"
ROOM_CLASS = {"refPath": "/Script/RoguelitePrototype.RogueliteParametricRoom"}
PORT = 8001
URL = f"http://127.0.0.1:{PORT}/mcp"
SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
APP = "EditorToolset.EditorAppToolset"
LOGS = "EditorToolset.LogsToolset"
ROOM_POSITION = {"x": 10000, "y": 0, "z": 0}
DIMENSIONS = ["RoomLength", "RoomWidth", "RoomHeight", "WallThickness", "FloorThickness"]
parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="validation")
parser.add_argument("--null-rhi", action="store_true")
args = parser.parse_args()
if not args.run_name.replace("-", "").isalnum():
    raise ValueError("run-name must be alphanumeric/hyphen")
REPORT = EVIDENCE / f"{args.run_name}.json"
if REPORT.exists():
    raise RuntimeError("Evidence already exists; refusing overwrite.")
with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", PORT)) == 0:
        raise RuntimeError("Port already in use; refusing another editor's MCP server.")

evidence = {"task": "P01", "project": str(ROOT / "testfordemo.uproject"),
            "map": MAP, "null_rhi": args.null_rhi, "calls": [], "checks": []}
request_id = 0
session_id = None
property_schemas = {}


def rpc(method, params=None, notification=False):
    global request_id, session_id
    request_id += 1
    payload = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if not notification:
        payload["id"] = request_id
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
               "MCP-Protocol-Version": "2025-11-25"}
    if session_id:
        headers["Mcp-Session-Id"] = session_id
    request = urllib.request.Request(URL, json.dumps(payload).encode(), headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=60) as response:
        session_id = response.headers.get("Mcp-Session-Id", session_id)
        raw = response.read().decode()
    if not raw:
        return {}
    if raw.lstrip().startswith(("data:", "event:")):
        messages = [json.loads(line[5:].strip()) for line in raw.splitlines() if line.startswith("data:")]
        result = next(item for item in messages if item.get("id") == request_id)
    else:
        result = json.loads(raw)
    if "error" in result:
        raise RuntimeError(result["error"])
    return result.get("result", {})


def call(toolset, name, arguments=None):
    params = {"toolset_name": toolset, "tool_name": name, "arguments": arguments or {}}
    result = rpc("tools/call", {"name": "call_tool", "arguments": params})
    if result.get("isError"):
        evidence["calls"].append({**params, "error": result})
        raise RuntimeError(result)
    text = "\n".join(item.get("text", "") for item in result.get("content", []) if item.get("type") == "text")
    parsed = json.loads(text) if text else {}
    value = parsed.get("returnValue")
    recorded = value
    if name == "CaptureViewport" and isinstance(value, dict):
        recorded = {**value, "image": {"mimeType": value["image"]["mimeType"],
                                       "base64Length": len(value["image"]["data"])}}
    evidence["calls"].append({**params, "returnValue": recorded})
    return value


def check(name, condition, detail=None):
    evidence["checks"].append({"name": name, "passed": bool(condition), "detail": detail})
    print(f"{name}: {'PASS' if condition else 'FAIL'}", flush=True)
    if not condition:
        raise AssertionError(f"{name}: {detail}")


def discover(obj):
    value = call(OBJECT, "list_properties", {"instance": obj})
    schema = json.loads(value) if isinstance(value, str) else value
    property_schemas[obj["refPath"]] = schema
    return schema


def property_name(obj, native_name):
    schema = property_schemas.get(obj["refPath"])
    if schema is None:
        schema = discover(obj)
    return next(name for name in schema if name.casefold() == native_name.casefold())


def props(obj, names):
    actual_names = [property_name(obj, name) for name in names]
    value = call(OBJECT, "get_properties", {"instance": obj, "properties": actual_names})
    decoded = json.loads(value) if isinstance(value, str) else value
    return {name: decoded[actual] for name, actual in zip(names, actual_names)}


def set_props(obj, values):
    actual_values = {property_name(obj, name): value for name, value in values.items()}
    check("set_properties " + ",".join(values), call(OBJECT, "set_properties", {
        "instance": obj, "values": json.dumps(actual_values, ensure_ascii=False)}) is True)


def find(label):
    return call(SCENE, "find_actors", {"name": label, "tag": "", "collision_channels": []})


def component(actor, cls):
    found = call(ACTOR, "get_components", {"actor": actor, "component_type": {"refPath": cls}})
    check("single POI mesh component", len(found) == 1, found)
    return found[0]


def similar(a, b):
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(similar(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(similar(x, y) for x, y in zip(a, b))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, abs_tol=1e-5)
    return a == b


def poi_snapshot(poi, mesh):
    return {"transform": call(ACTOR, "get_actor_transform", {"actor": poi}),
            "bounds": call(ACTOR, "get_actor_bounds", {"actor": poi}),
            "mesh_configuration": props(mesh, ["StaticMesh", "OverrideMaterials", "BodyInstance", "Mobility",
                                               "RelativeLocation", "RelativeRotation", "RelativeScale3D"])}


def world(local):
    return {axis: ROOM_POSITION[axis] + value for axis, value in zip(("x", "y", "z"), local)}


def trace(name, start, end, expected):
    distance = call(SCENE, "trace_world", {"start": world(start), "end": world(end)})
    valid = distance is None if expected is None else isinstance(distance, (int, float)) and abs(distance - expected) < 0.2
    check(name, valid, {"start": world(start), "end": world(end), "expected_cm": expected, "actual_cm": distance})


def collision_suite(name, length, width, height, wall=20, floor=20):
    for side, end, expected in (
        ("north", (length, 0, 200), length / 2), ("south", (-length, 0, 200), length / 2),
        ("east", (0, width, 200), width / 2), ("west", (0, -width, 200), width / 2)):
        trace(name + " " + side + " interior blocking", (0, 0, 200), end, expected)
    trace(name + " floor top", (0, 0, 100), (0, 0, -100), 100)
    trace(name + " north outer face", (length / 2 + 200, 0, 200), (0, 0, 200), 200 - wall)
    trace(name + " floor bottom", (0, 0, -100), (0, 0, 100), 100 - floor)
    trace(name + " wall above room", (0, 0, height + 50), (length, 0, height + 50), None)


def capture(name):
    if args.null_rhi:
        evidence.setdefault("not_tested", []).append("Viewport image: null-RHI")
        return
    try:
        value = call(APP, "CaptureViewport", {"captureTransform": {
            "location": {"x": 13500, "y": -3500, "z": 2600},
            "rotation": {"pitch": -28, "yaw": 135, "roll": 0}},
            # The current MCP adapter requires an explicit optional struct.
            # Zero grid/label values disable spatial overlays.
            "annotations": {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0,
                            "maxLabelDistance": 0, "classFilter": ROOM_CLASS, "maxLabels": 0},
            "bShowUI": False})
        data = base64.b64decode(value["image"]["data"])
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise RuntimeError("Empty/non-PNG viewport image")
        folder = EVIDENCE / "captures"
        folder.mkdir(exist_ok=True)
        path = folder / f"{args.run_name}-{name}.png"
        if path.exists():
            raise RuntimeError("Capture already exists; refusing overwrite")
        path.write_bytes(data)
        evidence.setdefault("captures", []).append({"path": str(path), "sha256": hashlib.sha256(data).hexdigest()})
    except Exception as error:
        evidence.setdefault("not_tested", []).append(f"Viewport {name}: {error!r}")


startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = ["D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(ROOT / "testfordemo.uproject"), MAP,
           "-unattended", "-nosplash", "-nosound", "-nop4", "-ModelContextProtocolStartServer",
           f"-ModelContextProtocolPort={PORT}", "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1",
           f"-abslog={EVIDENCE / (args.run_name + '-editor.log')}"]
if args.null_rhi:
    command.append("-nullrhi")
process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
evidence["temporary_editor_pid"] = process.pid
pie_started = False
print(f"Temporary P01 editor PID={process.pid}", flush=True)
try:
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Editor exited before MCP: {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", PORT), timeout=1):
                break
        except OSError:
            time.sleep(2)
    else:
        raise TimeoutError("MCP startup")
    evidence["initialize"] = rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "RogueliteP01Validation", "version": "1.0"}})
    rpc("notifications/initialized", notification=True)
    evidence["meta_tools"] = rpc("tools/list")
    toolsets = rpc("tools/call", {"name": "list_toolsets", "arguments": {}})
    evidence["toolsets"] = toolsets
    for toolset in (SCENE, ACTOR, OBJECT, ASSET, APP, LOGS):
        described = rpc("tools/call", {"name": "describe_toolset", "arguments": {"toolset_name": toolset}})
        if described.get("isError"):
            raise RuntimeError(described)
    check("correct map", call(SCENE, "get_current_level") == MAP)
    check("map starts saved", call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    check("PIE initially stopped", call(APP, "IsPIERunning") is False)
    rooms = find("P01_ParametricRoom")
    check("no duplicate P01 rooms", len(rooms) <= 1)
    room = rooms[0] if rooms else call(SCENE, "add_to_scene_from_class", {
        "actor_type": ROOM_CLASS, "name": "P01_ParametricRoom", "xform": {"location": ROOM_POSITION}})
    evidence["room"] = room
    discovery = discover(room)
    check("room properties discovered", all(name.casefold() in {key.casefold() for key in discovery} for name in DIMENSIONS + ["ManualPOIActors", "ValidationWarnings"]))
    component_refs = call(ACTOR, "get_components", {"actor": room})
    check("six fixed components", len(component_refs) == 6, component_refs)
    meshes = call(ACTOR, "get_components", {"actor": room, "component_type": {"refPath": "/Script/Engine.StaticMeshComponent"}})
    check("five shell mesh components", len(meshes) == 5)
    for mesh in meshes:
        discovery = discover(mesh)
        check("shell properties discovered", all(name.casefold() in {key.casefold() for key in discovery} for name in ["RelativeLocation", "RelativeScale3D", "BodyInstance"]))
    set_props(room, dict(zip(DIMENSIONS, [2000, 2000, 400, 20, 20])))
    check("2000x2000 dimensions", props(room, DIMENSIONS) == dict(zip(DIMENSIONS, [2000, 2000, 400, 20, 20])))
    collision_suite("2000x2000", 2000, 2000, 400)
    capture("2000x2000")
    set_props(room, {"RoomLength": 3000, "RoomWidth": 2500})
    collision_suite("3000x2500", 3000, 2500, 400)
    trace("old north wall removed after expansion", (950, 0, 200), (1100, 0, 200), None)
    trace("old east wall removed after expansion", (0, 950, 200), (0, 1100, 200), None)
    capture("3000x2500")
    set_props(room, {"RoomHeight": 700})
    trace("height increased: wall at z600", (0, 0, 600), (3000, 0, 600), 1500)
    check("height edit leaves XY unchanged", props(room, ["RoomLength", "RoomWidth", "RoomHeight"]) == {"RoomLength": 3000, "RoomWidth": 2500, "RoomHeight": 700})
    set_props(room, {"RoomHeight": 400})
    trace("height reduced: old upper collision removed", (0, 0, 600), (3000, 0, 600), None)
    set_props(room, {"WallThickness": 50, "FloorThickness": 40})
    collision_suite("thickness 50/40", 3000, 2500, 400, 50, 40)
    set_props(room, {"WallThickness": 20, "FloorThickness": 20})
    trace("old outer wall thickness removed", (1530, 0, 200), (1560, 0, 200), None)
    trace("old floor thickness removed", (0, 0, -35), (0, 0, -25), None)
    pois = find("P01_ManualPOI")
    check("no duplicate P01 POIs", len(pois) <= 1)
    poi = pois[0] if pois else call(SCENE, "add_to_scene_from_class", {
        "actor_type": {"refPath": "/Script/Engine.StaticMeshActor"}, "name": "P01_ManualPOI",
        "xform": {"location": {"x": 10900, "y": 600, "z": 100},
                  "rotation": {"pitch": 0, "yaw": 30, "roll": 0}, "scale": {"x": 3, "y": 2, "z": 2}}})
    poi_mesh = component(poi, "/Script/Engine.StaticMeshComponent")
    discover(poi_mesh)
    if not pois:
        set_props(poi_mesh, {"StaticMesh": {"refPath": "/Engine/BasicShapes/Cube.Cube"}})
    baseline_poi = poi_snapshot(poi, poi_mesh)
    evidence["poi_baseline"] = baseline_poi
    set_props(room, {"ManualPOIActors": [poi]})
    check("POI inside larger room", not props(room, ["OutOfBoundsManualPOIActors"])["OutOfBoundsManualPOIActors"])
    for index, (length, width) in enumerate([(2000, 2000), (3000, 2500), (2200, 2100), (3000, 2500), (2000, 2000)]):
        set_props(room, {"RoomLength": length, "RoomWidth": width})
        current_components = call(ACTOR, "get_components", {"actor": room})
        check(f"repeat {index} component references stable", len(current_components) == 6
              and {item["refPath"] for item in current_components} == {item["refPath"] for item in component_refs})
        check(f"repeat {index} POI transform and configuration unchanged", similar(poi_snapshot(poi, poi_mesh), baseline_poi))
    warnings = props(room, ["ValidationWarnings", "OutOfBoundsManualPOIActors"])
    check("Bounds warning with origin still inside", len(warnings["OutOfBoundsManualPOIActors"]) == 1
          and any("P01_POI_OUT_OF_BOUNDS" in line and "P01_ManualPOI" in line for line in warnings["ValidationWarnings"]), warnings)
    evidence["out_of_bounds_warnings"] = warnings
    evidence["warning_log"] = call(LOGS, "GetLogEntries", {"category": "LogRoguelitePrototype", "pattern": "P01_POI_OUT_OF_BOUNDS", "maxEntries": 4})
    check("warning appears in Output Log", bool(evidence["warning_log"]))
    collision_suite("repeated updates final 2000x2000", 2000, 2000, 400)
    trace("old expanded north wall removed after shrink", (1400, 0, 200), (1600, 0, 200), None)
    trace("old expanded east wall removed after shrink", (0, 1150, 200), (0, 1350, 200), None)
    set_props(room, dict(zip(DIMENSIONS, [0, -1, 0, 0, -1])))
    check("invalid dimensions clamped", props(room, DIMENSIONS) == dict(zip(DIMENSIONS, [100, 100, 100, 1, 1])))
    set_props(room, dict(zip(DIMENSIONS, [3000, 2500, 500, 20, 20])))
    check("room actor unit scale", call(ACTOR, "get_actor_transform", {"actor": room})["scale"] == {"x": 1, "y": 1, "z": 1})
    check("POI unchanged before save", similar(poi_snapshot(poi, poi_mesh), baseline_poi))
    evidence["saved_room_properties"] = props(room, DIMENSIONS + ["ManualPOIActors"])
    evidence["saved_shell_properties"] = [props(mesh, ["RelativeLocation", "RelativeScale3D", "BodyInstance"]) for mesh in meshes]
    check("save only development map", call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("saved map clean", call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    # Open an empty engine template to unload the saved map, then reload it.
    call(SCENE, "load_level", {"level_path": "/Engine/Maps/Templates/Template_Default"})
    check("map unloaded", call(SCENE, "get_current_level") != MAP)
    call(SCENE, "load_level", {"level_path": MAP})
    room = find("P01_ParametricRoom")[0]
    poi = find("P01_ManualPOI")[0]
    poi_mesh = component(poi, "/Script/Engine.StaticMeshComponent")
    check("saved dimensions and POI references persisted", props(room, DIMENSIONS + ["ManualPOIActors"]) == evidence["saved_room_properties"])
    check("POI unchanged after reload", similar(poi_snapshot(poi, poi_mesh), baseline_poi))
    check("fixed components after reload", len(call(ACTOR, "get_components", {"actor": room})) == 6)
    collision_suite("reloaded 3000x2500 height500", 3000, 2500, 500)
    trace("reloaded height500 upper wall", (0, 0, 450), (3000, 0, 450), 1500)
    capture("reloaded-final")
    call(APP, "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 2}})
    pie_started = True
    check("PIE starts with new room", call(APP, "IsPIERunning") is True)
    evidence["pie_log"] = call(LOGS, "GetLogEntries", {"category": "LogRoguelitePrototype", "pattern": "ROGUELITE_DEV_START", "maxEntries": 2})
    check("independent GameMode in PIE", any("LegacyActorCount=0" in line for line in evidence["pie_log"]))
    call(APP, "StopPIE")
    pie_started = False
    check("PIE stopped", call(APP, "IsPIERunning") is False)
    check("map remains clean", call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    evidence["passed"] = True
    print("P01_MCP_INTEGRATION_PASS", flush=True)
except Exception as error:
    evidence["passed"] = False
    evidence["error"] = repr(error)
    raise
finally:
    if pie_started:
        try:
            call(APP, "StopPIE")
        except Exception as error:
            evidence["cleanup_pie_error"] = repr(error)
    # Stop only the editor instance this script created, never the owner's process.
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            evidence["stop_timeout"] = True
    evidence["temporary_editor_stopped"] = process.poll() is not None
    REPORT.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
