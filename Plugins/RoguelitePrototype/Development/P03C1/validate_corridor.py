"""P03-C1-only fixtures. Refuses existing evidence; NEVER run against P01/P02/P03A maps."""
import argparse
import json
import math
import traceback
from corridor_mcp import Client, HERE, MAP, SCENE, ACTOR, ASSET, APP, field, similar, own_map

parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="validation")
args = parser.parse_args()
report = HERE / (args.run_name + ".json")
if report.exists() or not args.run_name.replace("-", "").isalnum():
    raise RuntimeError("Existing/invalid evidence name; inspect before retry")
c = Client()
evidence = {"task": "P03-C1", "map": MAP, "checks": [], "cases": [], "not_tested": [
    "human interaction/UndoRedo", "Blueprint pure query invocation", "NavMesh", "runtime resizing after BeginPlay",
    "Room-Corridor-Room assembly/reservation", "fog/loading/encounter", "Game Target/packaging"]}


def check(name, passed, detail=None):
    evidence["checks"].append({"name": name, "passed": bool(passed), "detail": detail})
    print(name + (": PASS" if passed else ": FAIL"), flush=True)
    if not passed:
        raise AssertionError(name + ": " + str(detail))


def props():
    return c.props(corridor, ["CorridorLength", "CorridorWidth", "CorridorHeight", "WallThickness", "FloorThickness",
        "DoorWidth", "DoorHeight", "StartSocket", "EndSocket", "ValidationWarnings", "bHasPassageTestResult", "bPassageTestPassed"])


def components():
    return c.call(ACTOR, "get_components", {"actor": corridor})


def fixed(name):
    actual = components()
    check(name + " 13 fixed components", len(actual) == 13 and {json.dumps(v, sort_keys=True) for v in actual} == component_ids, actual)
    check(name + " Actor Scale 1", similar(field(c.transform(corridor), "scale"), {"x": 1, "y": 1, "z": 1}))


def set_values(values):
    c.set(corridor, values)
    p = props()
    check("raw authoring values retained " + str(values), all(similar(p[k], v) for k, v in values.items()), p)
    return p


def baseline():
    c.move(corridor, 90000, 0)
    return set_values({"CorridorLength": 1000, "CorridorWidth": 400, "CorridorHeight": 300,
        "WallThickness": 20, "FloorThickness": 20, "DoorWidth": 200, "DoorHeight": 250})


def socket_checks(name, length, yaw=0):
    p = props()
    for key, sign, endpoint in (("StartSocket", -1, "Start"), ("EndSocket", 1, "End")):
        outer = p[key]
        door = field(outer, "DoorSocket")
        local = {"x": sign * length / 2, "y": 0, "z": 0}
        r = math.radians(yaw)
        world = {"x": 90000 + local["x"] * math.cos(r), "y": local["x"] * math.sin(r), "z": 0}
        normal = {"x": sign * math.cos(r), "y": sign * math.sin(r), "z": 0}
        check(name + " " + key + " threshold/direction/spec", field(door, "bValid")
            and field(outer, "Endpoint") == endpoint and similar(field(door, "LocalPosition"), local)
            and similar(field(door, "WorldPosition"), world) and similar(field(door, "WorldOutwardDirection"), normal)
            and similar(field(door, "DoorWidth"), p["DoorWidth"]) and similar(field(door, "DoorHeight"), p["DoorHeight"]), outer)
        check(name + " " + key + " no claimed connection", field(outer, "bConnected") is False, outer)
    check(name + " no warning", not p["ValidationWarnings"], p)
    evidence["cases"].append({"name": name, "parameters_and_sockets": p})


def ray(name, start, end, expected=None):
    s = {"x": 90000 + start[0], "y": start[1], "z": start[2]}
    e = {"x": 90000 + end[0], "y": end[1], "z": end[2]}
    result = c.call(SCENE, "trace_world", {"start": s, "end": e})
    passed = result is None if expected is None else isinstance(result, (int, float)) and abs(result - expected) < 0.1
    check(name, passed, {"start": s, "end": e, "hit_distance_cm": result, "expected": expected})


