"""P03-A integration on its own map only. Does not invoke P01/P02 validators."""
import argparse
import base64
import json
import math
import traceback
from mcp_client import Client, ROOT, HERE, MAP, SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, field, similar

parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="validation")
parser.add_argument("--editor-log", default="p03a-editor.log")
args = parser.parse_args()
report = HERE / (args.run_name + ".json")
if report.exists() or not args.run_name.replace("-", "").isalnum():
    raise RuntimeError("Refusing invalid/existing report")
c = Client()
evidence = {"task": "P03-A", "map": MAP, "checks": [], "cases": [], "not_tested": []}

def check(name, passed, detail=None):
    evidence["checks"].append({"name": name, "passed": bool(passed), "detail": detail})
    print(name + (": PASS" if passed else ": FAIL"), flush=True)
    if not passed:
        raise AssertionError(name + ": " + str(detail))

def own(name, cls, x, y, z=0):
    found = c.find(name)
    check("unique " + name, len(found) <= 1)
    return found[0] if found else c.call(SCENE, "add_to_scene_from_class", {
        "actor_type": {"refPath": cls}, "name": name, "xform": {"location": {"x": x, "y": y, "z": z}}})

def cube(name, x, y, z, scale):
    found = c.find(name)
    check("unique " + name, len(found) <= 1)
    actor = found[0] if found else c.call(SCENE, "add_to_scene_from_asset", {"asset_path": "/Engine/BasicShapes/Cube.Cube",
        "name": name, "xform": {"location": {"x": x, "y": y, "z": z}, "scale": dict(zip(("x", "y", "z"), scale))}})
    return actor

def act(text="Align Selected Rooms", assembler=None):
    obj = assembler or assembly
    check("Details button " + text, c.button(obj, text) is True)
    return c.props(obj, ["LastResult"])["LastResult"]

def configure(sa="North", sb="South", mode="Direct", assembler=None):
    c.set(assembler or assembly, {"RoomA": a, "RoomB": b, "SocketA": sa, "SocketB": sb, "ConnectionMode": mode})

def connections():
    return c.props(assembly, ["Connections"])["Connections"]

def release():
    records = connections()
    if records:
        # Release this test owner's recorded pair, even after setup changed to a different pair.
        first = records[0]
        c.set(assembly, {"RoomA": field(first, "RoomA"), "RoomB": field(first, "RoomB"),
            "SocketA": field(first, "SocketA"), "SocketB": field(first, "SocketB")})
        act("Disconnect Selected Rooms")
    check("no reserved connection", not connections())

def doors(room):
    for side in ("North", "South", "East", "West"):
        c.door(room, side, bEnabled=True, bAutoCenter=True, ManualOffset=0, DoorWidth=200, DoorHeight=250)

def reset():
    # All reset operations affect this script's explicitly named P03-A fixtures only.
    release()
    configure()
    c.move(a, 50000, 0)
    c.move(b, 55000, 5000)
    c.set(a, {"RoomLength": 2000, "RoomWidth": 2000, "RoomHeight": 400, "WallThickness": 20, "FloorThickness": 20})
    c.set(b, {"RoomLength": 3000, "RoomWidth": 2500, "RoomHeight": 500, "WallThickness": 20, "FloorThickness": 20, "ManualPOIActors": []})
    doors(a)
    doors(b)

def success(name, expected_yaw=None):
    before_a = c.transform(a)
    result = act()
    check(name + " geometry/capsule", all(field(result, n) for n in ("bSucceeded", "bGeometryPassed", "bCapsulePassed")), result)
    check(name + " socket centers/normals", field(result, "SocketDistance") <= 0.1 and field(result, "NormalDot") < -0.99999, result)
    check(name + " fixed A unchanged", similar(before_a, c.transform(a)))
    check(name + " 24 real probes", len(field(result, "Probes")) == 24 and all(field(p, "bPassed") for p in field(result, "Probes")), result)
    check(name + " one logical record", len(connections()) == 1)
    transform = c.transform(b)
    check(name + " Scale 1", similar(field(transform, "scale"), {"x": 1, "y": 1, "z": 1}), transform)
    if expected_yaw is not None:
        yaw = field(field(transform, "rotation"), "yaw")
        check(name + " expected quarter turn", abs((yaw - expected_yaw + 180) % 360 - 180) < 0.001, transform)
    evidence["cases"].append({"name": name, "result": result, "transform_a": before_a, "transform_b": transform, "connections": connections()})
    return result

