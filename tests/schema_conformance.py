"""Optional developer check: pip install jsonschema==4.25.1.
Runs the actual executable; no Python is used by the product or native CTest.
"""
import json
import hashlib
import csv
import io
import math
import pathlib
import subprocess
import sys
import tempfile
import time

from jsonschema import Draft202012Validator
from slicer_contract_fixture import options as slice_options, verify_package

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
    gcode = pathlib.Path(workspace) / "review fixture.gcode"
    raw_gcode = b"G21\nG90\nM83\nM104S210\nM140S60\nG1X10Y5Z.2E1\nG91\nG1X15E1\n"
    gcode.write_bytes(raw_gcode)
    gcode_options = {"firmware": "marlin", "machine": {"name": "Analytic fixture", "motion_bounds_mm": [[-20, 20], [-20, 20], [0, 100]]},
                     "material": {"name": "Fixture PLA", "nozzle_temperature_c": [190, 230], "bed_temperature_c": [50, 70]},
                     "initial": {"units": "mm", "xyz_mode": "absolute", "extrusion_mode": "absolute", "position_mm": [0, 0, 0], "extruder_mm": 0}}
    gcode_arguments = {"document_id": "part", "revision": 1, "path": str(gcode),
                       "expected_sha256": hashlib.sha256(raw_gcode).hexdigest(), "options": gcode_options}
    gcode_result = call("cad_gcode_review", gcode_arguments)
    assert gcode_result["report"]["status"] == "fail" and gcode_result["report"]["statistics"]["commanded_bounds_mm"][0][1] == 25
    assert pathlib.Path(gcode_result["artifact_path"]).read_bytes() == raw_gcode
    report_bytes = pathlib.Path(gcode_result["path"]).read_bytes()
    assert len(report_bytes) == gcode_result["report_bytes"] and hashlib.sha256(report_bytes).hexdigest() == gcode_result["report_sha256"]
    assert json.loads(report_bytes)["source_association"] == "caller_declared_not_geometry_verified"
    gcode_job = call("cad_job", {"action": "submit", "request_id": "schema_gcode", "tool": "cad_gcode_review", "arguments": gcode_arguments})
    for _ in range(500):
        gcode_job = call("cad_job", {"action": "get", "job_id": gcode_job["job_id"]})
        if gcode_job["state"] not in {"queued", "running", "cancelling"}:
            break
        time.sleep(.01)
    assert gcode_job["state"] == "succeeded" and gcode_job["result"]["report"]["status"] == "fail"
    for bad in [dict(gcode_arguments, shell="bad"), dict(gcode_arguments, expected_sha256="BAD"),
                dict(gcode_arguments, options=dict(gcode_options, script="bad"))]:
        assert not Draft202012Validator(tools["cad_gcode_review"]["inputSchema"]).is_valid(bad)
        checks += 1
    checks += 7
    printer_profiles = {}
    for role in ["machine", "process", "filament"]:
        profile_path = pathlib.Path(workspace) / ("printer-" + role + ".json")
        profile_raw = json.dumps({"type": role, "name": "Explicit schema " + role, "post_process": []}).encode()
        profile_path.write_bytes(profile_raw)
        printer_profiles[role] = {"path": str(profile_path), "expected_sha256": hashlib.sha256(profile_raw).hexdigest()}
    printer_args = {"document_id": "part", "revision": 1, "action": "plan", "path": str(gcode),
                    "expected_sha256": hashlib.sha256(raw_gcode).hexdigest(), "options": {
                        "printer": {"backend": "manual", "id": "schema_printer", "model": "Assessed fixture",
                                    "nozzle_diameter_mm": .4, "bed_type": "Explicit plate", "handoff": "plain_gcode"},
                        "profiles": printer_profiles, "review": gcode_options}}
    printer_plan = call("cad_printer_handoff", printer_args)
    assert printer_plan["readiness"]["status"] == "fail" and not printer_plan["hardware_contact"]
    assert printer_plan["source_association"] == "caller_declared_not_geometry_verified"
    printer_verified = call("cad_printer_handoff", {"document_id": "part", "revision": 1, "action": "verify",
                          "plan_path": printer_plan["path"], "expected_sha256": printer_plan["sha256"]})
    assert printer_verified["readiness"] == printer_plan["readiness"] and not printer_verified["physical_print_started"]
    printer_job = call("cad_job", {"action": "submit", "request_id": "schema_printer", "tool": "cad_printer_handoff", "arguments": printer_args})
    for _ in range(500):
        printer_job = call("cad_job", {"action": "get", "job_id": printer_job["job_id"]})
        if printer_job["state"] not in {"queued", "running", "cancelling"}:
            break
        time.sleep(.01)
    assert printer_job["state"] == "succeeded" and printer_job["result"]["readiness"]["status"] == "fail"
    Draft202012Validator(tools["cad_printer_handoff"]["outputSchema"]).validate(printer_job["result"])
    for bad in [dict(printer_args, action="start"), dict(printer_args, execute=True),
                dict(printer_args, options=dict(printer_args["options"], host="guessed")),
                dict(printer_args, expected_sha256="BAD")]:
        assert not Draft202012Validator(tools["cad_printer_handoff"]["inputSchema"]).is_valid(bad)
        checks += 1
    checks += 5
    if len(sys.argv) > 2:
        slicing = {"document_id": "part", "revision": 1, "action": "plan",
                   "options": slice_options(pathlib.Path(workspace) / "native profiles", sys.argv[2], gcode_options)}
        planned = call("cad_slice", slicing)
        assert hashlib.sha256(pathlib.Path(planned["path"]).read_bytes()).hexdigest() == planned["sha256"]
        running = {"document_id": "part", "revision": 1, "action": "run", "plan_path": planned["path"], "expected_sha256": planned["sha256"]}
        slice_job = call("cad_job", {"action": "submit", "request_id": "schema_slice", "tool": "cad_slice", "arguments": running})
        for _ in range(500):
            slice_job = call("cad_job", {"action": "get", "job_id": slice_job["job_id"]})
            if slice_job["state"] not in {"queued", "running", "cancelling"}:
                break
            time.sleep(.01)
        assert slice_job["state"] == "succeeded", slice_job
        checks += verify_package(slice_job["result"])
        # Validate direct result and cad_job's compacted result schema separately.
        Draft202012Validator(tools["cad_slice"]["outputSchema"]).validate(slice_job["result"])
        for bad in [dict(slicing, shell="bad"), dict(slicing, options=dict(slicing["options"], version="2.4.1")),
                    dict(running, expected_sha256="BAD"), dict(running, options=slicing["options"])]:
            assert not Draft202012Validator(tools["cad_slice"]["inputSchema"]).is_valid(bad)
            checks += 1
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
    step_bytes = pathlib.Path(exported["path"]).read_bytes()
    step_hash = hashlib.sha256(step_bytes).hexdigest()
    purchase = {"supplier": "Schema fixture supplier", "part_number": "FIXTURE-1", "source_url": "https://example.invalid/FIXTURE-1"}
    bought = call("cad_import", {"document_id": "purchased_schema", "path": exported["path"],
        "expected_sha256": step_hash, "purchase": purchase})
    bound_purchase = dict(purchase, artifact_sha256=step_hash)
    assert bought["model"]["features"][0]["purchase"] == bound_purchase
    purchased_assembly = json.loads(json.dumps(bought["model"]))
    purchased_assembly["features"].append({"id": "assembly", "type": "assembly", "parts": [
        {"id": "one", "input": "imported"}, {"id": "two", "input": "imported", "placement": {"translation": [30, 0, 0]}}]})
    purchased_assembly["output"] = "assembly"
    call("cad_create", {"document_id": "purchased_assembly_schema", "model": purchased_assembly})
    bought_bom = call("cad_bom", {"document_id": "purchased_assembly_schema", "revision": 1})
    assert bought_bom["bom"]["items"][0]["purchase"] == bound_purchase and bought_bom["bom"]["items"][0]["quantity"] == 2
    bought_package = call("cad_manufacture", {"document_id": "purchased_assembly_schema", "revision": 1, "options": {"drawings": False}})
    bought_manifest = json.loads(pathlib.Path(bought_package["path"]).read_text())
    original = bought_manifest["parts"][0]["source_artifact"]
    assert original["sha256"] == step_hash and (pathlib.Path(bought_package["directory"]) / original["path"]).read_bytes() == step_bytes
    assert bought_manifest["parts"][0]["purchase"] == bound_purchase
    for bad in [{"expected_sha256": "broken"}, {"purchase": dict(purchase, source_url="file:///tmp/part")},
                {"purchase": dict(purchase, artifact_sha256="broken")}]:
        assert not Draft202012Validator(tools["cad_import"]["inputSchema"]).is_valid(
            dict(document_id="bad_purchase", path=exported["path"], **bad))
        checks += 1
    missing_hash = json.loads(json.dumps(bought["model"]))
    del missing_hash["features"][0]["purchase"]["artifact_sha256"]
    assert not Draft202012Validator(tools["cad_create"]["inputSchema"]).is_valid({"document_id": "missing_hash", "model": missing_hash})
    checks += 5
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
    # Assemblies reuse the exact same tools, persisted document and live schemas.
    assembly = json.loads((source / "examples/assembly.create.json").read_text())
    assembled = call("cad_create", assembly)
    aid = assembly["document_id"]
    inventory = assembled["summary"]["assembly"]["parts"]
    assert len(inventory) >= 2 and all(len(p["transform"]) == 16 for p in inventory)
    bom = call("cad_bom", {"document_id": aid, "revision": 1})
    assert bom["bom"]["total_quantity"] == 4
    assert [(item["input"], item["quantity"], item["item_number"]) for item in bom["bom"]["items"]] == [
        ("plate", 2, 1), ("spacer", 2, 2)]
    assert all("material" not in item and "description" not in item for item in bom["bom"]["items"])
    assert json.loads(pathlib.Path(bom["path"]).read_text()) == bom
    for artifact in bom["artifacts"]:
        content = pathlib.Path(artifact["path"]).read_bytes()
        assert len(content) == artifact["bytes"] > 0
        if artifact["format"] == "json":
            saved_bom = json.loads(content)
            assert saved_bom["bom"] == bom["bom"] and saved_bom["revision"] == 1
        else:
            rows = list(csv.DictReader(io.StringIO(content.decode("ascii"))))
            assert [(r["input"], r["quantity"], r["part_ids"]) for r in rows] == [
                ("plate", "2", "base;cover"), ("spacer", "2", "spacer_a;spacer_b")]
        checks += 2
    checks += 4
    balloon_recipe = json.loads((source / "examples/assembly-bom.drawing.json").read_text())
    bom_drawing = call("cad_drawing", balloon_recipe)
    assert bom_drawing["bom"] == bom["bom"] and len(bom_drawing["balloons"]) == 4
    assert {b["part_id"]: b["item_number"] for b in bom_drawing["balloons"]} == {
        "base": 1, "cover": 1, "spacer_a": 2, "spacer_b": 2}
    sidecars = {a["format"]: pathlib.Path(a["path"]).read_bytes() for a in bom_drawing["artifacts"]}
    assert set(sidecars) == {"pdf", "svg", "dxf", "json", "csv"}
    assert json.loads(sidecars["json"])["bom"] == bom["bom"]
    assert b'BALLOONS' in sidecars["dxf"] and b'BILL OF MATERIALS' in sidecars["svg"]
    assert call("cad_read", {"document_id": aid})["model"] == assembly["model"]
    checks += 6
    assembly_mesh = call("cad_query", {"document_id": aid, "revision": 1, "kind": "mesh"})
    part_ids = {p["id"] for p in inventory}
    assert {f["part_id"] for f in assembly_mesh["topology"]["faces"]} == part_ids
    pick = {"document_id": aid, "revision": 1, "evaluation_id": assembly_mesh["evaluation_id"],
            "feature_id": assembly_mesh["feature_id"], "kind": "edge",
            "entity_id": assembly_mesh["topology"]["edges"][0]["id"]}
    selected = call("cad_resolve_selection", pick)
    assert selected["geometry"]["part_id"] in part_ids and "selector" not in selected
    call("cad_drawing", json.loads((source / "examples/assembly.drawing.json").read_text()))
    call("cad_view", {"document_id": aid, "revision": 1})
    call("cad_open", {"document_id": aid, "view_id": "assembly_schema"})
    for _ in range(200):
        synced = call("cad_viewer", {"action": "sync", "view_id": "assembly_schema"})
        if synced["state"] != "loading":
            break
        time.sleep(.02)
    assert synced["state"] == "ready" and len(synced["summary"]["assembly"]["parts"]) == len(inventory)
    assert synced["hidden_part_ids"] == []
    measurement_args={"document_id":aid,"revision":1,"evaluation_id":synced["evaluation_id"],"feature_id":synced["feature_id"],
        "query":{"action":"pair","targets":[{"kind":"part","part_id":"spacer_a"},{"kind":"part","part_id":"spacer_b"}],"minimum_clearance_mm":1}}
    measured=call("cad_measure",measurement_args)
    assert math.isclose(measured["report"]["minimum_distance_mm"],math.hypot(40,10)-10,abs_tol=1e-6)
    assert measured["report"]["status"]=="pass" and measured["report"]["pairs"][0]["intersection_volume_mm3"]==0
    all_pairs=call("cad_measure",dict(measurement_args,query={"action":"clearance"}))
    assert len(all_pairs["report"]["pairs"])==6 and all_pairs["report"]["coverage"]=="all_assembly_leaves"
    viewer_measure={"action":"measure","view_id":"assembly_schema","evaluation_id":synced["evaluation_id"],"query":measurement_args["query"]}
    pending_measure=call("cad_viewer",viewer_measure)
    for _ in range(500):
        pending_measure=call("cad_viewer",{k:v for k,v in viewer_measure.items() if k!="query"})
        if pending_measure["state"] not in {"queued","running","cancelling"}:break
        time.sleep(.01)
    assert pending_measure["state"]=="succeeded" and pending_measure["result"]["report"]["status"]=="pass"
    assert call("cad_context",{"view_id":"assembly_schema"})["measurement"]["job_id"]==pending_measure["job_id"]
    call("cad_viewer",dict(viewer_measure,query=None))
    for bad_query in [{"action":"pair","targets":[{"kind":"face","entity_id":"edge-1"},{"kind":"part","part_id":"cover"}]},
        {"action":"clearance","script":"bad"},{"action":"clearance","part_ids":["base"]},
        {"action":"clearance","minimum_clearance_mm":-1}]:
        assert not Draft202012Validator(tools["cad_measure"]["inputSchema"]).is_valid(dict(measurement_args,query=bad_query));checks+=1
    checks+=5
    default_presentation = {"clip": None, "explode": {"distance_mm": 0, "directions": []}}
    assert synced["presentation"] == default_presentation
    presentation = {"clip": {"normal": [0, 0, 1], "offset_mm": 4, "keep": "negative"},
                    "explode": {"distance_mm": 20, "directions": [{"part_id": "cover", "direction": [0, 0, 1]}]}}
    visibility_args = {"action": "context", "view_id": "assembly_schema",
                       "evaluation_id": synced["evaluation_id"], "selection": None,
                       "hidden_part_ids": ["cover", "spacer_b"], "presentation": presentation}
    hidden_context = call("cad_viewer", visibility_args)
    assert hidden_context["hidden_part_ids"] == ["cover", "spacer_b"]
    hidden_sync = call("cad_viewer", {"action": "sync", "view_id": "assembly_schema",
                                     "known_evaluation_id": synced["evaluation_id"]})
    assert hidden_sync["changed"] is False and hidden_sync["hidden_part_ids"] == ["cover", "spacer_b"]
    assert call("cad_context", {"view_id": "assembly_schema"})["hidden_part_ids"] == ["cover", "spacer_b"]
    omitted_visibility = {k: v for k, v in visibility_args.items() if k != "hidden_part_ids"}
    assert call("cad_viewer", omitted_visibility)["hidden_part_ids"] == ["cover", "spacer_b"]
    assert call("cad_viewer", {**visibility_args, "hidden_part_ids": []})["hidden_part_ids"] == []
    checks += 6
    assert hidden_context["presentation"] == hidden_sync["presentation"] == presentation
    omitted_presentation = {k: v for k, v in visibility_args.items() if k != "presentation"}
    assert call("cad_viewer", omitted_presentation)["presentation"] == presentation
    assert call("cad_read", {"document_id": aid})["model"] == assembly["model"]
    for bad_presentation in [dict(presentation, script="bad"),
            dict(presentation, clip=dict(presentation["clip"], normal=[0, 0, 2])),
            dict(presentation, explode=dict(presentation["explode"], distance_mm=-1))]:
        assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(
            dict(visibility_args, presentation=bad_presentation))
        checks += 1
    checks += 4
    for invalid_hidden in ["cover", [1], ["cover", "cover"], ["../part"], ["cover"] * 65]:
        assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(
            {**visibility_args, "hidden_part_ids": invalid_hidden})
        checks += 1
    feature = next(f for f in assembly["model"]["features"] if f["id"] == assembly["model"]["output"])
    mate = feature["mates"][0]
    changed = call("cad_apply", {"document_id": aid, "expected_revision": 1, "operations": [
        {"op": "set_mate", "assembly_id": feature["id"], "mate": {**mate, "offset": [0, 0, 2]}}]})
    assert changed["revision"] == 2
    call("cad_apply", {"document_id": aid, "expected_revision": 2, "operations": [
        {"op": "remove_mate", "assembly_id": feature["id"], "mate_id": mate["id"]},
        {"op": "set_part_placement", "assembly_id": feature["id"], "part_id": mate["child"],
         "placement": {"translation": [10, 20, 30]}}]})
    checks += 5
    for operation in [{"op": "set_mate", "assembly_id": feature["id"]},
                      {"op": "remove_mate", "assembly_id": feature["id"], "mate_id": 42},
                      {"op": "set_part_placement", "assembly_id": feature["id"], "part_id": "p", "placement": {"translation": [1, 2]}}]:
        assert not Draft202012Validator(tools["cad_apply"]["inputSchema"]).is_valid(
            {"document_id": aid, "expected_revision": 3, "operations": [operation]})
        checks += 1
    metadata = {"input": "plate", "item_number": 8, "part_number": "P-01",
                "description": 'Plate, "checked"', "material": "Aluminum",
                "purchase": {"supplier": "Example supplier", "part_number": "V-P01",
                             "source_url": "https://example.invalid/V-P01", "artifact_sha256": "a" * 64}}
    metadata_edit = call("cad_apply", {"document_id": aid, "expected_revision": 3, "operations": [
        {"op": "set_bom_item", "assembly_id": feature["id"], "item": metadata}]})
    updated_bom = call("cad_bom", {"document_id": aid, "revision": 4, "feature_id": feature["id"]})
    assert metadata_edit["revision"] == 4
    assert updated_bom["bom"]["items"][-1]["description"] == metadata["description"]
    assert updated_bom["bom"]["items"][-1]["item_number"] == 8
    csv_path = next(a["path"] for a in updated_bom["artifacts"] if a["format"] == "csv")
    with pathlib.Path(csv_path).open(newline="") as stream:
        parsed_rows = list(csv.DictReader(stream))
    assert parsed_rows[-1]["description"] == metadata["description"]
    assert parsed_rows[-1]["supplier_part_number"] == "V-P01"
    package = call("cad_manufacture", {"document_id": aid, "revision": 4,
        "options": {"drawings": False, "parts": [{"feature_id": "plate", "process": "cnc"}]}})
    package_root = pathlib.Path(package["directory"])
    package_manifest = json.loads(pathlib.Path(package["path"]).read_text())
    assert package["part_count"] == 2 and package_manifest["process_review"]["status"] == "not_evaluated"
    assert next(p for p in package_manifest["parts"] if p["feature_id"] == "plate")["purchase"] == metadata["purchase"]
    for artifact in package_manifest["artifacts"]:
        assert not pathlib.Path(artifact["path"]).is_absolute()
        content = (package_root / artifact["path"]).read_bytes()
        assert len(content) == artifact["bytes"] and hashlib.sha256(content).hexdigest() == artifact["sha256"]
        checks += 2
    assert call("cad_read", {"document_id": aid})["model"] == metadata_edit["model"]
    checks += 5
    process_profile = {"process": "fdm", "orientation": {"build_direction": [0, 0, 1], "x_direction": [1, 0, 0]},
                       "minimum_wall_mm": 7, "overhang_angle_deg": 45}
    reviewed = call("cad_fabrication_review", {"document_id": "part", "revision": 3,
                                              "options": {"profile": process_profile}})
    review_bytes = pathlib.Path(reviewed["path"]).read_bytes()
    review_file = json.loads(review_bytes)
    assert len(review_bytes) == reviewed["bytes"] and hashlib.sha256(review_bytes).hexdigest() == reviewed["sha256"]
    assert review_file["source"]["revision"] == 3 and review_file["source"]["native_build"] == reviewed["native_build"]
    findings = {c["id"]: c for c in reviewed["report"]["parts"][0]["checks"]}
    assert findings["sampled_wall_thickness"]["status"] == "fail"
    assert abs(findings["sampled_wall_thickness"]["evidence"]["minimum_sampled_chord_mm"] - 6) < 1e-5
    assert findings["global_minimum_wall"]["status"] == "unknown"
    integrated = call("cad_manufacture", {"document_id": "part", "revision": 3, "options": {
        "drawings": False, "fabrication_review": {"profile": process_profile}}})
    integrated_manifest = json.loads(pathlib.Path(integrated["path"]).read_text())
    assert integrated_manifest["process_review"] == {"status": "fail", "report_path": "review.json"}
    integrated_review = next(a for a in integrated_manifest["artifacts"] if a["path"] == "review.json")
    assert hashlib.sha256((pathlib.Path(integrated["directory"]) / "review.json").read_bytes()).hexdigest() == integrated_review["sha256"]
    assert call("cad_read", {"document_id": "part"})["revision"] == 3
    checks += 9
    for bad_profile in [{"process": "cnc", "orientation": process_profile["orientation"], "overhang_angle_deg": 45},
                        {"process": "sheet_laser", "orientation": process_profile["orientation"], "sheet_thickness_mm": 2}]:
        assert not Draft202012Validator(tools["cad_fabrication_review"]["inputSchema"]).is_valid(
            {"document_id": "part", "revision": 3, "options": {"profile": bad_profile}})
        checks += 1
    assert call("cad_bom", {"document_id": aid, "revision": 1})["bom"] == bom["bom"]
    call("cad_apply", {"document_id": aid, "expected_revision": 4, "operations": [
        {"op": "remove_bom_item", "assembly_id": feature["id"], "input": "plate"}]})
    assert call("cad_bom", {"document_id": aid, "revision": 5})["bom"] == bom["bom"]
    checks += 6
    for operation in [
        {"op": "set_bom_item", "assembly_id": feature["id"]},
        {"op": "remove_bom_item", "assembly_id": feature["id"], "input": 42},
        *[{"op": "set_bom_item", "assembly_id": feature["id"], "item": {"input": "plate", "item_number": n}}
          for n in [0, 1000, 1.5]],
        *[{"op": "set_bom_item", "assembly_id": feature["id"], "item": {"input": "plate", "description": value}}
          for value in ["line\n", "\u00e9", "x" * 121]],
        {"op": "set_bom_item", "assembly_id": feature["id"], "item": {"input": "plate", "quantity": 2}}]:
        assert not Draft202012Validator(tools["cad_apply"]["inputSchema"]).is_valid(
            {"document_id": aid, "expected_revision": 5, "operations": [operation]})
        checks += 1
    for bad in [{"bom": "yes"}, {"bom": True, "balloons": [{"view": "front", "part_id": "base", "anchor": [1, 2], "label": [1, 2]}]},
                {"bom": True, "balloons": [{"view": "front", "part_id": "base", "anchor": [1, 2, 3], "label": [1, 2, 3]}]},
                {"bom": True, "balloons": [{"view": "front", "part_id": "base", "anchor": [1, 2, 3], "label": [1, 2], "item_number": 1}]}]:
        assert not Draft202012Validator(tools["cad_drawing"]["inputSchema"]).is_valid(
            {"document_id": aid, "revision": 1, "drawing": bad})
        checks += 1
    nested_example = json.loads((source / "examples/nested-assembly.create.json").read_text())
    nested = call("cad_create", nested_example)
    nid = nested["document_id"]
    leaves = {"left/foot", "left/link", "right/foot", "right/link", "spare"}
    assert {part["id"] for part in nested["summary"]["assembly"]["parts"]} == leaves
    nested_mesh = call("cad_query", {"document_id": nid, "revision": 1, "kind": "mesh"})
    assert {face["part_id"] for face in nested_mesh["topology"]["faces"]} == leaves
    nested_bom = call("cad_bom", {"document_id": nid, "revision": 1})["bom"]
    assert nested_bom["total_quantity"] == 5 and nested_bom["structure"][0]["bom"]["part_number"] == "MODULE"
    call("cad_drawing", {"document_id": nid, "revision": 1, "drawing": {
        "views": [{"id": "top", "orientation": "top", "explode": [{"part_id": "left", "translation": [0, 0, 20]}]}],
        "bom": True, "balloons": [{"view": "top", "part_id": "left/foot", "anchor": [5, 2, 2], "label": [-5, 15]}]}})
    call("cad_open", {"document_id": nid, "view_id": "nested_schema"})
    for _ in range(300):
        nested_view = call("cad_viewer", {"action": "sync", "view_id": "nested_schema"})
        if nested_view["state"] == "ready":
            break
        time.sleep(0.02)
    assert nested_view["state"] == "ready"
    review_appearance={"default_color":[.2,.3,.4],"parts":[{"part_id":"left/foot","color":[1,0,0]},{"part_id":"spare","color":[0,1,0]}]}
    review_camera={"yaw":.2,"pitch":.4,"zoom":2,"pan":[.1,.2]}
    appearance_context={"action":"context","view_id":"nested_schema","evaluation_id":nested_view["evaluation_id"],"selection":None,"appearance":review_appearance,"camera":review_camera}
    colored=call("cad_viewer",appearance_context)
    assert colored["appearance"]==review_appearance and colored["presets"]==[]
    preset_action={"action":"preset","view_id":"nested_schema","evaluation_id":nested_view["evaluation_id"]}
    saved=call("cad_viewer",dict(preset_action,operation="save",name="Assembly review"))
    assert saved["presets"][0]["appearance"]==review_appearance and saved["presets"][0]["camera"]==review_camera
    call("cad_viewer",dict(appearance_context,appearance={"default_color":[.4,.4,.4],"parts":[]}))
    applied=call("cad_viewer",dict(preset_action,operation="apply",name="Assembly review"))
    assert applied["appearance"]==review_appearance and applied["camera"]==review_camera and applied["selection"] is None
    assert len(call("cad_viewer",dict(preset_action,operation="list"))["presets"])==1
    assert call("cad_viewer",dict(preset_action,operation="delete",name="Assembly review"))["presets"]==[]
    for bad in [dict(appearance_context,appearance={"default_color":[0,2,0],"parts":[]}),dict(appearance_context,appearance={"default_color":[0,0,0],"parts":[],"opacity":.5}),dict(preset_action,operation="save"),dict(preset_action,operation="list",name="extra"),dict(preset_action,operation="save",name="x"*65),dict(preset_action,operation="shell",name="bad")]:
        assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(bad)
        checks+=1
    checks+=7
    call("cad_viewer", {"action": "context", "view_id": "nested_schema", "evaluation_id": nested_view["evaluation_id"],
                        "selection": None, "hidden_part_ids": ["left/foot", "left/link"]})
    assert call("cad_context", {"view_id": "nested_schema"})["hidden_part_ids"] == ["left/foot", "left/link"]
    call("cad_viewer", {"action": "motion_preview", "view_id": "nested_schema", "evaluation_id": nested_view["evaluation_id"],
                        "assembly_id": "module", "values": [{"mate_id": "pivot", "coordinate": "angle_deg", "value": 0}]})
    for _ in range(300):
        nested_view = call("cad_viewer", {"action": "sync", "view_id": "nested_schema"})
        if nested_view["state"] == "ready":
            break
        time.sleep(0.02)
    assert nested_view["state"] == "ready" and nested_view["draft"] and nested_view["feature_id"] == "machine"
    assert nested_view["summary"]["assembly"]["mechanisms"][0]["occurrences"] == ["left", "right"]
    draft_presets=call("cad_viewer",{"action":"preset","operation":"list","view_id":"nested_schema","evaluation_id":nested_view["evaluation_id"]})
    assert draft_presets["evaluation_id"]==nested_view["evaluation_id"] and draft_presets["revision"]==nested_view["revision"] and draft_presets["feature_id"]==nested_view["feature_id"] and not draft_presets["stale"] and draft_presets["draft"]
    assert draft_presets["selection"] is None and "camera" not in draft_presets
    checks+=2

    call("cad_viewer", {"action": "motion_save", "view_id": "nested_schema", "evaluation_id": nested_view["evaluation_id"],
                        "assembly_id": "module", "pose_id": "nested_review"})
    for _ in range(300):
        nested_view = call("cad_viewer", {"action": "sync", "view_id": "nested_schema"})
        if nested_view["state"] == "ready":
            break
        time.sleep(0.02)
    assert nested_view["state"] == "ready" and not nested_view["draft"] and nested_view["revision"] == 2
    call("cad_robot_export", {"document_id": nid, "revision": 2, "robot": {"format": "urdf", "joint_properties": [
        {"mate_id": path + "/pivot", "coordinate": "angle_deg", "effort": 5, "velocity": 0.2} for path in ["left", "right"]]}})
    assert call("cad_read", {"document_id": nid, "revision": 1})["model"] == nested_example["model"]
    checks += 4
    component_base = {"schema_version": 1, "units": "mm", "parameters": {"angle": 45},
                      "features": [{"id": "seed", "type": "box", "size": [1, 1, 1]}], "output": "seed"}
    call("cad_create", {"document_id": "component_schema", "model": component_base})
    component_op = {"op": "set_component", "id": "library_machine", "source_document_id": nid, "source_revision": 1,
                    "bindings": {"angle": {"parameter": "angle"}}}
    component_edits = [component_op, {"op": "set_output", "feature_id": "library_machine"}]
    call("cad_preview", {"document_id": "component_schema", "expected_revision": 1, "kind": "mesh", "operations": component_edits})
    imported = call("cad_apply", {"document_id": "component_schema", "expected_revision": 1, "operations": component_edits})
    assert imported["summary"]["components"][0]["source"]["revision"] == 1 and not imported["summary"]["components"][0]["modified"]
    assert imported["model"]["components"][0]["snapshot"] == nested_example["model"]
    call("cad_read", {"document_id": "component_schema"})
    call("cad_create", {"document_id": "copied_component_schema", "model": imported["model"]})
    call("cad_open", {"document_id": "component_schema", "view_id": "component_schema"})
    for _ in range(300):
        component_view = call("cad_viewer", {"action": "sync", "view_id": "component_schema"})
        if component_view["state"] == "ready":
            break
        time.sleep(.02)
    assert component_view["state"] == "ready" and component_view["summary"]["components"] == imported["summary"]["components"]
    for patch in [{"source_revision": 0}, {"discard_local_changes": "yes"}, {"bindings": {"angle": "eval(45)"}}]:
        assert not Draft202012Validator(tools["cad_apply"]["inputSchema"]).is_valid({"document_id": "component_schema", "expected_revision": 2,
            "operations": [dict(component_op, **patch)]})
        checks += 1
    call("cad_apply", {"document_id": "component_schema", "expected_revision": 2,
                       "operations": [{"op": "detach_component", "id": "library_machine"}]})
    call("cad_apply", {"document_id": "copied_component_schema", "expected_revision": 1,
                       "operations": [{"op": "set_output", "feature_id": "seed"}, {"op": "remove_component", "id": "library_machine"}]})
    checks += 3
    checks += 5
    # New curves remain editable native documents on every process invocation;
    # validate the actual service's input/output schemas, including curve mesh
    # and drawing publication, not just JSON shapes in isolation.
    for name in ["curved-plate", "curved-pipe"]:
        example = json.loads((source / "examples" / f"{name}.create.json").read_text())
        created_curve = call("cad_create", example)
        cid = created_curve["document_id"]
        call("cad_query", {"document_id": cid, "revision": 1, "kind": "mesh"})
        call("cad_export", {"document_id": cid, "revision": 1, "format": "step"})
        parameter, value = ("crown", 30) if name == "curved-plate" else ("rise", 25)
        edits = [{"op": "set_parameter", "name": parameter, "value": value}]
        call("cad_preview", {"document_id": cid, "expected_revision": 1, "operations": edits})
        call("cad_apply", {"document_id": cid, "expected_revision": 1, "operations": edits})
        assert call("cad_read", {"document_id": cid, "revision": 1})["model"] == example["model"]
        checks += 1
        if name == "curved-plate":
            call("cad_drawing", {"document_id": cid, "revision": 2,
                "drawing": {"views": [{"id": "top", "orientation": "top"}]}})
    mechanism = json.loads((source / "examples/articulated-arm.create.json").read_text())
    mid = mechanism["document_id"]
    call("cad_create", mechanism)
    call("cad_open", {"document_id": mid, "view_id": "playback_schema"})
    for _ in range(500):
        timeline = call("cad_viewer", {"action": "sync", "view_id": "playback_schema"})
        if timeline["state"] == "ready": break
        time.sleep(.01)
    assert timeline["state"] == "ready"
    playback_base = {"action": "sequence", "view_id": "playback_schema", "evaluation_id": timeline["evaluation_id"]}
    playback_source = call("cad_read", {"document_id": mid})
    def playback_frame(time_s, angle, travel, distance):
        return {"time_s": time_s, "presentation": {"clip": None, "explode": {"distance_mm": distance, "directions": []}},
                "joints": [{"assembly_id": "mechanism", "values": [{"mate_id": "hinge", "coordinate": "angle_deg", "value": angle}, {"mate_id": "spindle_joint", "coordinate": "travel_mm", "value": travel}]}]}
    sequence = {"name": "Schema coordinated playback", "frames": [playback_frame(0, 0, 0, 0), playback_frame(2, 90, 18, 10)]}
    sequence_saved = call("cad_viewer", dict(playback_base, operation="save", sequence=sequence))
    assert sequence_saved["sequences"][0]["source"]["evaluation_id"] == timeline["evaluation_id"]
    options = call("cad_viewer", dict(playback_base, operation="options", name=sequence["name"], speed=1.5, loop=True))
    assert options["playback"]["state"] == "unapplied"
    call("cad_viewer", dict(playback_base, operation="seek", name=sequence["name"], time_s=1))
    for _ in range(500):
        timeline = call("cad_viewer", {"action": "sync", "view_id": "playback_schema"})
        if timeline["state"] == "ready": break
        time.sleep(.01)
    assert timeline["state"] == "ready" and timeline["draft"] and timeline["playback"]["state"] == "displayed" and timeline["playback"]["time_s"] == 1
    dofs = timeline["summary"]["assembly"]["motion"]["dofs"]
    assert next(v["value"] for v in dofs if v["mate_id"] == "hinge") == 45
    assert next(v["value"] for v in dofs if v["mate_id"] == "rail") == 4.5
    assert timeline["presentation"]["explode"]["distance_mm"] == 5 and call("cad_read", {"document_id": mid}) == playback_source
    playback_base["evaluation_id"] = timeline["evaluation_id"]
    listed = call("cad_viewer", dict(playback_base, operation="list"))
    assert listed["evaluation_id"] == timeline["evaluation_id"] and not listed["stale"]
    assert call("cad_context", {"view_id": "playback_schema"})["playback"]["time_s"] == 1
    assert call("cad_viewer", dict(playback_base, operation="delete", name=sequence["name"]))["sequences"] == []
    for bad in [dict(playback_base, operation="seek", name=sequence["name"], time_s=3601), dict(playback_base, operation="list", script="forbidden"), dict(playback_base, operation="options", name=sequence["name"], speed=5), dict(playback_base, operation="save", sequence=dict(sequence, script="forbidden")), dict(playback_base, operation="save", sequence=dict(sequence, frames=sequence["frames"][:1]))]:
        assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(bad)
        checks += 1
    checks += 8
    robot_args = json.loads((source / "examples/articulated-arm.robot.json").read_text())
    call("cad_robot_export", robot_args)
    robot_args["robot"]["format"] = "srdf"
    call("cad_robot_export", robot_args)
    robot_args["robot"]["format"] = "sdf"
    robot_args["robot"]["inertials"] = [{"link": name, "mass_kg": .1,
        "center_of_mass_m": [0,0,0], "inertia_kg_m2": [.00002,.00003,.00004,0,0,0]}
        for name in ["part_ground","part_lever","part_slide","part_spindle","carrier_spindle_joint"]]
    call("cad_robot_export", robot_args)
    for bad in [{"format":"urdf"}, {"format":"dae","joint_properties":[]},
                {"format":"urdf","joint_properties":[{"mate_id":"hinge","coordinate":"angle_deg","effort":-1,"velocity":1}]}]:
        assert not Draft202012Validator(tools["cad_robot_export"]["inputSchema"]).is_valid({"document_id":mid,"revision":1,"robot":bad})
        checks += 1
    edits = [{"op": "apply_pose", "assembly_id": "mechanism", "pose_id": "extended"}]
    preview = call("cad_preview", {"document_id": mid, "expected_revision": 1, "operations": edits, "kind": "mesh"})
    assert preview["draft"] and "mesh" in preview and "path" not in preview
    incomplete = {k:v for k,v in preview.items() if k not in ["mesh","topology"]}
    assert not Draft202012Validator(tools["cad_preview"]["outputSchema"]).is_valid(incomplete)
    assert not Draft202012Validator(tools["cad_preview"]["outputSchema"]).is_valid(dict(preview, draft=False))
    checks += 2
    call("cad_open", {"document_id": mid, "view_id": "motion_schema"})

    def motion_ready(revision):
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            shown = call("cad_viewer", {"action": "sync", "view_id": "motion_schema"})
            if shown["state"] == "ready" and shown["revision"] == revision:
                return shown
            assert shown["state"] == "loading", shown
            time.sleep(.02)
        raise AssertionError("Motion view did not become ready")

    shown = motion_ready(1)
    call("cad_viewer", {"action": "motion_preview", "view_id": "motion_schema", "evaluation_id": shown["evaluation_id"], "pose_id": "extended"})
    shown = motion_ready(1)
    context = call("cad_viewer", {"action": "context", "view_id": "motion_schema", "evaluation_id": shown["evaluation_id"], "selection": None})
    assert context["draft"] and not context["stale"] and context["preview_operations"] == edits
    call("cad_context", {"view_id": "motion_schema"})
    call("cad_viewer", {"action": "motion_reset", "view_id": "motion_schema", "evaluation_id": shown["evaluation_id"]})
    shown = motion_ready(1)
    values = [{"mate_id": "hinge", "coordinate": "angle_deg", "value": 60}, {"mate_id": "spindle_joint", "coordinate": "travel_mm", "value": 10}]
    call("cad_viewer", {"action": "motion_preview", "view_id": "motion_schema", "evaluation_id": shown["evaluation_id"], "values": values})
    shown = motion_ready(1)
    call("cad_viewer", {"action": "motion_save", "view_id": "motion_schema", "evaluation_id": shown["evaluation_id"], "pose_id": "inspection"})
    shown = motion_ready(2)
    assert not shown["draft"] and "inspection" in shown["summary"]["assembly"]["motion"]["poses"]
    call("cad_query", {"document_id": mid, "revision": 2, "kind": "mesh"})
    for fmt in ["step", "stl"]:
        call("cad_export", {"document_id": mid, "revision": 2, "format": fmt})
    call("cad_drawing", {"document_id": mid, "revision": 2, "drawing": {"views": [{"id": "top", "orientation": "top"}]}})
    assert call("cad_read", {"document_id": mid, "revision": 1})["model"] == mechanism["model"]
    checks += 4
    # Native section reports have their own material mesh and result-local IDs;
    # validate the actual service through the independent Draft 2020-12 reader.
    ring_model={"schema_version":1,"units":"mm","parameters":{"radius":5},"features":[
        {"id":"outer","type":"cylinder","radius":{"parameter":"radius"},"height":10},
        {"id":"inner","type":"cylinder","radius":2,"height":10},
        {"id":"ring","type":"cut","left":"outer","right":"inner"}],"output":"ring"}
    call("cad_create",{"document_id":"section_schema","model":ring_model})
    ring_eval=call("cad_query",{"document_id":"section_schema","revision":1,"kind":"topology"})
    section_query={"action":"section","plane":{"normal":[0,0,1],"offset_mm":5}}
    section_args={"document_id":"section_schema","revision":1,"evaluation_id":ring_eval["evaluation_id"],"feature_id":"ring","query":section_query}
    section_result=call("cad_measure",section_args)
    assert math.isclose(section_result["report"]["area_mm2"],21*math.pi,abs_tol=1e-6)
    assert section_result["report"]["regions"][0]["wire_count"]==2 and section_result["report"]["mesh"]["triangles"]
    assert all(item.startswith("cap-") for item in section_result["report"]["mesh"]["triangle_regions"])
    oblique_eval=call("cad_query",{"document_id":"section_schema","revision":1,"kind":"topology","feature_id":"outer"})
    oblique=call("cad_measure",dict(section_args,evaluation_id=oblique_eval["evaluation_id"],feature_id="outer",
        query={"action":"section","plane":{"normal":[1/math.sqrt(2),0,1/math.sqrt(2)],"offset_mm":5/math.sqrt(2)}}))
    assert oblique["report"]["curves"][0]["curve_kind"]=="ellipse"
    checks+=4
    for bad in [dict(section_query,script="eval()"),dict(section_query,part_ids=[]),
                dict(section_query,plane={"normal":[0,0,1],"offset_mm":1e13}),
                dict(section_query,plane={"normal":[0,1],"offset_mm":5}),
                dict(section_query,explode={"distance_mm":-1,"directions":[]})]:
        assert not Draft202012Validator(tools["cad_measure"]["inputSchema"]).is_valid(dict(section_args,query=bad));checks+=1
    changed_report=json.loads(json.dumps(section_result));changed_report["report"]["mesh"]["triangle_regions"][0]="face-1"
    assert not Draft202012Validator(tools["cad_measure"]["outputSchema"]).is_valid(changed_report);checks+=1
    call("cad_open",{"document_id":"section_schema","view_id":"section_schema"})
    deadline=time.monotonic()+15
    while True:
        section_view=call("cad_viewer",{"action":"sync","view_id":"section_schema"})
        if section_view["state"]=="ready":break
        assert section_view["state"]=="loading" and time.monotonic()<deadline;time.sleep(.02)
    section_presentation={"clip":{"normal":[0,0,1],"offset_mm":5,"keep":"negative"},"explode":{"distance_mm":0,"directions":[]}}
    section_context={"action":"context","view_id":"section_schema","evaluation_id":section_view["evaluation_id"],"selection":None,"presentation":section_presentation}
    call("cad_viewer",section_context)
    section_action={"action":"section","view_id":"section_schema","evaluation_id":section_view["evaluation_id"]}
    section_started=call("cad_viewer",dict(section_action,query=section_query))
    deadline=time.monotonic()+15
    while True:
        section_done=call("cad_viewer",section_action)
        if section_done["state"]=="succeeded":break
        assert section_done["state"] in ["queued","running"] and time.monotonic()<deadline;time.sleep(.02)
    assert math.isclose(section_done["result"]["report"]["area_mm2"],21*math.pi,abs_tol=1e-6)
    assert call("cad_context",{"view_id":"section_schema"})["section"]["job_id"]==section_started["job_id"]
    section_presentation["clip"]["keep"]="positive"
    assert call("cad_viewer",section_context)["section"]["job_id"]==section_started["job_id"]
    assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(dict(section_action,action="measure",query=section_query));checks+=4
    assert call("cad_viewer",dict(section_action,query=None))["state"]=="empty";checks+=1
    section_job=call("cad_job",{"action":"submit","request_id":"section_schema_job","tool":"cad_measure","arguments":section_args})
    deadline=time.monotonic()+15
    while True:
        section_job_done=call("cad_job",{"action":"get","job_id":section_job["job_id"]})
        if section_job_done["state"]=="succeeded":break
        assert section_job_done["state"] in ["queued","running"] and time.monotonic()<deadline;time.sleep(.02)
    call("cad_apply",{"document_id":"section_schema","expected_revision":1,"operations":[{"op":"set_parameter","name":"radius","value":6}]})
    assert call("cad_job",{"action":"get","job_id":section_job["job_id"]})["result"]==section_job_done["result"]
    checks+=1
    # Review notes use native inspection centers and independently closed schemas.
    call("cad_create",{"document_id":"annotation_schema","model":{"schema_version":1,"units":"mm","parameters":{},"features":[{"id":"box","type":"box","size":[20,10,6]}],"output":"box"}})
    call("cad_open",{"document_id":"annotation_schema","view_id":"annotation_schema"})
    deadline=time.monotonic()+15
    while True:
        note_view=call("cad_viewer",{"action":"sync","view_id":"annotation_schema"})
        if note_view["state"]=="ready":break
        assert note_view["state"]=="loading" and time.monotonic()<deadline;time.sleep(.02)
    note_action={"action":"annotation","view_id":"annotation_schema","evaluation_id":note_view["evaluation_id"]}
    note_result=call("cad_viewer",dict(note_action,operation="add",anchor={"kind":"model"},text="Inspect clearance <plain text>"))
    note=note_result["annotations"][0]
    assert note["status"]=="current" and note["anchor"]["point_mm"]==[10,5,3]
    assert note["anchor_lifetime"]=="evaluation" and note["coordinate_space"]=="committed_source_pose"
    assert call("cad_context",{"view_id":"annotation_schema"})["annotations"]==[note]
    checks+=3
    note_topology=call("cad_query",{"document_id":"annotation_schema","revision":1,"kind":"topology"})
    note_ref={"document_id":"annotation_schema","revision":1,"evaluation_id":note_view["evaluation_id"],"feature_id":"box","kind":"face","entity_id":note_topology["topology"]["faces"][0]["id"]}
    note_result=call("cad_viewer",dict(note_action,operation="add",anchor={"kind":"entity","reference":note_ref},text="Native face inspection center"))
    assert note_result["annotations"][1]["anchor"]["point_mm"]==note_topology["topology"]["faces"][0]["center_mm"];checks+=1
    note_result=call("cad_viewer",dict(note_action,operation="update",annotation_id=note["id"],text="Edited note"))
    assert note_result["annotations"][0]["anchor"]==note["anchor"];checks+=1
    invalid_notes=[dict(note_action,operation="add",anchor={"kind":"model","point_mm":[0,0,0]},text="bad"),
        dict(note_action,operation="add",anchor={"kind":"model"},text="x"*513),
        dict(note_action,operation="add",anchor={"kind":"model"},text="bad\u0001"),
        dict(note_action,operation="update",annotation_id=note["id"],text="bad",anchor={"kind":"model"}),
        dict(note_action,operation="delete",annotation_id=note["id"],script="x")]
    for bad in invalid_notes:
        assert not Draft202012Validator(tools["cad_viewer"]["inputSchema"]).is_valid(bad);checks+=1
    malformed=json.loads(json.dumps(note_result));malformed["annotations"][0]["anchor_lifetime"]="stable"
    assert not Draft202012Validator(tools["cad_viewer"]["outputSchema"]).is_valid(malformed);checks+=1
    call("cad_apply",{"document_id":"annotation_schema","expected_revision":1,"operations":[{"op":"replace_feature","id":"box","feature":{"id":"box","type":"box","size":[21,10,6]}}]})
    historical=call("cad_context",{"view_id":"annotation_schema"})["annotations"]
    assert all(item["status"]=="retired" and item["evaluation_id"]==note_view["evaluation_id"] for item in historical);checks+=1
    deadline=time.monotonic()+15
    while True:
        note_view2=call("cad_viewer",{"action":"sync","view_id":"annotation_schema"})
        if note_view2["state"]=="ready":break
        assert note_view2["state"]=="loading" and time.monotonic()<deadline;time.sleep(.02)
    note_current=dict(note_action,evaluation_id=note_view2["evaluation_id"])
    assert call("cad_viewer",dict(note_current,operation="list"))["annotations"]==historical;checks+=1
    assert len(call("cad_viewer",dict(note_current,operation="delete",annotation_id=note["id"]))["annotations"])==1;checks+=1
    assert not call("cad_viewer",dict(note_current,operation="clear"))["annotations"];checks+=1
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
