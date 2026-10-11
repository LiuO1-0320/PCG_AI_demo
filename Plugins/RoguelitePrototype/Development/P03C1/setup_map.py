"""Run once after current official schemas have been inspected. Never overwrite any map."""
import json
from corridor_mcp import Client, HERE, MAP, SOURCE_MAP, SCENE, ASSET, APP

report = HERE / "map-setup.json"
if report.exists():
    raise RuntimeError("Setup evidence exists; inspect before any retry")
c = Client()
evidence = {}
try:
    if c.call(SCENE, "get_current_level") != SOURCE_MAP or c.call(APP, "IsPIERunning"):
        raise RuntimeError("Source not idle")
    if c.call(ASSET, "is_dirty", {"asset_path": SOURCE_MAP}):
        raise RuntimeError("Source dirty")
    if c.call(ASSET, "exists", {"path": MAP}):
        raise RuntimeError("C1 map exists; refusing overwrite")
    if c.call(ASSET, "duplicate", {"path": SOURCE_MAP, "new_path": MAP}) is not True:
        raise RuntimeError("Duplicate failed")
    if c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is not True:
        raise RuntimeError("Save failed")
    c.call(SCENE, "load_level", {"level_path": MAP})
    if c.call(SCENE, "get_current_level") != MAP:
        raise RuntimeError("C1 map did not load")
    corridor = c.call(SCENE, "add_to_scene_from_class", {
        "actor_type": {"refPath": "/Script/RoguelitePrototype.RogueliteParametricCorridor"}, "name": "P03C1_Corridor",
        "xform": {"location": {"x": 90000, "y": 0, "z": 0}, "rotation": {"pitch": 0, "yaw": 0, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}}})
    if not corridor:
        raise RuntimeError("Compiled C1 class unavailable")
    if c.call(ASSET, "save_assets", {"asset_paths": [MAP]}) is not True:
        raise RuntimeError("Fixture save failed")
    evidence.update(corridor=corridor, completed=True)
finally:
    evidence["calls"] = c.calls
    with report.open("x", encoding="utf-8") as f:
        json.dump(evidence, f, ensure_ascii=False, indent=2)
