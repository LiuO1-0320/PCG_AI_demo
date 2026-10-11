"""Read back the owned C1 fixture, run its explicit sweep, capture real Editor images."""
import base64
import json
from corridor_mcp import Client, HERE, MAP, APP, ASSET, own_map

report = HERE / "final-editor-readback.json"
if report.exists():
    raise RuntimeError("Evidence already exists; inspect before retry")
c = Client()
own_map(c)
a = c.find("P03C1_Corridor")[0]
assert c.button(a, "Check Passage") is True
p = c.props(a, ["CorridorLength", "CorridorWidth", "CorridorHeight", "WallThickness", "FloorThickness",
    "DoorWidth", "DoorHeight", "StartSocket", "EndSocket", "ValidationWarnings", "bHasPassageTestResult",
    "bPassageTestPassed", "PassageTestEvidence"])
assert p["bHasPassageTestResult"] and p["bPassageTestPassed"]
annotations = {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0, "maxLabelDistance": 0,
    "classFilter": {"refPath": "/Script/Engine.Actor"}, "maxLabels": 0}
images = []
for name, loc, pitch, yaw in (("corridor-overview", (88000, -2300, 1800), -29, 43),
        ("corridor-start-opening", (88600, 0, 150), 0, 0)):
    v = c.call(APP, "CaptureViewport", {"captureTransform": {"location": dict(zip(("x", "y", "z"), loc)),
        "rotation": {"pitch": pitch, "yaw": yaw, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}},
        "annotations": annotations, "bShowUI": False})
    data = base64.b64decode(v["image"]["data"], validate=True)
    assert data.startswith(b"\x89PNG\r\n\x1a\n")
    target = HERE / (name + ".png")
    with target.open("xb") as f:
        f.write(data)
    images.append({"path": str(target), "bytes": len(data), **{k: x for k, x in v.items() if k != "image"}})
clean = c.call(ASSET, "is_dirty", {"asset_path": MAP}) is False
with report.open("x", encoding="utf-8") as f:
    json.dump({"parameters": p, "images": images, "map_clean": clean, "calls": c.calls}, f, ensure_ascii=False, indent=2)
assert clean
print(json.dumps({"capsule_passed": p["bPassageTestPassed"], "map_clean": clean,
    "images": [{"path": i["path"], "bytes": i["bytes"]} for i in images], "calls": len(c.calls)}))
