"""Optional developer check: pip install jsonschema==4.25.1.
Runs the actual executable; no Python is used by the product or native CTest.
"""
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import time

from jsonschema import Draft202012Validator

exe = str(pathlib.Path(sys.argv[1]).resolve())
source = pathlib.Path(__file__).resolve().parents[1]
definitions = json.loads(subprocess.check_output([exe, "tools"], text=True))
tools = {item["name"]: item for item in definitions}
for item in definitions:
    Draft202012Validator.check_schema(item["inputSchema"])
    Draft202012Validator.check_schema(item["outputSchema"])
checks = 2 * len(definitions)

with tempfile.TemporaryDirectory(prefix="cad-schemas-") as workspace:
    def call(name, arguments):
        global checks
        definition = tools[name]
        Draft202012Validator(definition["inputSchema"]).validate(arguments)
        busy_deadline = time.monotonic() + 5
        while True:
            run = subprocess.run([exe, "call", name, "--workspace", workspace, "--input", "-"],
                                 input=json.dumps(arguments), text=True, capture_output=True, timeout=45)
            if not run.returncode:
                break
            error = json.loads(run.stderr).get("error", {})
            if error.get("code") != "workspace_busy" or time.monotonic() >= busy_deadline:
                raise AssertionError(f"{name}: {run.stderr}")
            # Jobs can briefly hold the nonblocking metadata lock while the
            # coordinator updates state. Retry that documented transient only.
            time.sleep(.02)
        result = json.loads(run.stdout)
        Draft202012Validator(definition["outputSchema"]).validate(result)
        checks += 2
        return result

    model = {"schema_version": 1, "units": "mm", "parameters": {"height": 6},
             "features": [{"id": "base", "type": "box", "size": [20, 10, {"parameter": "height"}]}], "output": "base"}
    created = call("cad_create", {"document_id": "part", "model": model, "request_id": "create_once"})
    assert call("cad_create", {"document_id": "part", "model": model, "request_id": "create_once"}) == created
    call("cad_read", {"document_id": "part"})
    drawing_recipe = {"title": "Plate & fixture <A>", "sheet": "A4", "scale": 2,
        "views": [{"id": "front", "orientation": "front"}, {"id": "top", "orientation": "top"}],
        "dimensions": [{"view": "front", "kind": "width"}, {"view": "front", "kind": "height"}],
        "material": "Aluminum", "notes": ["Dimensions in mm"]}
    first_drawing = call("cad_drawing", {"document_id": "part", "revision": 1, "drawing": drawing_recipe})
    assert {item["format"] for item in first_drawing["artifacts"]} == {"svg", "pdf", "dxf"}
    saved_recipe = json.loads(pathlib.Path(first_drawing["recipe_path"]).read_text())
    assert saved_recipe["drawing"] == drawing_recipe
    assert {item["kind"]: item["value_mm"] for item in first_drawing["dimensions"]} == {"width": 20, "height": 6}
    old_artifacts = {item["path"]: pathlib.Path(item["path"]).read_bytes() for item in first_drawing["artifacts"]}
    checks += 3
    standard_recipe = {"layout": "first_angle", "sheet": "A3", "views": [
        {"id": "front", "orientation": "front"}, {"id": "top", "orientation": "top"},
        {"id": "right", "orientation": "right"},
        {"id": "cut", "orientation": "section", "section": {"axis": "z", "offset": 3}, "hatch": True}]}
    standard = call("cad_drawing", {"document_id": "part", "revision": 1, "drawing": standard_recipe})
    assert standard["layout"] == "first_angle" and len(standard["view_layouts"]) == 4
    layout = {p["view"]: p for p in standard["view_layouts"]}
    assert layout["front"]["origin_mm"][0] == layout["top"]["origin_mm"][0]
    assert layout["front"]["origin_mm"][1] == layout["right"]["origin_mm"][1]
    checks += 3
    for bad in [{"layout": "automatic"}, {"views": [{"id": "front", "orientation": "front", "hatch": True}]},
                {"views": [{"id": "cut", "orientation": "section", "section": {"axis": "z", "offset": 3}, "hatch": 1}]}]:
        assert not Draft202012Validator(tools["cad_drawing"]["inputSchema"]).is_valid(
            {"document_id": "part", "revision": 1, "drawing": bad})
        checks += 1
    angular_recipe = {"views": [{"id": "top", "orientation": "top"}], "scale": 2,
        "general_tolerances": {"linear": .1, "angular": .5}, "dimensions": [
            {"view": "top", "kind": "angular", "arc_radius": 4, "lines": [
                {"from": [0, 0], "to": [20, 0]}, {"from": [0, 0], "to": [0, 10]}],
             "manufacturing_tolerance": {"type": "limits", "lower": 89.5, "upper": 90.5}},
            {"view": "top", "kind": "width", "manufacturing_tolerance": {"type": "symmetric", "value": .01}},
            {"view": "top", "kind": "height", "manufacturing_tolerance": {"type": "deviation", "lower": -.02, "upper": .01}}]}
    angular = call("cad_drawing", {"document_id": "part", "revision": 1, "drawing": angular_recipe})
    assert angular["dimensions"][0]["value_deg"] == 90 and "value_mm" not in angular["dimensions"][0]
    assert angular["dimensions"][0]["lower_limit_deg"] == 89.5
    assert angular["dimensions"][1]["upper_limit_mm"] == 20.01
    assert angular["dimensions"][2]["label"] == "10 +0.01/-0.02"
    checks += 4
    for bad_dimension in [
        {"view": "top", "kind": "angular", "lines": []},
        {"view": "top", "kind": "angular", "lines": angular_recipe["dimensions"][0]["lines"], "sweep": "clockwise"},
        {"view": "top", "kind": "width", "manufacturing_tolerance": {"type": "limits", "lower": 1}},
        {"view": "top", "kind": "width", "manufacturing_tolerance": {"type": "symmetric", "value": .1, "units": "mm"}}]:
        assert not Draft202012Validator(tools["cad_drawing"]["inputSchema"]).is_valid(
            {"document_id": "part", "revision": 1, "drawing": {"dimensions": [bad_dimension]}})
        checks += 1
    operations = [{"op": "set_parameter", "name": "height", "value": 8}]
    call("cad_apply", {"document_id": "part", "expected_revision": 1, "operations": operations})
    regenerated = call("cad_drawing", {"document_id": "part", "revision": 2, "drawing": saved_recipe["drawing"]})
    assert {item["kind"]: item["value_mm"] for item in regenerated["dimensions"]} == {"width": 20, "height": 8}
    assert regenerated["recipe_path"] != first_drawing["recipe_path"]
    assert all(pathlib.Path(path).read_bytes() == content for path, content in old_artifacts.items())
    assert all(pathlib.Path(item["path"]).stat().st_size == item["bytes"] > 0 for item in regenerated["artifacts"])
    checks += 4
    for kind in ["summary", "topology", "mesh"]:
        call("cad_query", {"document_id": "part", "revision": 2, "kind": kind})
    view = call("cad_view", {"document_id": "part", "revision": 2})
    data = json.loads(pathlib.Path(view["data_path"]).read_text())
    for kind, collection in [("face", "faces"), ("edge", "edges")]:
        call("cad_resolve_selection", {"document_id": "part", "revision": 2,
            "evaluation_id": view["evaluation_id"], "feature_id": "base", "kind": kind,
            "entity_id": data["topology"][collection][0]["id"]})
    call("cad_preview", {"document_id": "part", "expected_revision": 2,
        "operations": [{"op": "set_parameter", "name": "height", "value": 10}]})
    exported = call("cad_export", {"document_id": "part", "revision": 2, "format": "step"})
    imported = call("cad_import", {"document_id": "imported", "path": exported["path"], "request_id": "import_once"})
    pathlib.Path(exported["path"]).unlink()
    rebuilt = call("cad_query", {"document_id": "imported", "revision": 1})
    assert abs(rebuilt["summary"]["volume_mm3"] - imported["summary"]["volume_mm3"]) < 1e-6
    call("cad_compare", {"document_id": "part", "from_revision": 1, "to_revision": 2})
    restored = call("cad_restore", {"document_id": "part", "expected_revision": 2, "source_revision": 1, "request_id": "restore_once"})
    assert restored["revision"] == 3 and abs(restored["summary"]["volume_mm3"] - 1200) < 1e-6
    for name in ["bracket", "nozzle"]:
        example = json.loads((source / "examples" / f"{name}.create.json").read_text())
        result = call("cad_create", example)
        document_id = result["document_id"]
        call("cad_view", {"document_id": document_id, "revision": 1})
        call("cad_export", {"document_id": document_id, "revision": 1, "format": "step"})
        parameter, value = ("wall", 5) if name == "bracket" else ("height", 30)
        edited = call("cad_apply", {"document_id": document_id, "expected_revision": 1,
            "operations": [{"op": "set_parameter", "name": parameter, "value": value}]})
        # Every call starts a new native process. Reopen the edited document and
        # compare an independent analytic volume, not two copies of our output.
        reopened = call("cad_read", {"document_id": document_id})
        measured = call("cad_query", {"document_id": document_id, "revision": 2})
        original = call("cad_query", {"document_id": document_id, "revision": 1})
        expected = ((40*5 + (30-5)*5)*30 - 2*math.pi*2.5**2*5 if name == "bracket"
                    else math.pi*30*(12**2 + 12*5 + 5**2)/3 - math.pi*3**2*30)
        assert edited["revision"] == reopened["revision"] == 2
        assert reopened["model"]["parameters"][parameter] == value
        assert [f["id"] for f in reopened["model"]["features"]] == [f["id"] for f in example["model"]["features"]]
        assert measured["feature_id"] == example["model"]["output"]
        assert abs(measured["summary"]["volume_mm3"] - expected) < 1e-3, (name, measured["summary"], expected)
        assert abs(original["summary"]["volume_mm3"] - result["summary"]["volume_mm3"]) < 1e-6
        checks += 6
        call("cad_view", {"document_id": document_id, "revision": 2})
        call("cad_export", {"document_id": document_id, "revision": 2, "format": "step"})
    job = call("cad_job", {"action": "submit", "request_id": "async_query", "tool": "cad_query",
        "arguments": {"document_id": "part", "revision": 3, "kind": "topology"}})
    for _ in range(100):
        job = call("cad_job", {"action": "get", "job_id": job["job_id"]})
        if job["state"] in ["succeeded", "failed", "cancelled", "interrupted"]:
            break
        time.sleep(.02)
    assert job["state"] == "succeeded", job
    call("cad_job", {"action": "cancel", "job_id": job["job_id"]})
    call("cad_job", {"action": "list"})
    documents = call("cad_list", {})
    assert {"part", "imported"} <= {item["document_id"] for item in documents["documents"]}
    opened = call("cad_open", {"view_id": "schema_view", "document_id": "part"})
    assert opened["resource_uri"] == "ui://agent-3d-cad/viewer.html"
    for _ in range(200):
        synced = call("cad_viewer", {"action": "sync", "view_id": "schema_view"})
        if synced["state"] != "loading":
            break
        time.sleep(.02)
    assert synced["state"] == "ready", synced
    unchanged = call("cad_viewer", {"action": "sync", "view_id": "schema_view",
        "known_evaluation_id": synced["evaluation_id"]})
    assert unchanged["changed"] is False and "model" not in unchanged
    parts, offset = [], 0
    while offset is not None:
        chunk = call("cad_viewer", {"action": "mesh", "view_id": "schema_view",
            "evaluation_id": synced["evaluation_id"], "offset": offset})
        assert chunk["offset"] == offset
        parts.append(chunk["data"])
        offset = chunk["next_offset"]
    payload = json.loads("".join(parts))
    assert payload["evaluation_id"] == synced["evaluation_id"]
    selection = {"document_id": "part", "revision": 3, "evaluation_id": synced["evaluation_id"],
                 "feature_id": synced["feature_id"], "kind": "edge",
                 "entity_id": payload["topology"]["edges"][0]["id"]}
    context = call("cad_viewer", {"action": "context", "view_id": "schema_view",
        "evaluation_id": synced["evaluation_id"], "selection": selection,
        "camera": {"yaw": .4, "pitch": .2, "zoom": 1.5, "pan": [0, 1]}, "prompt": "Round this edge"})
    assert context["selection"] == selection and context["stale"] is False
    assert call("cad_context", {"view_id": "schema_view"})["selection"] == selection
    shown = call("cad_show", {"view_id": "schema_view", "document_id": "imported"})
    assert shown["view_id"] == opened["view_id"] and shown["document_id"] == "imported"
    checks += 8
    for arguments in [{"action": "submit"}, {"action": "get"}, {"action": "list", "job_id": "extra"}]:
        assert not Draft202012Validator(tools["cad_job"]["inputSchema"]).is_valid(arguments)
        checks += 1
    frames = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "schema-conformance", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "cad_query", "arguments": {"document_id": "part", "revision": 3, "kind": "topology"}}},
    ]
    mcp = subprocess.run([exe, "serve", "--workspace", workspace], input="\n".join(map(json.dumps, frames)), text=True, capture_output=True, timeout=45, check=True)
    responses = list(map(json.loads, mcp.stdout.splitlines()))
    assert len(responses) == 3 and len(responses[1]["result"]["tools"]) == len(tools)
    reply = responses[2]["result"]
    assert not reply["isError"] and json.loads(reply["content"][0]["text"]) == reply["structuredContent"]
    Draft202012Validator(tools["cad_query"]["outputSchema"]).validate(reply["structuredContent"])
    checks += 3
print(f"schema conformance: {checks} checks passed across {len(tools)} tools")
