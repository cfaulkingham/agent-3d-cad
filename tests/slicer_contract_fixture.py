"""Inputs for the native process-control fixture; no slicing accuracy simulation."""
import hashlib
import json
from pathlib import Path


def options(root, executable, review):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    profiles = {}
    for role in ("machine", "process", "filament"):
        profile = {"type": role, "name": "Normal" if role == "machine" else f"Fixture {role}"}
        if role == "machine":
            profile.update(gcode_flavor="marlin", printer_technology="FFF")
        else:
            profile.update(compatible_printers=["Normal"], post_process=[""])
        raw = json.dumps(profile).encode()
        path = root / f"{role}.json"
        path.write_bytes(raw)
        profiles[role] = {"path": str(path), "expected_sha256": hashlib.sha256(raw).hexdigest()}
    executable = Path(executable).resolve(strict=True)
    return {"backend": "orcaslicer", "version": "2.4.2",
            "executable": {"path": str(executable), "expected_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()},
            "profiles": profiles, "bed_type": "High Temp Plate", "review": review}


def verify_package(result):
    root = Path(result["directory"])
    manifest = json.loads(Path(result["path"]).read_bytes())
    for artifact in manifest["artifacts"]:
        path = Path(artifact["path"])
        assert not path.is_absolute() and ".." not in path.parts
        raw = (root / path).read_bytes()
        assert len(raw) == artifact["bytes"] and hashlib.sha256(raw).hexdigest() == artifact["sha256"]
    assert {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} == {
        a["path"] for a in manifest["artifacts"]} | {"manifest.json"}
    assert result["physical_print_started"] is False and manifest["printer_approval"] == "not_evaluated"
    raw = Path(result["gcode_path"]).read_bytes()
    assert len(raw) == result["gcode_bytes"] and hashlib.sha256(raw).hexdigest() == result["gcode_sha256"]
    return len(manifest["artifacts"]) + 3
