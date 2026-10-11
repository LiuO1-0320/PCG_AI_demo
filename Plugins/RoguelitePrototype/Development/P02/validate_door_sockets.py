"""P02 official MCP integration. Saves only the separate P02 development map.

Does not run/import the P01 validation script. Existing P01 layout is read only.
Resume is permitted only for this task's dedicated map and named test actors.
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
SOURCE_MAP = "/Game/Roguelite/Maps/L_Roguelite_Dev"
MAP = "/Game/Roguelite/Maps/L_Roguelite_DoorSocket_Dev"
ROOM_CLASS = {"refPath": "/Script/RoguelitePrototype.RogueliteParametricRoom"}
SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
APP = "EditorToolset.EditorAppToolset"
LOGS = "EditorToolset.LogsToolset"
SLATE = "SlateInspectorToolset.SlateInspectorToolset"
PORT = 8001
URL = f"http://127.0.0.1:{PORT}/mcp"
SIDES = ["North", "South", "East", "West"]
NORMALS = {"North": (1, 0, 0), "South": (-1, 0, 0), "East": (0, 1, 0), "West": (0, -1, 0)}
POSITION = {"x": 30000, "y": 0, "z": 0}
DIMENSIONS = ["RoomLength", "RoomWidth", "RoomHeight", "WallThickness", "FloorThickness"]
parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="validation")
parser.add_argument("--resume-owned-map", action="store_true")
parser.add_argument("--hold-for-supplement", action="store_true")
args = parser.parse_args()
if not args.run_name.replace("-", "").isalnum():
    raise ValueError("run-name must be alphanumeric/hyphen")
REPORT = EVIDENCE / f"{args.run_name}.json"
STOP_FILE = EVIDENCE / f"{args.run_name}-stop-request.json"
if REPORT.exists() or STOP_FILE.exists():
    raise RuntimeError("Evidence already exists; refusing overwrite")
with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", PORT)) == 0:
        raise RuntimeError("MCP port occupied; refusing another editor")
evidence = {"task": "P02", "project": str(ROOT / "testfordemo.uproject"), "map": MAP,
            "calls": [], "checks": [], "schemas": {}}
request_id = 0
session_id = None
property_schemas = {}
room = None


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
        result = next(json.loads(line[5:].strip()) for line in raw.splitlines()
                      if line.startswith("data:") and json.loads(line[5:].strip()).get("id") == request_id)
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
    value = json.loads(text).get("returnValue") if text else None
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


def key(mapping, native):
    return next(k for k in mapping if k.casefold() == native.casefold())


def field(mapping, native):
    return mapping[key(mapping, native)]


def discover(obj):
    value = call(OBJECT, "list_properties", {"instance": obj})
    schema = json.loads(value) if isinstance(value, str) else value
    property_schemas[obj["refPath"]] = schema
    return schema


def props(obj, names):
    schema = property_schemas.get(obj["refPath"]) or discover(obj)
    actual_names = [key(schema, name) for name in names]
    value = call(OBJECT, "get_properties", {"instance": obj, "properties": actual_names})
    decoded = json.loads(value) if isinstance(value, str) else value
    return {name: decoded[actual] for name, actual in zip(names, actual_names)}


def set_props(obj, values):
    schema = property_schemas.get(obj["refPath"]) or discover(obj)
    actual = {key(schema, name): value for name, value in values.items()}
    check("set " + ",".join(values), call(OBJECT, "set_properties", {
        "instance": obj, "values": json.dumps(actual, ensure_ascii=False)}) is True)


def set_door(side, **changes):
    name = side + "Door"
    current = props(room, [name])[name]
    values = {**current, **{key(current, native): value for native, value in changes.items()}}
    set_props(room, {name: values})


def all_doors(enabled, auto=True):
    values = props(room, [side + "Door" for side in SIDES])
    for value in values.values():
        for native, new in {"bEnabled": enabled, "bAutoCenter": auto, "ManualOffset": 0,
                            "DoorWidth": 200, "DoorHeight": 250}.items():
            value[key(value, native)] = new
    set_props(room, values)


def find(label):
    return call(SCENE, "find_actors", {"name": label, "tag": "", "collision_channels": []})


def similar(a, b):
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(similar(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(similar(x, y) for x, y in zip(a, b))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, abs_tol=0.0001)
    return a == b


def world(local):
    return {axis: POSITION[axis] + value for axis, value in zip(("x", "y", "z"), local)}


def trace(label, start, end, expected):
    distance = call(SCENE, "trace_world", {"start": world(start), "end": world(end)})
    valid = distance is None if expected is None else isinstance(distance, (int, float)) and abs(distance - expected) < 0.2
    check(label, valid, {"start": world(start), "end": world(end), "expected_cm": expected, "actual_cm": distance})


def wall_ray(label, side, length, width, tangent, height, expected):
    n = NORMALS[side]
    t = (0, 1, 0) if side in ("North", "South") else (1, 0, 0)
    half = (length if side in ("North", "South") else width) / 2
    start = tuple(n[i] * (half - 200) + t[i] * tangent + (height if i == 2 else 0) for i in range(3))
    end = tuple(n[i] * (half + 200) + t[i] * tangent + (height if i == 2 else 0) for i in range(3))
    trace(label, start, end, expected)


def sockets():
    values = props(room, ["DoorSockets"])["DoorSockets"]
    result = {}
    for value in values:
        side = field(value, "Side")
        if isinstance(side, int):
            side = SIDES[side]
        else:
            side = str(side).split("::")[-1]
        result[side] = value
    return result


def socket_suite(label, length, width, offsets, yaw=0):
    values = sockets()
    check(label + " four socket records", len(values) == 4, values)
    for side in SIDES:
        value = values[side]
        n = NORMALS[side]
        t = (0, 1, 0) if side in ("North", "South") else (1, 0, 0)
        half = (length if side in ("North", "South") else width) / 2
        local = [n[i] * (half + 20) + t[i] * offsets.get(side, 0) for i in range(3)]
        radians = math.radians(yaw)
        c, s = math.cos(radians), math.sin(radians)
        expected_world = {"x": POSITION["x"] + c * local[0] - s * local[1],
                          "y": POSITION["y"] + s * local[0] + c * local[1], "z": 0}
        expected_normal = {"x": c * n[0] - s * n[1], "y": s * n[0] + c * n[1], "z": 0}
        check(label + " " + side + " position/direction", field(value, "bValid") is True
              and similar(field(value, "LocalPosition"), dict(zip(("x", "y", "z"), local)))
              and similar(field(value, "WorldPosition"), expected_world)
              and similar(field(value, "LocalOutwardDirection"), dict(zip(("x", "y", "z"), n)))
              and similar(field(value, "WorldOutwardDirection"), expected_normal), value)


def openings_suite(label, length, width, offsets=None, door_width=200, door_height=250):
    offsets = offsets or {}
    for side in SIDES:
        offset = offsets.get(side, 0)
        for height in (1, door_height / 2, door_height - 1):
            wall_ray(f"{label} {side} opening z{height}", side, length, width, offset, height, None)
        for sign in (-1, 1):
            wall_ray(f"{label} {side} solid side {sign}", side, length, width,
                     offset + sign * (door_width / 2 + 30), 100, 200)
        wall_ray(f"{label} {side} lintel", side, length, width, offset, door_height + 30, 200)


def closed_shell_suite(label, length, width, height, wall=20, floor=20):
    for side in SIDES:
        n = NORMALS[side]
        extent = length if side in ("North", "South") else width
        trace(label + " " + side, (0, 0, 200), (n[0] * extent, n[1] * extent, 200), extent / 2)
    trace(label + " floor top", (0, 0, 100), (0, 0, -100), 100)
    trace(label + " north outer face", (length / 2 + 200, 0, 200), (0, 0, 200), 200 - wall)
    trace(label + " floor bottom", (0, 0, -100), (0, 0, 100), 100 - floor)
    trace(label + " above walls", (0, 0, height + 50), (length, 0, height + 50), None)


def poi_snapshot(poi, mesh):
    return {"transform": call(ACTOR, "get_actor_transform", {"actor": poi}),
            "bounds": call(ACTOR, "get_actor_bounds", {"actor": poi}),
            "mesh_configuration": props(mesh, ["StaticMesh", "OverrideMaterials", "BodyInstance", "Mobility",
                                               "RelativeLocation", "RelativeRotation", "RelativeScale3D"])}


def capture(name):
    try:
        value = call(APP, "CaptureViewport", {"captureTransform": {
            "location": {"x": 33500, "y": -3500, "z": 2600},
            "rotation": {"pitch": -28, "yaw": 135, "roll": 0}},
            "annotations": {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0,
                            "maxLabelDistance": 0, "classFilter": ROOM_CLASS, "maxLabels": 0}, "bShowUI": True})
        data = base64.b64decode(value["image"]["data"])
        check("capture PNG " + name, data.startswith(b"\x89PNG\r\n\x1a\n"))
        path = EVIDENCE / f"{args.run_name}-{name}.png"
        with path.open("xb") as output:
            output.write(data)
        evidence.setdefault("captures", []).append({"path": str(path), "sha256": hashlib.sha256(data).hexdigest()})
    except Exception as error:
        evidence.setdefault("not_tested", []).append(f"Viewport {name}: {error!r}")


startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
command = ["D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(ROOT / "testfordemo.uproject"), SOURCE_MAP,
           "-unattended", "-nosplash", "-nosound", "-nop4", "-ModelContextProtocolStartServer",
           f"-ModelContextProtocolPort={PORT}", "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1",
           f"-abslog={EVIDENCE / (args.run_name + '-editor.log')}"]
process = subprocess.Popen(command, startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
evidence["temporary_editor_pid"] = process.pid
pie_started = False
print(f"Temporary P02 editor PID={process.pid}", flush=True)
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
        "clientInfo": {"name": "RogueliteP02Validation", "version": "1.0"}})
    rpc("notifications/initialized", notification=True)
    evidence["toolsets"] = rpc("tools/call", {"name": "list_toolsets", "arguments": {}})
    for toolset in (SCENE, ACTOR, OBJECT, ASSET, APP, LOGS, SLATE):
        described = rpc("tools/call", {"name": "describe_toolset", "arguments": {"toolset_name": toolset}})
        if described.get("isError"):
            raise RuntimeError(described)
        evidence["schemas"][toolset] = described
    check("source P01 map loaded read only", call(SCENE, "get_current_level") == SOURCE_MAP)
    check("source P01 map not dirty", call(ASSET, "is_dirty", {"asset_path": SOURCE_MAP}) is False)
    source_rooms = find("P01_ParametricRoom")
    check("P01 room exists", len(source_rooms) == 1)
    evidence["source_p01_properties"] = props(source_rooms[0], DIMENSIONS + ["ManualPOIActors"])
    if not args.resume_owned_map:
        check("new P02 map absent", call(ASSET, "exists", {"path": MAP}) is False)
        check("duplicate into dedicated P02 map", call(ASSET, "duplicate", {"path": SOURCE_MAP, "new_path": MAP}) is True)
        # Duplication creates an unsaved World; the MCP loader rejects dirty targets.
        check("save only new cloned P02 map before loading", call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    else:
        check("dedicated P02 map exists", call(ASSET, "exists", {"path": MAP}) is True)
    call(SCENE, "load_level", {"level_path": MAP})
    check("P02 map loaded", call(SCENE, "get_current_level") == MAP)
    rooms = find("P02_DoorSocketRoom")
    check("no duplicate P02 rooms", len(rooms) <= 1)
    room = rooms[0] if rooms else call(SCENE, "add_to_scene_from_class", {
        "actor_type": ROOM_CLASS, "name": "P02_DoorSocketRoom", "xform": {"location": POSITION}})
    evidence["room"] = room
    evidence["room_schema"] = discover(room)
    set_props(room, dict(zip(DIMENSIONS, [2000, 2000, 400, 20, 20])))
    component_refs = call(ACTOR, "get_components", {"actor": room})
    meshes = call(ACTOR, "get_components", {"actor": room, "component_type": {"refPath": "/Script/Engine.StaticMeshComponent"}})
    check("13 fixed mesh components", len(meshes) == 13, meshes)
    check("18 fixed editor components", len(component_refs) == 18, component_refs)
    all_doors(True)
    socket_suite("centered 2000x2000", 2000, 2000, {})
    openings_suite("centered 2000x2000", 2000, 2000)
    capture("centered-2000x2000")
    for side in SIDES:
        set_door(side, bEnabled=False)
        check(side + " disabled socket absent", side not in sockets())
        wall_ray(side + " disabled full wall", side, 2000, 2000, 0, 100, 200)
        set_door(side, bEnabled=True)
        wall_ray(side + " re-enabled opening", side, 2000, 2000, 0, 100, None)
    for side in SIDES:
        all_doors(False)
        set_door(side, bEnabled=True)
        check("only " + side + " socket", set(sockets()) == {side})
        for tested in SIDES:
            wall_ray("only " + side + ": " + tested, tested, 2000, 2000, 0, 100, None if tested == side else 200)
    all_doors(True)
    set_door("North", bAutoCenter=False, ManualOffset=300)
    set_door("East", bAutoCenter=False, ManualOffset=-250)
    offsets = {"North": 300, "East": -250}
    socket_suite("manual offsets", 2000, 2000, offsets)
    openings_suite("manual offsets", 2000, 2000, offsets)
    wall_ray("old north centered opening closed", "North", 2000, 2000, 0, 100, 200)
    wall_ray("old east centered opening closed", "East", 2000, 2000, 0, 100, 200)
    set_props(room, {"RoomLength": 3000, "RoomWidth": 2500})
    socket_suite("resized offsets retained", 3000, 2500, offsets)
    openings_suite("resized manual offsets", 3000, 2500, offsets)
    trace("old north wall removed after expansion", (950, 0, 200), (1100, 0, 200), None)
    trace("old east wall removed after expansion", (0, 950, 200), (0, 1100, 200), None)
    for yaw in (90, 180, 270):
        call(ACTOR, "set_actor_transform", {"actor": room, "xform": {"location": POSITION,
             "rotation": {"pitch": 0, "yaw": yaw, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}}})
        set_props(room, {"RoomHeight": 400})
        socket_suite(f"rotated {yaw}", 3000, 2500, offsets, yaw)
    call(ACTOR, "set_actor_transform", {"actor": room, "xform": {"location": POSITION,
         "rotation": {"pitch": 0, "yaw": 0, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}}})
    set_props(room, {"RoomLength": 2000, "RoomWidth": 2000})
    all_doors(True)
    for bad in ({"DoorWidth": 0}, {"DoorWidth": -200}, {"DoorWidth": 2100}, {"DoorHeight": 500},
                {"bAutoCenter": False, "ManualOffset": 9999}):
        set_door("North", bAutoCenter=True, ManualOffset=0, DoorWidth=200, DoorHeight=250, **{})
        set_door("North", **bad)
        value = sockets()["North"]
        current = props(room, ["NorthDoor"])["NorthDoor"]
        warnings = props(room, ["ValidationWarnings"])["ValidationWarnings"]
        check("invalid door warned and config retained " + str(bad), field(value, "bValid") is False
              and all(field(current, k) == v for k, v in bad.items())
              and any("P02_INVALID_DOOR" in w and "Side=North" in w for w in warnings), {"config": current, "socket": value, "warnings": warnings})
        wall_ray("invalid north restores full wall " + str(bad), "North", 2000, 2000, 0, 100, 200)
    set_door("North", DoorWidth=200, DoorHeight=250, bAutoCenter=False, ManualOffset=600)
    check("offset600 initially legal", field(sockets()["North"], "bValid") is True)
    set_props(room, {"RoomWidth": 1000})
    check("shrinking retains offset600 and invalidates opening", field(props(room, ["NorthDoor"])["NorthDoor"], "ManualOffset") == 600
          and field(sockets()["North"], "bValid") is False)
    wall_ray("shrink invalid door full wall", "North", 2000, 1000, 0, 100, 200)
    set_props(room, {"RoomWidth": 2000})
    check("expanding restores original offset600", field(sockets()["North"], "bValid") is True)
    wall_ray("restored offset600 opening", "North", 2000, 2000, 600, 100, None)
    for offset in (-850, 850):
        set_door("North", ManualOffset=offset)
        check("corner-safe limit " + str(offset), field(sockets()["North"], "bValid") is True)
        wall_ray("legal boundary opening " + str(offset), "North", 2000, 2000, offset, 100, None)
    set_door("North", bAutoCenter=True, DoorWidth=1900, DoorHeight=380)
    check("legal width/top limits", field(sockets()["North"], "bValid") is True)
    wall_ray("legal max height opening", "North", 2000, 2000, 0, 379, None)
    wall_ray("legal min lintel collision", "North", 2000, 2000, 0, 390, 200)
    for property_name in ("MinDoorSideWall", "MinDoorLintelHeight"):
        set_props(room, {property_name: 0})
        check("illegal clearance warning " + property_name, field(sockets()["North"], "bValid") is False
              and props(room, [property_name])[property_name] == 0)
        set_props(room, {property_name: 50 if property_name == "MinDoorSideWall" else 20})
    evidence["door_warning_log"] = call(LOGS, "GetLogEntries", {"category": "LogRoguelitePrototype", "pattern": "P02_INVALID_DOOR", "maxEntries": 8})
    check("invalid door warning in Output Log", bool(evidence["door_warning_log"]))
    all_doors(False)
    pois = find("P02_ManualPOI")
    check("no duplicate manual POI", len(pois) <= 1)
    poi = pois[0] if pois else call(SCENE, "add_to_scene_from_class", {
        "actor_type": {"refPath": "/Script/Engine.StaticMeshActor"}, "name": "P02_ManualPOI",
        "xform": {"location": {"x": 30900, "y": 600, "z": 100},
                  "rotation": {"pitch": 0, "yaw": 30, "roll": 0}, "scale": {"x": 3, "y": 2, "z": 2}}})
    poi_mesh = call(ACTOR, "get_components", {"actor": poi, "component_type": {"refPath": "/Script/Engine.StaticMeshComponent"}})[0]
    discover(poi_mesh)
    if not pois:
        set_props(poi_mesh, {"StaticMesh": {"refPath": "/Engine/BasicShapes/Cube.Cube"}})
    evidence["poi_baseline"] = poi_snapshot(poi, poi_mesh)
    set_props(room, {"ManualPOIActors": [poi]})
    closed_shell_suite("P01 regression 2000x2000", 2000, 2000, 400)
    set_props(room, {"RoomLength": 3000, "RoomWidth": 2500})
    closed_shell_suite("P01 regression 3000x2500", 3000, 2500, 400)
    set_props(room, {"RoomHeight": 700})
    trace("P01 height700 wall at z600", (0, 0, 600), (3000, 0, 600), 1500)
    set_props(room, {"RoomHeight": 400})
    trace("P01 height400 no old upper collision", (0, 0, 600), (3000, 0, 600), None)
    set_props(room, {"WallThickness": 50, "FloorThickness": 40})
    closed_shell_suite("P01 thickness50/40", 3000, 2500, 400, 50, 40)
    set_props(room, {"WallThickness": 20, "FloorThickness": 20})
    trace("P01 old floor thickness removed", (0, 0, -35), (0, 0, -25), None)
    trace("P01 old outer wall thickness removed", (1530, 0, 200), (1560, 0, 200), None)
    for index, (length, width) in enumerate(((2000, 2000), (3000, 2500), (2200, 2100), (3000, 2500), (2000, 2000))):
        set_props(room, {"RoomLength": length, "RoomWidth": width})
        all_doors(bool(index % 2))
        check(f"repeat {index} fixed components", {x["refPath"] for x in call(ACTOR, "get_components", {"actor": room})}
              == {x["refPath"] for x in component_refs})
        check(f"repeat {index} POI untouched", similar(poi_snapshot(poi, poi_mesh), evidence["poi_baseline"]))
    warnings = props(room, ["ValidationWarnings", "OutOfBoundsManualPOIActors"])
    check("P01 actual Bounds warning with origin inside", len(warnings["OutOfBoundsManualPOIActors"]) == 1
          and any("P01_POI_OUT_OF_BOUNDS" in w and "P02_ManualPOI" in w for w in warnings["ValidationWarnings"]), warnings)
    evidence["poi_bounds_warning"] = warnings
    trace("old expanded north collision removed", (1400, 0, 200), (1600, 0, 200), None)
    trace("old expanded east collision removed", (0, 1150, 200), (0, 1350, 200), None)
    set_props(room, dict(zip(DIMENSIONS, [0, -1, 0, 0, -1])))
    check("P01 min dimensions preserved", props(room, DIMENSIONS) == dict(zip(DIMENSIONS, [100, 100, 100, 1, 1])))
    set_props(room, dict(zip(DIMENSIONS, [3000, 2500, 500, 20, 20])))
    all_doors(True)
    set_door("North", bAutoCenter=False, ManualOffset=300)
    set_door("East", bAutoCenter=False, ManualOffset=-250)
    socket_suite("final", 3000, 2500, offsets)
    openings_suite("final", 3000, 2500, offsets)
    trace("final floor top", (0, 0, 100), (0, 0, -100), 100)
    check("POI unchanged before save", similar(poi_snapshot(poi, poi_mesh), evidence["poi_baseline"]))
    check("unit actor scale", call(ACTOR, "get_actor_transform", {"actor": room})["scale"] == {"x": 1, "y": 1, "z": 1})
    evidence["saved_properties"] = props(room, DIMENSIONS + [s + "Door" for s in SIDES] + ["ManualPOIActors"])
    evidence["saved_sockets"] = props(room, ["DoorSockets"])["DoorSockets"]
    call(APP, "SelectActors", {"actors": [room]})
    check("save only P02 map", call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("P02 map saved clean", call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    call(SCENE, "load_level", {"level_path": "/Engine/Maps/Templates/Template_Default"})
    check("P02 map unloaded", call(SCENE, "get_current_level") != MAP)
    call(SCENE, "load_level", {"level_path": MAP})
    room = find("P02_DoorSocketRoom")[0]
    check("saved parameters and POI references retained", props(room, list(evidence["saved_properties"])) == evidence["saved_properties"])
    check("socket diagnostics regenerated on load", similar(props(room, ["DoorSockets"])["DoorSockets"], evidence["saved_sockets"]))
    check("fixed components after reload", len(call(ACTOR, "get_components", {"actor": room})) == 18)
    check("POI unchanged after reload", similar(poi_snapshot(find("P02_ManualPOI")[0], poi_mesh), evidence["poi_baseline"]))
    socket_suite("reloaded", 3000, 2500, offsets)
    openings_suite("reloaded", 3000, 2500, offsets)
    call(APP, "SelectActors", {"actors": [room]})
    capture("reloaded-final")
    call(APP, "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 2}})
    pie_started = True
    check("real PIE started", call(APP, "IsPIERunning") is True)
    evidence["pie_log"] = call(LOGS, "GetLogEntries", {"category": "LogRoguelitePrototype", "pattern": "ROGUELITE_DEV_START", "maxEntries": 2})
    check("independent GameMode and no legacy actors", any("LegacyActorCount=0" in line for line in evidence["pie_log"]))
    call(APP, "StopPIE")
    pie_started = False
    check("PIE stopped", call(APP, "IsPIERunning") is False)
    check("P02 map remains clean", call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    evidence["passed"] = True
    print("P02_MCP_INTEGRATION_PASS", flush=True)
    if args.hold_for_supplement:
        REPORT.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print("P02_EDITOR_READY_FOR_SUPPLEMENT", flush=True)
        deadline = time.monotonic() + 600
        while not STOP_FILE.exists() and time.monotonic() < deadline and process.poll() is None:
            time.sleep(1)
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
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            evidence["stop_timeout"] = True
    evidence["temporary_editor_stopped"] = process.poll() is not None
    REPORT.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
