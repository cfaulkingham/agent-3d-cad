"""Developer-only, isolated CLI benchmark; no Python dependency for the service."""
import argparse
import hashlib
import json
import math
import pathlib
import platform
import shutil
import statistics
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("executable", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--resume", type=pathlib.Path)
parser.add_argument("--views", choices=("section", "standard"), default="section")
parser.add_argument("--cold-only", action="store_true", help="Measure creation and the first drawing, without warm/style/edit scenarios")
parser.add_argument("--timeout-ms", type=int, default=240000, help="Per-job budget, 1–300000 ms (default 240000)")
args = parser.parse_args()
if not 1 <= args.timeout_ms <= 300000:
    parser.error("--timeout-ms must be between 1 and 300000")
exe = args.executable.resolve()
executable_digest = hashlib.sha256(exe.read_bytes()).hexdigest()
args.output.mkdir(parents=True, exist_ok=True)
root = args.resume.resolve() if args.resume else pathlib.Path(tempfile.mkdtemp(prefix="cache-benchmark-", dir=args.output.resolve()))
workspace = root / "workspace"
source = pathlib.Path(__file__).resolve().parents[1] / "examples/m20-knob.create.json"
create = json.loads(source.read_text())
report = json.loads((root / "report.json").read_text()) if args.resume else {"platform": platform.platform(), "executable": str(exe), "executable_sha256": executable_digest,
    "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "workspace": str(workspace), "views": args.views, "timeout_ms": args.timeout_ms, "scenarios": []}
if args.resume and report["views"] != args.views:
    parser.error("--views must match the resumed benchmark")
if args.resume and report.get("executable_sha256", executable_digest) != executable_digest:
    parser.error("The executable changed; start a fresh benchmark instead of combining results from different builds")


def call(tool, request):
    for attempt in range(100):
        process = subprocess.run([str(exe), "call", tool, "--workspace", str(workspace), "--input", "-"],
                                 input=json.dumps(request), capture_output=True, text=True, timeout=260)
        if not process.returncode:
            break
        if json.loads(process.stderr).get("error", {}).get("code") != "workspace_busy":
            raise RuntimeError(process.stderr)
        time.sleep(0.02)
    else:
        raise RuntimeError(process.stderr)
    return json.loads(process.stdout)


def job(label, tool, request):
    call("cad_job", {"action": "submit", "request_id": label, "tool": tool, "arguments": request,
                     "budget": {"timeout_ms": args.timeout_ms, "memory_mb": 2048}})
    while True:
        state = call("cad_job", {"action": "get", "job_id": label})
        if state["state"] in ("succeeded", "failed", "cancelled", "interrupted"):
            break
        time.sleep(0.02)
    # Durable coordinator times also allow resuming an interrupted benchmark.
    durable = json.loads((workspace / "jobs" / label / "state.json").read_text())
    elapsed = (durable["updated_at_unix_ms"] - durable["submitted_at_unix_ms"]) / 1000
    result = state.get("result")
    (root / (label + ".json")).write_text(json.dumps(result if result is not None else state, indent=2) + "\n")
    cache = list((workspace / ".cache").glob("*.json"))
    report["scenarios"] = [s for s in report["scenarios"] if s["name"] != label]
    report["scenarios"].append({"name": label, "state": state["state"], "seconds": elapsed, "cache_entries": len(cache),
                                "cache_bytes": sum(p.stat().st_size for p in cache),
                                **({"error": state["error"]} if "error" in state else {})})
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{label}: {state['state']} in {elapsed:.3f}s ({len(cache)} entries)", flush=True)
    if state["state"] != "succeeded":
        raise RuntimeError(f"{label} {state['state']}: {state.get('error')}; report: {root / 'report.json'}")
    return result


if not args.resume:
    job("create", "cad_create", create)
    shutil.rmtree(workspace / ".cache", ignore_errors=True)
request = {"document_id": create["document_id"], "revision": 1,
           "drawing": {"formats": ["svg", "pdf", "dxf"]}}
if args.views == "section":
    request["drawing"]["views"] = [{"id": "longitudinal", "orientation": "section", "section": {"axis": "y", "offset": 0}},
                                    {"id": "grip", "orientation": "section", "section": {"axis": "z", "offset": 9}}]
    request["drawing"]["dimensions"] = [{"view": "longitudinal", "kind": "height"}, {"view": "grip", "kind": "width"}]
cold = job("cold_drawing", "cad_drawing", request)
if args.cold_only:
    print(f"Report: {root / 'report.json'}", flush=True)
    raise SystemExit(0)
for i in range(3):
    warm = job(f"warm_drawing_{i}", "cad_drawing", request)
    assert warm["dimensions"] == cold["dimensions"]
    assert len(warm["artifacts"]) == len(cold["artifacts"])
    for a, b in zip(cold["artifacts"], warm["artifacts"]):
        assert a["format"] == b["format"]
        assert pathlib.Path(a["path"]).read_bytes() == pathlib.Path(b["path"]).read_bytes()
# A recipe change reuses the same projections and renders new artifacts/identity.
restyled = json.loads(json.dumps(request))
restyled["drawing"].update(title="Cached geometry - revised drawing", sheet="A3",
                          layout="grid" if args.views == "section" else "first_angle",
                          general_tolerances={"linear": 0.1},
                          dimensions=[{"view": "longitudinal" if args.views == "section" else "front", "kind": "height"},
                                      {"view": "grip" if args.views == "section" else "top", "kind": "width"}])
styled = job("restyled_drawing", "cad_drawing", restyled)
assert abs(styled["dimensions"][0]["value_mm"] - 38) < 0.02
summary = call("cad_query", {"document_id": create["document_id"], "revision": 1})["summary"]
expected_width = summary["bounds_mm"]["max"][0] - summary["bounds_mm"]["min"][0]
assert abs(styled["dimensions"][1]["value_mm"] - expected_width) < 0.02
# Remove only projections to measure exact geometry reuse independently.
for path in (workspace / ".cache").glob("*.json"):
    payload = json.loads(json.loads(path.read_text())["payload"])
    if "views" in payload:
        path.unlink()
geometry_only = job("geometry_only_drawing", "cad_drawing", request)
assert len(geometry_only["dimensions"]) == len(cold["dimensions"])
for a, b in zip(geometry_only["dimensions"], cold["dimensions"]):
    assert a["kind"] == b["kind"] and a["view"] == b["view"]
    assert math.isclose(a["value_mm"], b["value_mm"], rel_tol=1e-8, abs_tol=1e-8)
# A changed thread length must miss, preserve the historical revision and update dimensions.
job("edit_thread", "cad_apply", {"document_id": create["document_id"], "expected_revision": 1,
    "operations": [{"op": "set_parameter", "name": "stud_length", "value": 21}]})
new = call("cad_query", {"document_id": create["document_id"], "revision": 2})
old = call("cad_query", {"document_id": create["document_id"], "revision": 1})
assert new["summary"]["volume_mm3"] != old["summary"]["volume_mm3"]
assert call("cad_read", {"document_id": create["document_id"], "revision": 1})["model"] == create["model"]
by_name = {s["name"]: s["seconds"] for s in report["scenarios"]}
report["warm_median_seconds"] = statistics.median(by_name[f"warm_drawing_{i}"] for i in range(3))
report["repeat_speedup"] = by_name["cold_drawing"] / report["warm_median_seconds"]
report["assertions"] = "Warm PDF/SVG/DXF bytes match cold; styled dimensions match 38 mm height and exact model width; edited thread volume changes; historical source unchanged."
(root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"Report: {root / 'report.json'}", flush=True)
