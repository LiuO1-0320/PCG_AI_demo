"""Read-only preservation audit, writing only a new P03-A report."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "final-verification.json"
if OUTPUT.exists():
    raise RuntimeError("Final evidence already exists")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

allowed = {"Plugins/RoguelitePrototype/Source/RoguelitePrototype/RoguelitePrototype.Build.cs",
    "Plugins/RoguelitePrototype/README.md", "Docs/06_EXECUTION_STATUS.md"}
baseline = json.loads((HERE / "baseline-files.json").read_text(encoding="utf-8-sig"))
checked = []
for item in baseline:
    path = ROOT / item["Path"]
    actual = sha(path) if path.exists() else None
    checked.append({"path": item["Path"], "before": item["SHA256"], "after": actual,
        "unchanged": actual == item["SHA256"], "approved_target": item["Path"] in allowed})
unexpected = [x for x in checked if not x["unchanged"] and not x["approved_target"]]
backups = json.loads((HERE / "backup-manifest.json").read_text(encoding="utf-8-sig"))
existing_changes = [{"path": x["Original"], "backup": x["Backup"], "backup_intact": sha(ROOT / x["Backup"]) == x["SHA256"],
    "before": x["SHA256"], "after": sha(ROOT / x["Original"])} for x in backups]
new = [ROOT / p for p in ("Plugins/RoguelitePrototype/Source/RoguelitePrototype/Public/RogueliteRoomAssembly.h",
    "Plugins/RoguelitePrototype/Source/RoguelitePrototype/Private/RogueliteRoomAssembly.cpp",
    "Plugins/RoguelitePrototype/P03A_ROOM_ASSEMBLY_GUIDE.md", "Plugins/RoguelitePrototype/P03A_RESULT.md",
    "Content/Roguelite/Maps/L_Roguelite_RoomAssembly_Dev.umap")]
new.extend(p for p in HERE.rglob("*") if p.is_file())
manifest = [{"path": p.relative_to(ROOT).as_posix(), "bytes": p.stat().st_size, "sha256": sha(p)} for p in sorted(new)]
git = ["git", "--no-optional-locks", "-c", "safe.directory=" + ROOT.as_posix()]
status = subprocess.check_output(git + ["status", "--short"], cwd=ROOT, text=True)
head = subprocess.check_output(git + ["rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
report = {"project": str(ROOT / "testfordemo.uproject"), "baseline_count": len(checked), "checked": checked,
    "unexpected_changes": unexpected, "modified_existing": existing_changes, "new_files": manifest,
    "git_head": head, "git_status_after": status, "git_status_before": (HERE / "git-status-before.txt").read_text(encoding="utf-8-sig"),
    "no_add_commit_reset_clean": True, "uproject_modified_by_this_task": False,
    "old_demo_and_p01_p02_preserved": not unexpected, "compiler_results": {"baseline": "Succeeded exit 0", "initial": "Succeeded exit 0 559.02s", "final": "Succeeded exit 0 32.23s"},
    "main_validation": "validation-ground.json 236/236", "final_validation": "final-editor-ground.json 13/13",
    "navigation": "Unavailable, not tested", "temporary_editors_closed": [json.loads((HERE / p).read_text(encoding="utf-8"))
        for p in ("editor-session-closed.json", "editor-ground-closed.json")]}
with OUTPUT.open("x", encoding="utf-8") as output:
    json.dump(report, output, ensure_ascii=False, indent=2)
print(json.dumps({"baseline": len(checked), "unchanged": sum(x["unchanged"] for x in checked), "approved_changed_in_baseline": [x["path"] for x in checked if not x["unchanged"] and x["approved_target"]],
    "unexpected_changes": unexpected, "backups_intact": all(x["backup_intact"] for x in existing_changes), "existing_modified_targets": len(existing_changes), "new_files": len(manifest), "head": head}, ensure_ascii=False))
if unexpected or not all(x["backup_intact"] for x in existing_changes):
    raise SystemExit(1)
