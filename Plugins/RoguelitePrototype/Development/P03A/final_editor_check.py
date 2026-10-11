"""Final readback + seam floor traces; only saves the task's new assembly map."""
import json
import argparse
from mcp_client import Client, HERE, MAP, SCENE, ACTOR, ASSET, APP, field

parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="final-editor-check")
args = parser.parse_args()
output = HERE / (args.run_name + ".json")
if output.exists():
    raise RuntimeError("Evidence exists")
c = Client()
evidence = {"checks": []}
def check(name, value, detail=None):
    evidence["checks"].append({"name": name, "passed": bool(value), "detail": detail})
    print(name + (": PASS" if value else ": FAIL"), flush=True)
    if not value:
        raise AssertionError(name)
try:
    check("new map idle", c.call(SCENE, "get_current_level") == MAP and not c.call(APP, "IsPIERunning"))
    a, b, assembly = (c.find(n)[0] for n in ("P03A_RoomA", "P03A_RoomB", "P03A_Assembly"))
    # Detect any gap in floor collision around the joined outer-wall seam.
    # Ray hits around the seam within the permitted precision tolerance; the
    # exact edge is covered by native capsule ground sweeps in LastResult.
    for x in (51019.999, 51020.001):
        for y in (-99, 0, 99):
            distance = c.call(SCENE, "trace_world", {"start": {"x": x, "y": y, "z": 100},
                "end": {"x": x, "y": y, "z": -100}})
            check(f"continuous seam floor x{x} y{y}", isinstance(distance, (int, float)) and abs(distance - 100) < 0.1, distance)
    evidence["dependencies"] = c.call(ASSET, "get_dependencies", {"asset_path": MAP})
    check("no Castle/Demo asset dependency", not any("/Game/Demo" in str(d) or "CastleDemo" in str(d) for d in evidence["dependencies"]), evidence["dependencies"])
    evidence["rooms"] = {"a": c.transform(a), "b": c.transform(b),
        "a_components": c.call(ACTOR, "get_components", {"actor": a}), "b_components": c.call(ACTOR, "get_components", {"actor": b})}
    check("fixed 18 room components each", len(evidence["rooms"]["a_components"]) == 18 and len(evidence["rooms"]["b_components"]) == 18)
    c.call(APP, "SelectActors", {"actors": [assembly]})
    check("explicit revalidation", c.button(assembly, "Revalidate Connections") is True)
    evidence["result"] = c.props(assembly, ["LastResult", "Connections"])
    check("geometry and capsule still passed", field(evidence["result"]["LastResult"], "bSucceeded"))
    c.call(APP, "SetCameraTransform", {"transform": {"location": {"x": 48700, "y": -3800, "z": 3800}, "rotation": {"pitch": -38, "yaw": 48, "roll": 0}}})
    check("save only P03A map", c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("P03A map saved clean", c.call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    evidence["completed"] = True
finally:
    evidence["calls"] = c.calls
    with output.open("x", encoding="utf-8") as file:
        json.dump(evidence, file, ensure_ascii=False, indent=2)