def shell(name, length=1000, width=400, height=300, wall=20, floor=20, door_width=200, door_height=250):
    ray(name + " floor", (0, 0, 100), (0, 0, -100), 100)
    ray(name + " floor underside", (0, 0, -floor - 100), (0, 0, 20), 100)
    ray(name + " left wall", (0, 0, 100), (0, -width / 2 - wall - 100, 100), width / 2)
    ray(name + " right wall", (0, 0, 100), (0, width / 2 + wall + 100, 100), width / 2)
    ray(name + " ceiling underside", (0, 0, 100), (0, 0, height + wall + 100), height - 100)
    ray(name + " ceiling top", (0, 0, height + wall + 100), (0, 0, height - 50), 100)
    for sign, label in ((-1, "start"), (1, "end")):
        outside = sign * (length / 2 + 100)
        inside = sign * (length / 2 - wall - 100)
        for z in (1, door_height / 2, door_height - 1):
            ray(name + f" {label} opening z{z}", (outside, 0, z), (inside, 0, z))
        if width > door_width:
            y = (width + door_width) / 4
            for side in (-1, 1):
                ray(name + f" {label} solid frame side{side}", (outside, side * y, 100), (inside, side * y, 100), 100)
        if height > door_height:
            ray(name + " " + label + " lintel", (outside, 0, (height + door_height) / 2), (inside, 0, (height + door_height) / 2), 100)


def passage(name, expected=True):
    check(name + " native Check Passage button", c.button(corridor, "Check Passage") is True)
    p = c.props(corridor, ["bHasPassageTestResult", "bPassageTestPassed", "PassageTestEvidence"])
    check(name + " native Pawn capsule result", p["bHasPassageTestResult"] and p["bPassageTestPassed"] is expected, p)
    if expected:
        check(name + " four actual Sweep/Overlap records", len(p["PassageTestEvidence"]) == 4
            and all("Passed=1" in line for line in p["PassageTestEvidence"]), p)
    evidence["cases"].append({"name": name, "native_capsule": p})


