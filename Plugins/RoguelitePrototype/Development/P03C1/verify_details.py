"""One-shot C1 Details input check, limited to the owned fixture and map."""
import json
import re
import time
from corridor_mcp import Client, HERE, MAP, SCENE, ACTOR, ASSET, SLATE, field, own_map

report = HERE / "details-validation.json"
if report.exists():
    raise RuntimeError("Evidence exists; inspect it before any retry")
c = Client()
evidence = {"checks": []}
observer = None


def check(name, condition, detail=None):
    evidence["checks"].append({"name": name, "passed": bool(condition), "detail": detail})
    if not condition:
        raise AssertionError(name + ": " + str(detail))


def snapshot():
    return c.call(SLATE, "Snapshot", {"ref": root, "maxDepth": 80, "bIncludeSourceLocations": False})


def control(s, role):
    lines = s.splitlines()
    index = next(i for i, line in enumerate(lines) if 'generic "Corridor Length\\r' in line)
    return next(re.search(r"\[ref=([^\]]+)\]", line).group(1)
        for line in lines[index + 1:index + 9] if line.strip().startswith(role))


def click(ref):
    c.call(SLATE, "Hover", {"ref": ref})
    time.sleep(.25)
    c.call(SLATE, "Click", {"ref": ref, "button": "left", "doubleClick": False,
        "modifiers": {"bShift": False, "bCtrl": False, "bAlt": False, "bCmd": False}})
    time.sleep(.2)


def type_value(value):
    s = snapshot()
    if not any(line.strip().startswith("textbox") and "[focused]" in line for line in s.splitlines()):
        click(control(s, "slider"))
        c.call(SLATE, "PressKey", {"key": "Enter"})
        time.sleep(.25)
        s = snapshot()
    ref = control(s, "textbox")
    click(ref)
    c.call(SLATE, "PressKey", {"key": "Ctrl+A"})
    check("real numeric Type " + str(value), c.call(SLATE, "Type",
        {"ref": ref, "text": str(value), "submit": True}) is True)
    time.sleep(.25)


try:
    own_map(c)
    actor = c.find("P03C1_Corridor")[0]
    before = c.props(actor, ["CorridorLength", "CorridorWidth", "CorridorHeight", "WallThickness", "FloorThickness"])
    check("owned final authoring state", before == {"CorridorLength": 2000, "CorridorWidth": 500,
        "CorridorHeight": 350, "WallThickness": 25, "FloorThickness": 30}, before)
    roots = c.call(SLATE, "Snapshot", {"ref": "", "maxDepth": 1, "bIncludeSourceLocations": False})
    root = re.search(r"window.*\[ref=([^\]]+)\]", roots).group(1)
    observer = c.call(SLATE, "Observe", {"ref": root, "maxDepth": 80})
    time.sleep(.2)
    type_value(2200)
    p = c.props(actor, [*before, "StartSocket", "EndSocket", "ValidationWarnings"])
    check("Details input updates length only", p["CorridorLength"] == 2200
        and all(p[k] == before[k] for k in before if k != "CorridorLength"), p)
    check("Details input automatically rebuilds endpoints", field(field(p["StartSocket"], "DoorSocket"), "LocalPosition")["x"] == -1100
        and field(field(p["EndSocket"], "DoorSocket"), "WorldPosition")["x"] == 91100, p)
    hit = c.call(SCENE, "trace_world", {"start": {"x": 90980, "y": 175, "z": 100},
        "end": {"x": 91005, "y": 175, "z": 100}})
    check("old end-frame collision removed after UI edit", hit is None, hit)
    check("UI edit no warning or duplicate components", not p["ValidationWarnings"]
        and len(c.call(ACTOR, "get_components", {"actor": actor})) == 13)
    type_value(2000)
    p = c.props(actor, [*before, "StartSocket", "EndSocket"])
    check("same UI input restores saved dimensions", all(p[k] == before[k] for k in before), p)
    hit = c.call(SCENE, "trace_world", {"start": {"x": 91100, "y": 175, "z": 100},
        "end": {"x": 90980, "y": 175, "z": 100}})
    check("restored end-frame collision", hit is not None and abs(hit - 100) < .1, hit)
    check("save only C1 map", c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is True)
    check("C1 clean", c.call(ASSET, "is_dirty", {"asset_path": MAP}) is False)
    evidence["completed"] = True
finally:
    if observer is not None:
        c.call(SLATE, "Unobserve", {"identifier": observer})
    evidence["calls"] = c.calls
    with report.open("x", encoding="utf-8") as f:
        json.dump(evidence, f, ensure_ascii=False, indent=2)
    print(json.dumps({"completed": evidence.get("completed", False), "checks": len(evidence["checks"]),
        "passed": sum(x["passed"] for x in evidence["checks"]), "calls": len(c.calls)}))
