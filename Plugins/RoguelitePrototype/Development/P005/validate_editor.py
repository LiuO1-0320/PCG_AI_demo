"""Validate P00.5 through Epic's localhost MCP in a temporary null-RHI editor."""
import json
import socket
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
EVIDENCE = Path(__file__).resolve().parent
EDITOR = Path("D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe")
MAP = "/Game/Roguelite/Maps/L_Roguelite_Dev"
PORT = 8001
URL = f"http://127.0.0.1:{PORT}/mcp"
session_id = None
request_id = 0
evidence = {"project": str(ROOT / "testfordemo.uproject"), "map": MAP, "transport": URL, "null_rhi": True}


def rpc(method, params=None, notification=False):
    global request_id, session_id
    request_id += 1
    payload = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if not notification:
        payload["id"] = request_id
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2025-11-25"}
    if session_id:
        headers["Mcp-Session-Id"] = session_id
    request = urllib.request.Request(URL, json.dumps(payload).encode(), headers=headers, method="POST")
    with urllib.request.urlopen(request, timeout=60) as response:
        session_id = response.headers.get("Mcp-Session-Id", session_id)
        raw = response.read().decode()
    if not raw:
        return {}
    if raw.lstrip().startswith("data:") or raw.lstrip().startswith("event:"):
        messages = [json.loads(line[5:].strip()) for line in raw.splitlines() if line.startswith("data:")]
        result = next(item for item in messages if item.get("id") == request_id)
    else:
        result = json.loads(raw)
    if "error" in result:
        raise RuntimeError(result["error"])
    return result.get("result", {})


def call(toolset, name, args=None):
    result = rpc("tools/call", {"name": "call_tool", "arguments": {"toolset_name": toolset, "tool_name": name, "arguments": args or {}}})
    if result.get("isError"):
        raise RuntimeError(result)
    text = "\n".join(item.get("text", "") for item in result.get("content", []) if item.get("type") == "text")
    try:
        value = json.loads(text).get("returnValue")
    except (ValueError, AttributeError):
        value = text
    print(f"{name}: {json.dumps(value, ensure_ascii=False)}", flush=True)
    return value


with socket.socket() as probe:
    if probe.connect_ex(("127.0.0.1", PORT)) == 0:
        raise RuntimeError("Validation port is already in use; refusing to reuse another server.")
if (EVIDENCE / "mcp-verification.json").exists():
    raise RuntimeError("Validation evidence already exists; refusing overwrite.")

startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
process = subprocess.Popen([
    str(EDITOR), str(ROOT / "testfordemo.uproject"), MAP,
    "-nullrhi", "-unattended", "-nosplash", "-nosound", "-nop4",
    "-ModelContextProtocolStartServer", f"-ModelContextProtocolPort={PORT}",
    "-ini:Engine:[HTTPServer.Listeners]:DefaultBindAddress=127.0.0.1",
    f"-abslog={EVIDENCE / 'editor-pie.log'}",
], startupinfo=startup, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
evidence["temporary_editor_pid"] = process.pid
print(f"Temporary editor PID={process.pid}; waiting for localhost MCP", flush=True)
pie_started = False
try:
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Temporary editor exited before MCP was ready: {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", PORT), timeout=1):
                break
        except OSError:
            time.sleep(2)
    else:
        raise TimeoutError("Local MCP startup timed out")
    evidence["initialize"] = rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "RogueliteP005Validation", "version": "1.0"}})
    rpc("notifications/initialized", notification=True)
    discovered = rpc("tools/list").get("tools", [])
    evidence["meta_tools"] = [{"name": tool["name"], "inputSchema": tool.get("inputSchema")} for tool in discovered]
    assert {"list_toolsets", "describe_toolset", "call_tool"}.issubset({tool["name"] for tool in discovered})
    for name in ("PluginToolset.PluginToolset", "editor_toolset.toolsets.scene.SceneTools", "editor_toolset.toolsets.asset.AssetTools", "EditorToolset.EditorAppToolset", "EditorToolset.LogsToolset"):
        described = rpc("tools/call", {"name": "describe_toolset", "arguments": {"toolset_name": name}})
        if described.get("isError"):
            raise RuntimeError(described)
    evidence["plugin_enabled"] = call("PluginToolset.PluginToolset", "IsEnabled", {"pluginName": "RoguelitePrototype"})
    evidence["plugin_info"] = call("PluginToolset.PluginToolset", "GetPluginInfo", {"pluginName": "RoguelitePrototype"})
    evidence["level"] = call("editor_toolset.toolsets.scene.SceneTools", "get_current_level")
    assert evidence["plugin_enabled"] is True and evidence["level"] == MAP
    evidence["dependencies"] = call("editor_toolset.toolsets.asset.AssetTools", "get_dependencies", {"asset_path": MAP})
    assert not any("CastleDemo" in dep or dep.startswith("/Game/Demo") for dep in evidence["dependencies"])
    assert call("EditorToolset.EditorAppToolset", "IsPIERunning") is False
    evidence["start_pie"] = call("EditorToolset.EditorAppToolset", "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 2}})
    pie_started = True
    evidence["pie_running"] = call("EditorToolset.EditorAppToolset", "IsPIERunning")
    evidence["runtime_log"] = call("EditorToolset.LogsToolset", "GetLogEntries", {"category": "LogRoguelitePrototype", "pattern": "ROGUELITE_DEV_START", "maxEntries": 5})
    assert evidence["pie_running"] is True
    assert any("LegacyActorCount=0" in line and "PawnClass=/Script/Engine.SpectatorPawn" in line for line in evidence["runtime_log"])
    evidence["stop_pie"] = call("EditorToolset.EditorAppToolset", "StopPIE")
    pie_started = False
    evidence["pie_stopped"] = not call("EditorToolset.EditorAppToolset", "IsPIERunning")
    evidence["map_dirty"] = call("editor_toolset.toolsets.asset.AssetTools", "is_dirty", {"asset_path": MAP})
    assert evidence["pie_stopped"] and evidence["map_dirty"] is False
    evidence["passed"] = True
    print("P005_MCP_PIE_PASS", flush=True)
except Exception as error:
    evidence["passed"] = False
    evidence["error"] = repr(error)
    raise
finally:
    if pie_started:
        try:
            call("EditorToolset.EditorAppToolset", "StopPIE")
        except Exception as error:
            evidence["cleanup_pie_error"] = repr(error)
    # This process was created solely by this validation script; leave the user's editor alone.
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            evidence["temporary_editor_stop_timeout"] = True
    evidence["temporary_editor_stopped"] = process.poll() is not None
    (EVIDENCE / "mcp-verification.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
