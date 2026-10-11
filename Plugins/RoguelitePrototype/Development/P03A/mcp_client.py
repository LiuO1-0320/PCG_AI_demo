"""Sequential localhost calls to the real Epic Editor MCP; never imports old validators."""
import json
import math
import re
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
MAP = "/Game/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev"
SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
APP = "EditorToolset.EditorAppToolset"
SLATE = "SlateInspectorToolset.SlateInspectorToolset"
LOGS = "EditorToolset.LogsToolset"

def field(mapping, name):
    return mapping[next(k for k in mapping if k.casefold() == name.casefold())]

def similar(a, b):
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(similar(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(similar(x, y) for x, y in zip(a, b))
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, abs_tol=0.001)
    return a == b

class Client:
    def __init__(self):
        self.request_id = 0
        self.session = None
        self.calls = []
        self.properties = {}
        self.rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "RogueliteP03A", "version": "1.0"}})
        self.rpc("notifications/initialized", {}, True)

    def rpc(self, method, params=None, notification=False):
        self.request_id += 1
        payload = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        if not notification:
            payload["id"] = self.request_id
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": "2025-11-25"}
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        req = urllib.request.Request("http://127.0.0.1:8001/mcp", json.dumps(payload).encode(), headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=60) as response:
            self.session = response.headers.get("Mcp-Session-Id", self.session)
            raw = response.read().decode()
        if not raw:
            return {}
        if raw.lstrip().startswith(("event:", "data:")):
            result = next(json.loads(line[5:]) for line in raw.splitlines()
                if line.startswith("data:") and json.loads(line[5:]).get("id") == self.request_id)
        else:
            result = json.loads(raw)
        if "error" in result:
            raise RuntimeError(result["error"])
        return result.get("result", {})

    def call(self, toolset, tool, arguments=None):
        params = {"toolset_name": toolset, "tool_name": tool, "arguments": arguments or {}}
        result = self.rpc("tools/call", {"name": "call_tool", "arguments": params})
        if result.get("isError"):
            self.calls.append({**params, "error": result})
            raise RuntimeError(result)
        content = next((x["text"] for x in result.get("content", []) if x.get("type") == "text"), "{}")
        value = json.loads(content).get("returnValue")
        # Image payloads are handled by a caller, never emitted as log text.
        recorded = value
        if isinstance(value, dict) and "image" in value:
            recorded = {**value, "image": {"mimeType": value["image"]["mimeType"], "bytes_base64": len(value["image"]["data"])}}
        if isinstance(value, dict) and "data" in value and "mimeType" in value:
            recorded = {"mimeType": value["mimeType"], "bytes_base64": len(value["data"])}
        self.calls.append({**params, "returnValue": recorded})
        return value

    def discover(self, obj):
        raw = self.call(OBJECT, "list_properties", {"instance": obj})
        self.properties[obj["refPath"]] = json.loads(raw) if isinstance(raw, str) else raw
        return self.properties[obj["refPath"]]

    def props(self, obj, names):
        schema = self.properties.get(obj["refPath"]) or self.discover(obj)
        actual = [next(k for k in schema if k.casefold() == n.casefold()) for n in names]
        raw = self.call(OBJECT, "get_properties", {"instance": obj, "properties": actual})
        value = json.loads(raw) if isinstance(raw, str) else raw
        return dict(zip(names, [value[k] for k in actual]))

    def set(self, obj, values):
        schema = self.properties.get(obj["refPath"]) or self.discover(obj)
        actual = {next(k for k in schema if k.casefold() == n.casefold()): v for n, v in values.items()}
        result = self.call(OBJECT, "set_properties", {"instance": obj, "values": json.dumps(actual, ensure_ascii=False)})
        if result is not True:
            raise RuntimeError("set_properties failed: " + str(result))

    def door(self, room, side, **changes):
        value = self.props(room, [side + "Door"])[side + "Door"]
        for name, new in changes.items():
            value[next(k for k in value if k.casefold() == name.casefold())] = new
        self.set(room, {side + "Door": value})

    def find(self, name):
        return self.call(SCENE, "find_actors", {"name": name, "tag": "", "collision_channels": []})

    def transform(self, actor):
        return self.call(ACTOR, "get_actor_transform", {"actor": actor})

    def move(self, actor, x, y, z=0, yaw=0, scale=1):
        value = {"location": {"x": x, "y": y, "z": z}, "rotation": {"pitch": 0, "yaw": yaw, "roll": 0},
            "scale": {"x": scale, "y": scale, "z": scale}}
        return self.call(ACTOR, "set_actor_transform", {"actor": actor, "xform": value})

    def button(self, actor, text):
        self.call(APP, "SelectActors", {"actors": [actor]})
        windows = self.call(SLATE, "Windows", {"action": "list", "index": -1})
        self.call(SLATE, "Windows", {"action": "select", "index": 0})
        roots = self.call(SLATE, "Snapshot", {"ref": "", "maxDepth": 1, "bIncludeSourceLocations": False})
        root = re.search(r"window.*\[ref=([^\]]+)\]", roots).group(1)
        observer = self.call(SLATE, "Observe", {"ref": root, "maxDepth": 80})
        time.sleep(0.2)  # Observer assigns deep refs on its real ~100ms editor tick.
        snapshot = self.call(SLATE, "Snapshot", {"ref": root, "maxDepth": 80, "bIncludeSourceLocations": False})
        lines = snapshot.splitlines()
        # Use only live widget references observed in the current snapshot.
        matching = [i for i, line in enumerate(lines) if text in line and line.lstrip().startswith("button")]
        for i in matching:
            for line in reversed(lines[max(0, i-4):i+1]):
                if ("SButton" in line or line.lstrip().startswith("button")) and (match := re.search(r"\[ref=([^\]]+)\]", line)):
                    self.call(SLATE, "Hover", {"ref": match.group(1)})
                    time.sleep(0.2)  # Give Slate a tick to update hit testing when switching buttons.
                    value = self.call(SLATE, "Click", {"ref": match.group(1), "button": "left", "doubleClick": False,
                        "modifiers": {"bShift": False, "bCtrl": False, "bAlt": False, "bCmd": False}})
                    time.sleep(0.25)  # Let the editor complete the real UI action before reading diagnostics.
                    self.call(SLATE, "Unobserve", {"identifier": observer})
                    return value
        raise RuntimeError("CallInEditor button not found: " + text + "\n" + "\n".join(lines[-80:]))
