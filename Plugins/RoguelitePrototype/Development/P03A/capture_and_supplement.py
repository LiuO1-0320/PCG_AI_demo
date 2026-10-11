"""Read final endpoints and capture the actual P03-A editor; save no existing map."""
import base64
import json
import argparse
from mcp_client import Client, HERE, MAP, SCENE, ACTOR, ASSET, APP, OBJECT, SLATE, field, similar

parser = argparse.ArgumentParser()
parser.add_argument("--run-name", default="supplement")
args = parser.parse_args()
report = HERE / (args.run_name + ".json")
if report.exists():
    raise RuntimeError("Evidence exists")
c = Client()
evidence = {}
try:
    if c.call(SCENE, "get_current_level") != MAP or c.call(APP, "IsPIERunning"):
        raise RuntimeError("Not idle on isolated P03A map")
    a, b, assembly = (c.find(n)[0] for n in ("P03A_RoomA", "P03A_RoomB", "P03A_Assembly"))
    evidence["final_rooms"] = {"a": c.props(a, ["RoomLength", "RoomWidth", "RoomHeight", "DoorSockets", "ManualPOIActors"]),
        "b": c.props(b, ["RoomLength", "RoomWidth", "RoomHeight", "DoorSockets", "ManualPOIActors"]),
        "transform_a": c.transform(a), "transform_b": c.transform(b)}
    evidence["result"] = c.props(assembly, ["LastResult", "Connections"])
    # Unsupported socket text is rejected by the real editor's enum conversion,
    # not a fabricated native TryAlign call with an invalid enum byte.
    before = c.props(assembly, ["SocketA", "SocketB", "Connections"])
    before_b = c.transform(b)
    try:
        rejected = c.call(OBJECT, "set_properties", {"instance": assembly, "values": json.dumps({"socketA": "MissingSocket"})})
        evidence["missing_socket_input"] = {"returnValue": rejected}
    except Exception as error:
        evidence["missing_socket_input"] = {"error": repr(error)}
    evidence["missing_socket_input"]["state_unchanged"] = similar(before, c.props(assembly, list(before))) and similar(before_b, c.transform(b))
    if not evidence["missing_socket_input"]["state_unchanged"]:
        raise RuntimeError("Invalid enum input unexpectedly changed state")
    c.call(APP, "SelectActors", {"actors": [assembly]})
    evidence["captures"] = []
    annotations = {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0, "maxLabelDistance": 0,
        "classFilter": {"refPath": "/Script/Engine.Actor"}, "maxLabels": 0}
    for name, location, rotation in (("overview", {"x": 48700, "y": -3800, "z": 3800}, {"pitch": -38, "yaw": 48, "roll": 0}),
        ("door-seam", {"x": 50400, "y": -450, "z": 180}, {"pitch": -4, "yaw": 36, "roll": 0})):
        transform = {"location": location, "rotation": rotation}
        c.call(APP, "SetCameraTransform", {"transform": transform})
        capture = c.call(APP, "CaptureViewport", {"captureTransform": transform, "annotations": annotations, "bShowUI": False})
        data = base64.b64decode(capture["image"]["data"])
        if not data.startswith(b"\x89PNG"):
            raise RuntimeError("Screenshot not PNG")
        path = HERE / (args.run_name + "-" + name + ".png")
        with path.open("xb") as output:
            output.write(data)
        evidence["captures"].append({"path": str(path), "camera": transform, "bytes": len(data)})
    evidence["map_dirty"] = c.call(ASSET, "is_dirty", {"asset_path": MAP})
    evidence["completed"] = True
finally:
    evidence["calls"] = c.calls
    with report.open("x", encoding="utf-8") as output:
        json.dump(evidence, output, ensure_ascii=False, indent=2)
print(json.dumps({"completed": evidence.get("completed"), "captures": evidence.get("captures"),
    "missing_socket": evidence.get("missing_socket_input"), "map_dirty": evidence.get("map_dirty")}, ensure_ascii=False))
