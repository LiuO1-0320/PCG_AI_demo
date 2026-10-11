"""Sequential calls to official localhost Epic MCP. Imports helpers, never old validators."""
import sys
from pathlib import Path
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MAP = "/Game/Roguelite/Maps/L_Roguelite_CorridorAssembly_Dev"
sys.path.insert(0, str(HERE.parent / "P03A"))
from mcp_client import Client as BaseClient, SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS, field, similar
class Client(BaseClient):
    def __init__(self):
        self.request_id = 0; self.session = None; self.calls = []; self.properties = {}
        self.rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "RogueliteP03C2", "version": "1.0"}})
        self.rpc("notifications/initialized", {}, True)
def own_map(c):
    if c.call(SCENE,"get_current_level") != MAP or c.call(APP,"IsPIERunning"):
        raise RuntimeError("Only the owned C2 map, outside PIE, is authorized")
