"""Read-only project audit; write one new C1 manifest, never alter protected files."""
import hashlib
import json
import os
import socket
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
out = HERE / "final-verification.json"
if out.exists():
    raise RuntimeError("Manifest exists: inspect instead of overwriting")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    return subprocess.check_output(["git", "-c", "safe.directory=" + ROOT.as_posix(), *args],
        cwd=ROOT, text=True, encoding="utf-8").strip()


baseline = json.loads((HERE / "baseline-files.json").read_text(encoding="utf-8-sig"))
manifest = json.loads((HERE / "backup-manifest.json").read_text(encoding="utf-8-sig"))
allowed = set(manifest["targets"])
before = {p["path"]: p["sha256"] for p in baseline}
changed, unchanged, missing = [], [], []
for name, original in before.items():
    path = ROOT / name
    if not path.is_file():
        missing.append(name)
        continue
    current = sha(path)
    (unchanged if current == original else changed).append({"path": name, "before": original, "after": current})
backups = [{"path": name, "backup_matches_baseline": sha(HERE / "Backups" / name) == before[name]}
    for name in manifest["targets"]]

skip = {".git", ".vs", "Binaries", "Intermediate", "Saved", "DerivedDataCache", "node_modules", "__pycache__"}
new = []
for parent, dirs, files in os.walk(ROOT):
    dirs[:] = [d for d in dirs if d not in skip]
    for leaf in files:
        path = Path(parent) / leaf
        relative = path.relative_to(ROOT).as_posix()
        if relative not in before:
            new.append({"path": relative, "sha256": sha(path), "bytes": path.stat().st_size})
allowed_new = set(manifest["new_paths"]) | {
    "Plugins/RoguelitePrototype/P03C1_CORRIDOR_GUIDE.md", "Plugins/RoguelitePrototype/P03C1_RESULT.md"}
unexpected_new = [p["path"] for p in new if p["path"] not in allowed_new
    and not p["path"].startswith("Plugins/RoguelitePrototype/Development/P03C1/")]
validation = json.loads((HERE / "validation.json").read_text(encoding="utf-8"))
details = json.loads((HERE / "details-validation.json").read_text(encoding="utf-8"))
readback = json.loads((HERE / "final-editor-readback.json").read_text(encoding="utf-8"))
closed = json.loads((HERE / "editor-closed.json").read_text())
with socket.socket() as probe:
    port_closed = probe.connect_ex(("127.0.0.1", 8001)) != 0
current_status = git("status", "--short")
before_status = (HERE / "git-before.txt").read_text(encoding="utf-8-sig").strip()
checks = {
    "253_existing_files": len(before) == 253,
    "only_five_allowed_existing_documents_changed": {p["path"] for p in changed} == allowed,
    "248_other_files_unchanged": len(unchanged) == 248,
    "none_missing": not missing,
    "five_backups_match": len(backups) == 5 and all(p["backup_matches_baseline"] for p in backups),
    "new_files_scoped": not unexpected_new,
    "main_333_passed": validation["completed"] and len(validation["checks"]) == 333
        and all(p["passed"] for p in validation["checks"]),
    "details_11_passed": details["completed"] and len(details["checks"]) == 11
        and all(p["passed"] for p in details["checks"]),
    "final_native_sweep_passed": readback["parameters"]["bHasPassageTestResult"]
        and readback["parameters"]["bPassageTestPassed"],
    "owned_editor_closed_port_stopped": closed["only_owned_pid_closed"] and port_closed,
    "git_status_matches_before": current_status.splitlines() == before_status.splitlines(),
}
evidence = {"task": "P03-C1", "root": str(ROOT), "head": git("rev-parse", "HEAD"),
    "checks": checks, "passed": all(checks.values()), "changed_existing": changed,
    "protected_unchanged": unchanged, "missing": missing, "backups": backups, "new_files": new,
    "unexpected_new": unexpected_new, "git_status": current_status, "owned_editor": closed,
    "not_tested": validation["not_tested"],
    "self_manifest_not_in_new_files": "This manifest is written after inventory; it is itself a new C1-only evidence file."}
with out.open("x", encoding="utf-8") as f:
    json.dump(evidence, f, ensure_ascii=False, indent=2)
print(json.dumps({"passed": evidence["passed"], "checks": checks,
    "modified_existing": len(changed), "protected": len(unchanged), "new_files": len(new)}, ensure_ascii=False))
assert evidence["passed"], "Inspect manifest; do not overwrite or retry unchanged"
