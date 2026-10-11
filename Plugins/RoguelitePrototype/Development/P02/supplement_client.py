"""One sequential official MCP call against the held, task-owned P02 editor."""
import argparse
import base64
import json
import urllib.request
from pathlib import Path

EVIDENCE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("toolset")
parser.add_argument("tool")
parser.add_argument("arguments")
parser.add_argument("output_name")
args = parser.parse_args()
if not args.output_name.replace("-", "").isalnum():
    raise ValueError("Invalid evidence name")
output = EVIDENCE / (args.output_name + ".json")
if output.exists():
    raise RuntimeError("Evidence exists; refusing overwrite")
session = None
request_id = 0


def rpc(method, params, notification=False):
    global session, request_id
    request_id += 1
    payload = {"jsonrpc": "2.0", "method": method, "params": params}
    if not notification:
        payload["id"] = request_id
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
               "MCP-Protocol-Version": "2025-11-25"}
    if session:
        headers["Mcp-Session-Id"] = session
    request = urllib.request.Request("http://127.0.0.1:8001/mcp", json.dumps(payload).encode(), headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=60) as response:
        session = response.headers.get("Mcp-Session-Id", session)
        raw = response.read().decode()
    if not raw:
        return {}
    if raw.lstrip().startswith(("data:", "event:")):
        decoded = next(json.loads(line[5:].strip()) for line in raw.splitlines()
                       if line.startswith("data:") and json.loads(line[5:].strip()).get("id") == request_id)
    else:
        decoded = json.loads(raw)
    if "error" in decoded:
        raise RuntimeError(decoded["error"])
    return decoded.get("result", {})


def call(toolset, tool, arguments):
    result = rpc("tools/call", {"name": "call_tool", "arguments": {
        "toolset_name": toolset, "tool_name": tool, "arguments": arguments}})
    if result.get("isError"):
        return result
    return json.loads(next(item["text"] for item in result["content"] if item["type"] == "text"))


rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
    "clientInfo": {"name": "P02Supplement", "version": "1.0"}})
rpc("notifications/initialized", {}, True)
level = call("editor_toolset.toolsets.scene.SceneTools", "get_current_level", {})
if level.get("returnValue") != "/Game/Roguelite/Maps/L_Roguelite_DoorSocket_Dev":
    raise RuntimeError("Refusing supplemental action outside P02 test map")
parameters = json.loads(args.arguments)
result = call(args.toolset, args.tool, parameters)
value = result.get("returnValue")
image_container = value
if isinstance(value, dict) and isinstance(value.get("image"), dict):
    image_container = value["image"]
if isinstance(image_container, dict) and "data" in image_container and "mimeType" in image_container:
    data = base64.b64decode(image_container["data"])
    image_path = EVIDENCE / (args.output_name + ".png")
    with image_path.open("xb") as image:
        image.write(data)
    metadata = {"mimeType": image_container["mimeType"], "bytes": len(data), "path": str(image_path)}
    if image_container is value:
        result["returnValue"] = metadata
    else:
        value["image"] = metadata
with output.open("x", encoding="utf-8") as file:
    json.dump({"toolset": args.toolset, "tool": args.tool, "arguments": parameters, "result": result}, file, ensure_ascii=False, indent=2)
print(json.dumps(result, ensure_ascii=False))