try:
    own_map(c)
    found = c.find("P03C1_Corridor")
    check("one task corridor", len(found) == 1, found)
    corridor = found[0]
    component_ids = {json.dumps(v, sort_keys=True) for v in components()}
    check("initial 13 components", len(component_ids) == 13)
    baseline()
    socket_checks("default unequal corridor/opening", 1000)
    shell("default")
    passage("default")

    set_values({"CorridorLength": 2000})
    socket_checks("independent length 2000", 2000)
    shell("length 2000", length=2000)
    ray("old end frame removed on length increase", (450, 150, 125), (550, 150, 125))
    fixed("length expanded")
    set_values({"CorridorLength": 1000})
    ray("old long floor removed", (750, 0, 100), (750, 0, -100))
    ray("old long wall removed", (750, 0, 125), (750, 400, 125))
    ray("old long ceiling removed", (750, 0, 200), (750, 0, 400))

    set_values({"CorridorWidth": 600})
    socket_checks("independent width 600", 1000)
    shell("width 600", width=600)
    ray("old narrow sidewall removed", (0, 205, 125), (0, 225, 125))
    set_values({"CorridorWidth": 400})
    ray("old wide floor removed", (0, 250, 100), (0, 250, -100))
    ray("old wide wall removed", (0, 300, 125), (0, 320, 125))
    ray("old wide ceiling removed", (0, 250, 200), (0, 250, 400))

    set_values({"CorridorHeight": 450})
    socket_checks("independent height 450", 1000)
    shell("height 450", height=450)
    ray("old low ceiling removed", (0, 0, 300), (0, 0, 320))
    set_values({"CorridorHeight": 300})
    ray("old tall sidewall removed", (0, 0, 400), (0, 400, 400))
    ray("old high ceiling removed", (0, 0, 450), (0, 0, 470))

    set_values({"WallThickness": 35, "FloorThickness": 40})
    shell("independent thickness", wall=35, floor=40)
    set_values({"WallThickness": 20, "FloorThickness": 20})
    ray("old thick wall removed", (0, 225, 100), (0, 240, 100))
    ray("old thick ceiling removed", (0, 0, 325), (0, 0, 340))
    ray("old thick floor removed", (0, 0, -35), (0, 0, -25))

    for n in range(4):
        set_values({"CorridorLength": 1000 + n * 200, "CorridorWidth": 400 + n * 50, "CorridorHeight": 300 + n * 40})
        fixed("repeat resize " + str(n))
    baseline()
    fixed("restored default")

    set_values({"CorridorWidth": 200, "CorridorHeight": 250})
    shell("equal full opening", width=200, height=250)
    socket_checks("equal full opening", 1000)
    passage("equal full opening")
    fixed("zero size frame segments disabled")
    baseline()

    for invalid in ({"CorridorLength": -20}, {"CorridorWidth": 0}, {"CorridorHeight": 80}, {"WallThickness": 0}, {"FloorThickness": -3}, {"WallThickness": 600}):
        p = set_values(invalid)
        check("invalid shell warning and invalid sockets " + str(invalid), p["ValidationWarnings"]
            and all(not field(field(p[k], "DoorSocket"), "bValid") for k in ("StartSocket", "EndSocket")), p)
        ray("invalid shell leaves no previous wall", (0, 0, 125), (0, 400, 125))
        ray("invalid shell leaves no previous floor", (0, 0, 100), (0, 0, -100))
        baseline()

    for invalid in ({"DoorWidth": 500}, {"DoorHeight": 350}, {"DoorWidth": 0}, {"DoorHeight": -5}):
        p = set_values(invalid)
        check("invalid opening diagnosed/preserved " + str(invalid), p["ValidationWarnings"]
            and all(not field(field(p[k], "DoorSocket"), "bValid") for k in ("StartSocket", "EndSocket")), p)
        for sign in (-1, 1):
            ray("invalid opening sealed end " + str(sign), (sign * 600, 0, 100), (sign * 400, 0, 100), 100)
        baseline()
        ray("restored opening no old full cap", (-600, 0, 100), (-400, 0, 100))

    set_values({"DoorWidth": 60})
    passage("capsule cannot fit narrow legal aperture", False)
    baseline()
    for yaw in (90, 180, 270):
        c.move(corridor, 90000, 0, yaw=yaw)
        check("explicit refresh after MCP transform", c.button(corridor, "Rebuild Corridor") is True)
        socket_checks("yaw " + str(yaw), 1000, yaw)
        passage("yaw " + str(yaw))
    baseline()

    # Only this test-owned obstacle is moved; no old manual POI or source layout is touched.
    blockers = c.find("P03C1_Blocker")
    check("unique own capsule obstacle", not blockers, blockers)
    blocker = c.call(SCENE, "add_to_scene_from_asset", {"asset_path": "/Engine/BasicShapes/Cube.Cube", "name": "P03C1_Blocker",
        "xform": {"location": {"x": 90000, "y": 20, "z": 100}, "scale": {"x": 0.1, "y": 0.1, "z": 0.1}}})
    ray("thin center ray misses off-center blocker", (-600, 0, 100), (600, 0, 100))
    passage("capsule detects off-center obstacle", False)
    c.move(blocker, 100000, 5000, z=100, scale=0.1)
    passage("obstacle parked restores passage")
    set_values({"CorridorLength": 2000, "CorridorWidth": 500, "CorridorHeight": 350, "WallThickness": 25, "FloorThickness": 30})
    socket_checks("saved final", 2000)
    shell("saved final", length=2000, width=500, height=350, wall=25, floor=30)
    passage("saved final")
    fixed("saved final")
    saved = props()
    check("save only C1 map", c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("saved map clean", c.call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    c.call(SCENE, "load_level", {"level_path": "/Engine/Maps/Templates/Template_Default"})
    check("C1 map really unloaded", c.call(SCENE, "get_current_level") != MAP)
    c.call(SCENE, "load_level", {"level_path": MAP})
    own_map(c)
    corridor = c.find("P03C1_Corridor")[0]
    reloaded = props()
    names = ["CorridorLength", "CorridorWidth", "CorridorHeight", "WallThickness", "FloorThickness", "DoorWidth", "DoorHeight", "StartSocket", "EndSocket"]
    check("reload preserves parameters and computed sockets", all(similar(saved[k], reloaded[k]) for k in names), reloaded)
    check("reload invalidates old capsule result", reloaded["bHasPassageTestResult"] is False)
    component_ids = {json.dumps(v, sort_keys=True) for v in components()}
    check("reload 13 components", len(component_ids) == 13)
    shell("reloaded", length=2000, width=500, height=350, wall=25, floor=30)
    passage("reloaded")
    dependencies = c.call(ASSET, "get_dependencies", {"asset_path": MAP})
    check("no Castle/Demo asset dependency", not any("CastleDemo" in str(d) or "/Game/Demo" in str(d) for d in dependencies), dependencies)
    start_result = c.call(APP, "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 1,
        "startTransform": {"location": {"x": 88600, "y": 0, "z": 150}, "rotation": {"pitch": 0, "yaw": 0, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}}}})
    evidence["pie_start_result"] = start_result
    check("PIE is actually running", c.call(APP, "IsPIERunning") is True)
    evidence["pie_stop_result"] = c.call(APP, "StopPIE")
    check("PIE actually stopped", c.call(APP, "IsPIERunning") is False)
    check("final C1 save", c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("final C1 clean", c.call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    evidence["final"] = {"properties": props(), "components": components(), "transform": c.transform(corridor), "dependencies": dependencies}
    evidence["completed"] = True
except Exception:
    evidence["error"] = traceback.format_exc()
    raise
finally:
    evidence["calls"] = c.calls
    with report.open("x", encoding="utf-8") as f:
        json.dump(evidence, f, ensure_ascii=False, indent=2)