def failure(name, reason, assembler=None):
    before_b, before_a = c.transform(b), c.transform(a)
    before_records = connections()
    result = act(assembler=assembler)
    # The current official MCP adapter corrupts non-ASCII UObject strings. Read the
    # same native warning from the UTF-8 editor log to verify the concrete reason.
    native_line = [line for line in (HERE / args.editor_log).read_text(encoding="utf-8", errors="replace").splitlines()
        if "LogRoguelitePrototype:" in line and "P03A RoomA=" in line][-1]
    check(name + " rejected with native reason", not field(result, "bSucceeded") and reason in native_line,
        {"result": result, "native_log": native_line})
    check(name + " exact transforms preserved", similar(before_b, c.transform(b)) and similar(before_a, c.transform(a)))
    check(name + " records preserved", similar(before_records, connections()))
    evidence["cases"].append({"name": name, "result": result, "native_log": native_line, "before_b": before_b, "after_b": c.transform(b)})
    return result

try:
    check("isolated P03A map", c.call(SCENE, "get_current_level") == MAP)
    a, b, assembly = (c.find(n)[0] for n in ("P03A_RoomA", "P03A_RoomB", "P03A_Assembly"))
    evidence["actors"] = {"a": a, "b": b, "assembly": assembly}
    reset()
    success("2000x2000 to 3000x2500 centered", 0)
    first_id = field(connections()[0], "Id")
    success("repeat alignment no duplicate", 0)
    check("same connection ID reused", first_id == field(connections()[0], "Id"))
    release()
    c.door(a, "North", bAutoCenter=False, ManualOffset=100)
    c.door(b, "South", bAutoCenter=False, ManualOffset=-250)
    success("independent nonzero offsets", 0)
    check("offset position Y350", abs(field(field(c.transform(b), "location"), "y") - 350) < 0.001)
    for yaw, sa, sb, expected in ((90, "North", "South", 90), (180, "North", "North", 0),
        (270, "East", "West", 270), (0, "North", "East", 90)):
        release()
        configure(sa, sb)
        c.move(a, 50000, 0, yaw=yaw)
        c.move(b, 55000, 5000)
        doors(a)
        doors(b)
        c.door(b, sb, bAutoCenter=False, ManualOffset=300)
        success(f"A yaw{yaw} {sa}/{sb} offset300", expected)
    reset()
    c.set(b, {"WallThickness": 35, "FloorThickness": 30, "RoomHeight": 600})
    success("different wall/floor thickness and height", 0)
    reset()
    c.door(a, "North", bEnabled=False)
    failure("disabled endpoint", "未启用")
    c.door(a, "North", bEnabled=True)
    c.door(b, "South", DoorWidth=240)
    failure("unequal widths", "不兼容")
    c.door(b, "South", DoorWidth=200, DoorHeight=300)
    failure("unequal heights", "不兼容")
    c.door(b, "South", DoorHeight=250, bAutoCenter=False, ManualOffset=99999)
    failure("invalid offset", "非法")
    check("invalid offset kept", field(c.props(b, ["SouthDoor"])["SouthDoor"], "ManualOffset") == 99999)
    c.door(b, "South", bAutoCenter=True, ManualOffset=0, DoorWidth=0)
    failure("invalid door width", "非法")
    c.door(b, "South", DoorWidth=200)
    c.door(a, "North", DoorWidth=60)
    c.door(b, "South", DoorWidth=60)
    failure("door too narrow for capsule", "胶囊")
    c.door(a, "North", DoorWidth=200, DoorHeight=170)
    c.door(b, "South", DoorWidth=200, DoorHeight=170)
    failure("door too low for capsule", "胶囊")
    c.door(a, "North", DoorHeight=250)
    c.door(b, "South", DoorHeight=250)
    c.move(b, 55000, 5000, yaw=45)
    failure("non-quarter-turn B", "Transform 非法")
    c.move(b, 55000, 5000, scale=2)
    failure("Actor scaling rejected", "Transform 非法")
    c.move(b, 55000, 5000)
    for mode in ("StraightCorridor", "CornerCorridor", "CustomConnector"):
        configure(mode=mode)
        failure(mode + " reserved only", "尚未实现")
    configure()
    c.set(assembly, {"RoomB": None})
    failure("invalid target actor", "有效")
    configure()
    third = own("P03A_OverlapFixture", "/Script/RoguelitePrototype.RogueliteParametricRoom", 65000, 0)
    c.move(third, 52540, 0)
    failure("candidate overlaps third room", "重叠")
    c.move(third, 65000, 0)
    poi = cube("P03A_ManualPOI", 55200, 5000, 50, (1, 0.5, 1))
    before_poi = c.transform(poi)
    c.set(b, {"ManualPOIActors": [poi]})
    failure("target manual POI protected", "手工 POI")
    c.set(b, {"RoomLength": 400, "RoomWidth": 400})
    warning = c.props(b, ["ValidationWarnings", "OutOfBoundsManualPOIActors"])
    check("P01 actual Bounds warning", bool(warning["ValidationWarnings"]) and poi in warning["OutOfBoundsManualPOIActors"], warning)
    check("P01 POI location rotation scale unchanged", similar(before_poi, c.transform(poi)))
    c.set(b, {"RoomLength": 3000, "RoomWidth": 2500, "ManualPOIActors": []})
    # Tiny obstacle misses all 9 opening rays but intersects the Pawn capsule at center.
    obstacle = cube("P03A_CapsuleOnlyBlocker", 70000, 0, 100, (0.1, 0.1, 0.1))
    c.move(obstacle, 51020, 20, 100, scale=0.1)
    result = failure("capsule catches ray-missed obstacle and rollback", "Sweep")
    check("rays passed while capsule blocked", field(result, "bGeometryPassed") and not field(result, "bCapsulePassed"), result)
    check("post-move failure restored B", field(result, "bTargetRestoredOnFailure"), result)
    c.move(obstacle, 70000, 0, 100, scale=0.1)
    success("recovery after collision failure", 0)
    competitor = own("P03A_CompetingAssembly", "/Script/RoguelitePrototype.RogueliteRoomAssembly", 48000, 1000)
    configure(assembler=competitor)
    failure("cross-registry endpoint occupied", "占用", competitor)
    before_b = c.transform(b)
    c.set(a, {"RoomLength": 2200})
    invalid = act("Revalidate Connections")
    check("dimension change invalidates current Direct connection", not field(invalid, "bSucceeded") and not field(connections()[0], "bGeometryValid"), invalid)
    check("revalidation never moves B", similar(before_b, c.transform(b)))
    success("explicit re-align after dimension change", 0)
    c.set(a, {"RoomLength": 2000})
    c.door(b, "South", bAutoCenter=False, ManualOffset=300)
    success("final 20m/30x25m noncentered B", 0)
    # P02: toggle each door then native trace in the isolated map; component set remains fixed.
    baseline_components = c.call(ACTOR, "get_components", {"actor": b})
    for side in ("North", "South", "East", "West"):
        socket = next(s for s in c.props(b, ["DoorSockets"])["DoorSockets"] if str(field(s, "Side")).split("::")[-1] == side)
        point, normal = field(socket, "WorldPosition"), field(socket, "WorldOutwardDirection")
        start = {ax: point[ax] - normal[ax] * 100 + (125 if ax == "z" else 0) for ax in ("x", "y", "z")}
        end = {ax: point[ax] + normal[ax] * 100 + (125 if ax == "z" else 0) for ax in ("x", "y", "z")}
        c.door(b, side, bEnabled=False)
        distance = c.call(SCENE, "trace_world", {"start": start, "end": end})
        check("P02 closed " + side + " wall collision", isinstance(distance, (int, float)) and abs(distance-80) < 0.2, distance)
        c.door(b, side, bEnabled=True)
        check("P02 opened " + side + " no stale wall", c.call(SCENE, "trace_world", {"start": start, "end": end}) is None)
    check("P02 repeated rebuild has no duplicate components", similar(baseline_components, c.call(ACTOR, "get_components", {"actor": b})))
    regression = own("P03A_ShellRegression", "/Script/RoguelitePrototype.RogueliteParametricRoom", 75000, 0)
    for side in ("North", "South", "East", "West"):
        c.door(regression, side, bEnabled=False)
    regression_components = c.call(ACTOR, "get_components", {"actor": regression})
    for length, width, height in ((2000, 2000, 400), (3000, 2500, 500), (3000, 2500, 600), (2000, 2000, 400)):
        c.set(regression, {"RoomLength": length, "RoomWidth": width, "RoomHeight": height})
        for nx, ny in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            start = {"x": 75000, "y": 0, "z": height/2}
            end = {"x": 75000+nx*4000, "y": ny*4000, "z": height/2}
            expected = length/2 if nx else width/2
            distance = c.call(SCENE, "trace_world", {"start": start, "end": end})
            check(f"P01 {length}x{width} h{height} wall {nx}/{ny}", isinstance(distance, (int, float)) and abs(distance-expected) < 0.2, distance)
        distance = c.call(SCENE, "trace_world", {"start": {"x": 75000, "y": 0, "z": 100}, "end": {"x": 75000, "y": 0, "z": -100}})
        check("P01 floor collision", isinstance(distance, (int, float)) and abs(distance-100) < 0.2, distance)
    check("P01 shell repeated changes fixed component set", similar(regression_components, c.call(ACTOR, "get_components", {"actor": regression})))
    check("P01 old expanded wall collision removed", c.call(SCENE, "trace_world", {
        "start": {"x": 76510, "y": -500, "z": 100}, "end": {"x": 76510, "y": 500, "z": 100}}) is None)
    success("P02 opening/lintel/floor/capsule regression", 0)
    evidence["navigation"] = field(c.props(assembly, ["LastResult"])["LastResult"], "Navigation")
    if str(evidence["navigation"]).split("::")[-1] != "Passed":
        evidence["not_tested"].append("Navigation connectivity: no usable capsule-sized NavMesh at endpoints; not a navigation pass.")
    saved = {"a": c.props(a, ["RoomLength", "RoomWidth", "RoomHeight", "NorthDoor"]),
        "b": c.props(b, ["RoomLength", "RoomWidth", "RoomHeight", "SouthDoor"]), "transform_b": c.transform(b), "id": field(connections()[0], "Id")}
    check("save only new P03A map", c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    c.call(SCENE, "load_level", {"level_path": "/Engine/Maps/Templates/Template_Default"})
    c.call(SCENE, "load_level", {"level_path": MAP})
    a, b, assembly = (c.find(n)[0] for n in ("P03A_RoomA", "P03A_RoomB", "P03A_Assembly"))
    check("reload retained parameters/offsets", similar(saved["a"], c.props(a, list(saved["a"]))) and similar(saved["b"], c.props(b, list(saved["b"]))))
    check("reload retained transform and logical connection ID", similar(saved["transform_b"], c.transform(b)) and saved["id"] == field(connections()[0], "Id"))
    check("reload marks collision result unverified", not field(connections()[0], "bGeometryValid") and str(field(connections()[0], "Navigation")).split("::")[-1] == "NotChecked")
    result = act("Revalidate Connections")
    check("reloaded map actual revalidation", field(result, "bSucceeded"), result)
    c.call(APP, "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 1,
        "startTransform": {"location": {"x": 50000, "y": 0, "z": 200}}}})
    check("new map PIE running", c.call(APP, "IsPIERunning") is True)
    evidence["pie_game_modes"] = c.find("RogueliteDevGameMode")
    evidence["pie_castle_directors"] = c.find("CastleDirector")
    check("independent PIE GameMode; no CastleDirector", bool(evidence["pie_game_modes"]) and not evidence["pie_castle_directors"])
    c.call(APP, "StopPIE")
    check("PIE stopped", c.call(APP, "IsPIERunning") is False)
    c.call(APP, "SelectActors", {"actors": [assembly]})
    c.call(APP, "SetCameraTransform", {"transform": {"location": {"x": 48700, "y": -3800, "z": 3800}, "rotation": {"pitch": -38, "yaw": 48, "roll": 0}}})
    try:
        capture = c.call(APP, "CaptureViewport", {"bShowUI": False})
        data = base64.b64decode(capture["image"]["data"])
        check("viewport PNG", data.startswith(b"\x89PNG"))
        with (HERE / (args.run_name + "-assembly.png")).open("xb") as image:
            image.write(data)
    except Exception as error:
        evidence["not_tested"].append("Viewport capture: " + repr(error))
    c.call(ASSET, "save_assets", {"asset_paths": [MAP]})
    evidence["completed"] = True
except Exception:
    evidence["error"] = traceback.format_exc()
    print(evidence["error"], flush=True)
finally:
    evidence["calls"] = c.calls
    with report.open("x", encoding="utf-8") as file:
        json.dump(evidence, file, ensure_ascii=False, indent=2)
if not evidence.get("completed"):
    raise SystemExit(1)
