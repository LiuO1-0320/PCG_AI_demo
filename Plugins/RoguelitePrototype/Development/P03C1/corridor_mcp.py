"""Reuse the verified sequential HTTP/Slate helper, never import or run old validators."""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MAP = "/Game/Roguelite/Maps/L_Roguelite_Corridor_Dev"
SOURCE_MAP = "/Game/Roguelite/Maps/L_Roguelite_Dev"
sys.path.insert(0, str(HERE.parent / "P03A"))
from mcp_client import Client as BaseClient, SCENE, ACTOR, OBJECT, ASSET, APP, SLATE, LOGS, field, similar


class Client(BaseClient):
    def __init__(self):
        self.request_id = 0
        self.session = None
        self.calls = []
        self.properties = {}
        self.rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "RogueliteP03C1", "version": "1.0"}})
        self.rpc("notifications/initialized", {}, True)


def own_map(client):
    if client.call(SCENE, "get_current_level") != MAP or client.call(APP, "IsPIERunning"):
        raise RuntimeError("P03C1-only operation refused: wrong map or PIE running")
