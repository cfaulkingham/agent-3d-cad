"""Independent SDK interoperability check against the real native stdio server.

Developer setup: python -m pip install -r tests/mcp_sdk_requirements.txt
Run: python tests/mcp_sdk_smoke.py /absolute/path/to/agent-3d-cad

The official MCP SDK owns process launch, framing, request correlation, modern
probe fallback, initialize/initialized, typed decoding and orderly shutdown.
This is distinct from schema_conformance.py's manually constructed RPC frames.
Nothing is registered in a host application; every model lives in a temporary
workspace. Python and the SDK are test dependencies, not product dependencies.

Primary references, verified 2026-10-06:
https://github.com/modelcontextprotocol/python-sdk/releases/tag/v2.3.0
https://py.sdk.modelcontextprotocol.io/client/
https://py.sdk.modelcontextprotocol.io/protocol-versions/
"""

import asyncio
import base64
import csv
import io
import importlib.metadata
import json
import hashlib
import math
import os
from pathlib import Path
import sys
import tempfile

from jsonschema import Draft202012Validator
from mcp import Client, StdioServerParameters
from slicer_contract_fixture import options as slice_options, verify_package


EXPECTED_SDK = "2.3.0"
APP_URI = "ui://agent-3d-cad/viewer.html"
APP_MIME = "text/html;profile=mcp-app"
TERMINAL = {"succeeded", "failed", "cancelled", "interrupted"}
checks = 0


def require(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1


def box_model():
    return {
        "schema_version": 1,
        "units": "mm",
        "parameters": {"height": 6},
        "features": [{"id": "base", "type": "box", "size": [20, 10, {"parameter": "height"}]}],
        "output": "base",
    }


def heavy_edits():
    # A valid model that is heavy inside the per-feature replication budget:
    # a 64 x 64 pin grid (4,096 solids) cut through one plate by one Boolean.
    features = [
        {"id": "plate", "type": "box", "size": [1300, 1300, 5], "origin": [-10, -10, 0]},
        {"id": "pin", "type": "cylinder", "radius": 4, "height": 20, "origin": [0, 0, -5]},
        {"id": "row", "type": "pattern", "input": "pin", "count": 64, "step": [20, 0, 0]},
        {"id": "grid", "type": "pattern", "input": "row", "count": 64, "step": [0, 20, 0]},
        {"id": "perforated", "type": "cut", "left": "plate", "right": "grid"},
    ]
    return [{"op": "add_feature", "feature": feature} for feature in features] + [
        {"op": "set_output", "feature_id": "perforated"}]


async def discover(client):
    require(client.protocol_version == "2025-11-25", "SDK did not negotiate the server's advertised baseline")
    require(client.session.initialize_result is not None, "SDK did not complete the initialize handshake")
    require(client.server_info.name == "agent-3d-cad", "Unexpected native server identity")
    require(client.server_capabilities.tools is not None, "Server did not advertise tools")
    require(client.server_capabilities.resources is not None, "Server did not advertise app resources")
    result = await client.list_tools()
    definitions = {tool.name: tool for tool in result.tools}
    require({"cad_create", "cad_read", "cad_apply", "cad_export", "cad_bom", "cad_drawing", "cad_gcode_review", "cad_printer_handoff", "cad_slice", "cad_job", "cad_open", "cad_show", "cad_context", "cad_list", "cad_viewer"} <= definitions.keys(),
            "Required editable CAD tools were not discovered")
    require(result.next_cursor is None, "Unexpected unhandled tool pagination")
    for tool in definitions.values():
        require(tool.output_schema is not None, f"Missing output schema for {tool.name}")
        Draft202012Validator.check_schema(tool.input_schema)
        Draft202012Validator.check_schema(tool.output_schema)
        require(True, f"Input/output schemas valid for {tool.name}")
    ui_tools = {name: tool.model_dump(by_alias=True) for name, tool in definitions.items()}
    require(ui_tools["cad_open"]["_meta"]["ui"]["resourceUri"] == APP_URI, "Open does not link the app resource")
    require(ui_tools["cad_viewer"]["_meta"]["ui"]["visibility"] == ["app"], "Viewer internals are not app-only")
    require("resourceUri" not in (ui_tools["cad_show"].get("_meta") or {}).get("ui", {}), "Show would open another app")
    resources = (await client.list_resources()).model_dump(by_alias=True, mode="json")
    require(len(resources["resources"]) == 1 and resources["resources"][0]["uri"] == APP_URI,
            "SDK resource discovery did not find the app")
    resource = (await client.read_resource(APP_URI)).model_dump(by_alias=True, mode="json")
    require(len(resource["contents"]) == 1, "Expected one app HTML resource")
    content = resource["contents"][0]
    require(content["uri"] == APP_URI and content["mimeType"] == APP_MIME, "Incorrect app resource identity")
    require("<canvas" in content["text"] and "CadBridge" in content["text"] and "@VIEWER_" not in content["text"],
            "SDK did not decode the compiled self-contained app")
    csp = content["_meta"]["ui"]["csp"]
    require(csp["connectDomains"] == [] and csp["resourceDomains"] == [], "App unexpectedly requests external network access")
    return definitions


async def call(client, definitions, name, arguments, *, expected_error=None, timeout=15):
    definition = definitions[name]
    Draft202012Validator(definition.input_schema).validate(arguments)
    # workspace_busy is the explicitly documented nonblocking publication/queue
    # contention result, not a reason to retry an arbitrary failed mutation.
    for attempt in range(100):
        result = await client.call_tool(name, arguments, read_timeout_seconds=timeout)
        value = result.structured_content
        if (result.is_error and isinstance(value, dict)
                and value.get("error", {}).get("code") == "workspace_busy"):
            await asyncio.sleep(0.01)
            continue
        break
    else:
        raise AssertionError("Workspace stayed busy during SDK smoke test")
    require(isinstance(value, dict), f"{name} did not return structured content")
    exported = value.get("result", {}) if value.get("state") == "succeeded" else value
    links = exported.get("downloads", []) if not result.is_error else []
    require(len(result.content) == 1 + len(links) and result.content[0].type == "text",
            f"{name} did not return the compatible JSON text content")
    require(json.loads(result.content[0].text) == value, f"{name} text and structured content disagree")
    for item, link in zip(result.content[1:], links):
        require(item.type == "resource_link" and item.model_dump(mode="json", by_alias=True, exclude_none=True) == link,
                f"{name} native resource link differs from its structured descriptor")
        paths = [entry["path"] for entry in exported.get("artifacts", exported.get("plates", []))]
        paths += [exported[key] for key in ("path", "layout_path") if key in exported]
        matching = {Path(path) for path in paths if Path(path).name == link["name"]}
        require(len(matching) == 1, "Download names exactly one native exported file")
        resource = (await client.read_resource(link["uri"])).model_dump(mode="json", by_alias=True)
        require(len(resource["contents"]) == 1, "SDK reads one export resource")
        content = resource["contents"][0]
        require(content["uri"] == link["uri"] and content["mimeType"] == link["mimeType"], "SDK preserves export resource identity")
        data = base64.b64decode(content["blob"], validate=True)
        require(data == matching.pop().read_bytes() and len(data) == link["size"], "SDK base64 recovers exact exported bytes")
    if expected_error is not None:
        require(result.is_error and value["error"]["code"] == expected_error,
                f"Expected structured {expected_error} error, received {value}")
    else:
        require(not result.is_error, f"{name} failed: {value}")
        Draft202012Validator(definition.output_schema).validate(value)
        require(True, f"{name} result satisfies independently discovered output schema")
    return value


async def poll(client, definitions, job_id):
    for _ in range(500):
        job = await call(client, definitions, "cad_job", {"action": "get", "job_id": job_id})
        if job["state"] in TERMINAL:
            return job
        await asyncio.sleep(0.01)
    raise AssertionError(f"Job {job_id} never reached a terminal state")


async def wait_for_native_build(workspace):
    # Observation only: native CTest uses the same worker diagnostic marker.
    # Waiting for a real worker avoids mistaking a completed fast job for evidence
    # that the SDK can exchange messages while OCCT is actually building.
    for _ in range(1000):
        if any(workspace.glob(".workers/*/building.json")):
            return
        await asyncio.sleep(0.005)
    raise AssertionError("Asynchronous job never entered a native geometry build")


async def smoke(executable, workspace):
    params = StdioServerParameters(command=str(executable), args=["serve", "--workspace", str(workspace)],
                                  env=dict(os.environ))
    created = None
    # Default auto mode independently probes modern discovery, then negotiates
    # the legacy lifecycle; the test never manufactures those protocol frames.
    async with Client(params, read_timeout_seconds=15) as client:
        definitions = await discover(client)
        example_dir = Path(__file__).resolve().parents[1] / "examples"
        assembly_args = json.loads((example_dir / "assembly.create.json").read_text())
        assembly = await call(client, definitions, "cad_create", assembly_args)
        require(len(assembly["summary"]["assembly"]["parts"]) >= 2, "SDK assembly inventory is missing")
        assembly_mesh = await call(client, definitions, "cad_query", {
            "document_id": assembly_args["document_id"], "revision": 1, "kind": "mesh"})
        require(all("part_id" in f for f in assembly_mesh["topology"]["faces"]), "SDK assembly faces lost part identity")
        assembly_recipe = json.loads((example_dir / "assembly.drawing.json").read_text())
        assembly_job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_assembly_drawing", "tool": "cad_drawing", "arguments": assembly_recipe})
        assembly_done = await poll(client, definitions, assembly_job["job_id"])
        require(assembly_done["state"] == "succeeded", "SDK exploded drawing job failed")
        require(json.loads(Path(assembly_done["result"]["recipe_path"]).read_text())["drawing"] == assembly_recipe["drawing"],
                "SDK exploded recipe did not round trip")
        aid = assembly_args["document_id"]
        gcode_path = workspace / "sdk fixture.gcode"
        gcode_bytes = b"G21\nG90\nM83\nM104S210\nM140S60\nG1X1Y1Z.2E1\nM117 firmware message\n"
        gcode_path.write_bytes(gcode_bytes)
        gcode_args = {"document_id": aid, "revision": 1, "path": str(gcode_path),
                      "expected_sha256": hashlib.sha256(gcode_bytes).hexdigest(), "options": {
                          "firmware": "marlin", "machine": {"name": "SDK fixture", "motion_bounds_mm": [[-20, 20], [-20, 20], [0, 100]]},
                          "material": {"name": "Fixture PLA", "nozzle_temperature_c": [190, 230], "bed_temperature_c": [50, 70]},
                          "initial": {"units": "mm", "xyz_mode": "absolute", "extrusion_mode": "absolute", "position_mm": [0, 0, 0], "extruder_mm": 0}}}
        gcode_job = await call(client, definitions, "cad_job", {"action": "submit", "request_id": "sdk_gcode", "tool": "cad_gcode_review", "arguments": gcode_args})
        gcode_done = await poll(client, definitions, gcode_job["job_id"])
        require(gcode_done["state"] == "succeeded", "SDK G-code review job failed")
        gcode_review = gcode_done["result"]
        require(gcode_review["report"]["status"] == "unknown", "SDK hid an unsupported firmware command")
        require(Path(gcode_review["artifact_path"]).read_bytes() == gcode_bytes, "SDK changed original G-code bytes")
        review_bytes = Path(gcode_review["path"]).read_bytes()
        require(hashlib.sha256(review_bytes).hexdigest() == gcode_review["report_sha256"] and len(review_bytes) == gcode_review["report_bytes"], "SDK report identity differs from raw bytes")
        await call(client, definitions, "cad_gcode_review", dict(gcode_args, expected_sha256="a" * 64), expected_error="artifact_mismatch")
        printer_profiles = {}
        for role in ["machine", "process", "filament"]:
            profile_path = workspace / ("sdk-printer-" + role + ".json")
            profile_bytes = json.dumps({"type": role, "name": "Explicit SDK " + role, "post_process": []}).encode()
            profile_path.write_bytes(profile_bytes)
            printer_profiles[role] = {"path": str(profile_path), "expected_sha256": hashlib.sha256(profile_bytes).hexdigest()}
        printer_args = {"document_id": aid, "revision": 1, "action": "plan", "path": str(gcode_path),
                        "expected_sha256": hashlib.sha256(gcode_bytes).hexdigest(), "options": {
                            "printer": {"backend": "manual", "id": "sdk_printer", "model": "Assessed fixture",
                                        "nozzle_diameter_mm": .4, "bed_type": "Explicit plate", "handoff": "plain_gcode"},
                            "profiles": printer_profiles, "review": gcode_args["options"]}}
        printer_job = await call(client, definitions, "cad_job", {"action": "submit", "request_id": "sdk_printer", "tool": "cad_printer_handoff", "arguments": printer_args})
        printer_done = await poll(client, definitions, printer_job["job_id"])
        require(printer_done["state"] == "succeeded", "SDK offline printer planning failed")
        printer = printer_done["result"]
        Draft202012Validator(definitions["cad_printer_handoff"].output_schema).validate(printer)
        require(printer["readiness"]["status"] == "unknown" and not printer["hardware_contact"] and not printer["physical_print_started"], "SDK invented printer readiness or physical side effects")
        require(printer["source_association"] == "caller_declared_not_geometry_verified", "SDK hid the declared toolpath association")
        verified_printer = await call(client, definitions, "cad_printer_handoff", {"document_id": aid, "revision": 1, "action": "verify",
                       "plan_path": printer["path"], "expected_sha256": printer["sha256"]})
        require(verified_printer["readiness"] == printer["readiness"], "SDK verification changed unsupported firmware findings")
        unsupported_start = dict(printer_args, action="start")
        require(not Draft202012Validator(definitions["cad_printer_handoff"].input_schema).is_valid(unsupported_start),
                "Printer schema admitted an unsupported physical start")
        rejected_start = await client.call_tool("cad_printer_handoff", unsupported_start)
        require(rejected_start.is_error and rejected_start.structured_content["error"]["code"] == "invalid_argument",
                "Native MCP printer validation admitted an unsupported physical start")
        require(json.loads(rejected_start.content[0].text) == rejected_start.structured_content,
                "Unsupported printer start lost its compatible structured error")
        await call(client, definitions, "cad_printer_handoff", dict(printer_args, expected_sha256="a" * 64), expected_error="artifact_mismatch")
        if len(sys.argv) > 2:
            await call(client, definitions, "cad_create", {"document_id": "sdk_slice_part", "model": box_model()})
            planned = await call(client, definitions, "cad_slice", {"document_id": "sdk_slice_part", "revision": 1, "action": "plan",
                "options": slice_options(workspace / "native profiles", sys.argv[2], gcode_args["options"])})
            require(hashlib.sha256(Path(planned["path"]).read_bytes()).hexdigest() == planned["sha256"], "SDK dry-run bytes differ from reviewed hash")
            running = {"document_id": "sdk_slice_part", "revision": 1, "action": "run", "plan_path": planned["path"], "expected_sha256": planned["sha256"]}
            slice_job = await call(client, definitions, "cad_job", {"action": "submit", "request_id": "sdk_slice", "tool": "cad_slice", "arguments": running})
            sliced = await poll(client, definitions, slice_job["job_id"])
            require(sliced["state"] == "succeeded", "SDK native slicing process fixture failed")
            verify_package(sliced["result"])
            Draft202012Validator(definitions["cad_slice"].output_schema).validate(sliced["result"])
            await call(client, definitions, "cad_slice", dict(running, expected_sha256="a" * 64), expected_error="artifact_mismatch")
        robot = await call(client, definitions, "cad_robot_export", {
            "document_id": aid, "revision": 1, "robot": {"format": "urdf", "joint_properties": []}})
        require(Path(robot["path"]).is_file() and Path(robot["directory"], "model.srdf").is_file(),
                "SDK robot export omitted paired URDF/SRDF")
        bom_job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_assembly_bom", "tool": "cad_bom",
            "arguments": {"document_id": aid, "revision": 1}})
        bom_done = await poll(client, definitions, bom_job["job_id"])
        require(bom_done["state"] == "succeeded", f"SDK asynchronous BOM export failed: {bom_done}")
        bom = bom_done["result"]
        require(bom["bom"]["total_quantity"] == 4 and
                [(i["input"], i["quantity"], i["item_number"]) for i in bom["bom"]["items"]] ==
                [("plate", 2, 1), ("spacer", 2, 2)], "SDK BOM did not group source instances deterministically")
        require(json.loads(Path(bom["path"]).read_text()) == bom, "SDK BOM manifest does not match its result")
        for artifact in bom["artifacts"]:
            data = Path(artifact["path"]).read_bytes()
            require(len(data) == artifact["bytes"] > 0, "SDK BOM artifact size differs from its manifest")
            if artifact["format"] == "json":
                saved = json.loads(data)
                require(saved["bom"] == bom["bom"] and saved["revision"] == 1,
                        "SDK BOM JSON lost revision-qualified inventory")
            else:
                rows = list(csv.DictReader(io.StringIO(data.decode("ascii"))))
                require([(r["input"], r["quantity"], r["part_ids"]) for r in rows] ==
                        [("plate", "2", "base;cover"), ("spacer", "2", "spacer_a;spacer_b")],
                        "SDK BOM CSV did not round trip through the independent CSV parser")
        metadata = {"input": "plate", "item_number": 9, "part_number": "P-01",
                    "description": 'Plate, "checked"', "material": "Aluminum",
                    "purchase": {"supplier": "Example supplier", "part_number": "V-P01",
                                 "source_url": "https://example.invalid/V-P01", "artifact_sha256": "a" * 64}}
        metadata_edit = await call(client, definitions, "cad_apply", {
            "document_id": aid, "expected_revision": 1, "operations": [
                {"op": "set_bom_item", "assembly_id": "assembly", "item": metadata}]})
        require(metadata_edit["revision"] == 2 and math.isclose(metadata_edit["summary"]["volume_mm3"], assembly["summary"]["volume_mm3"], abs_tol=1e-6),
                "SDK BOM metadata edit changed geometry or failed to commit")
        metadata_source = await call(client, definitions, "cad_read", {"document_id": aid, "revision": metadata_edit["revision"]})
        package_job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_manufacturing", "tool": "cad_manufacture",
            "arguments": {"document_id": aid, "revision": 2, "options": {
                "part_drawing": {"views": [{"id": "front", "orientation": "front"}],
                                 "dimensions": [{"view": "front", "kind": "width"}]},
                "parts": [{"feature_id": "plate", "process": "cnc", "material": "Aluminum"}]}}})
        package_done = await poll(client, definitions, package_job["job_id"])
        require(package_done["state"] == "succeeded", f"SDK manufacturing job failed: {package_done}")
        package = package_done["result"]
        manifest = json.loads(Path(package["path"]).read_text())
        require(package["part_count"] == 2 and len(manifest["occurrences"]) == 4,
                "SDK manufacturing lost unique source roll-up or physical occurrences")
        require(manifest["process_review"]["status"] == "not_evaluated", "SDK package falsely claimed process validation")
        require(next(p for p in manifest["parts"] if p["feature_id"] == "plate")["purchase"] == metadata["purchase"],
                "SDK manufacturing lost purchasing identity")
        for artifact in manifest["artifacts"]:
            require(not Path(artifact["path"]).is_absolute(), "SDK portable manifest contains an absolute artifact path")
            content = (Path(package["directory"]) / artifact["path"]).read_bytes()
            require(len(content) == artifact["bytes"] and hashlib.sha256(content).hexdigest() == artifact["sha256"],
                    "SDK manufacturing artifact failed independent SHA-256 verification")
        require(json.loads((Path(package["directory"]) / "source.json").read_text())["model"] == metadata_source["model"],
                "SDK package lost editable source")
        process_profile = {"process": "cnc", "orientation": {"build_direction": [0, 0, 1], "x_direction": [1, 0, 0]},
                           "tool_radius_mm": 1}
        review_job = await call(client, definitions, "cad_job", {"action": "submit", "request_id": "sdk_fabrication",
            "tool": "cad_fabrication_review", "arguments": {"document_id": aid, "revision": 2,
                "options": {"profile": process_profile, "minimum_clearance_mm": 0}}})
        reviewed_job = await poll(client, definitions, review_job["job_id"])
        require(reviewed_job["state"] == "succeeded", f"SDK measured review failed: {reviewed_job}")
        reviewed = reviewed_job["result"]
        review_bytes = Path(reviewed["path"]).read_bytes()
        saved_review = json.loads(review_bytes)
        require(len(review_bytes) == reviewed["bytes"] and hashlib.sha256(review_bytes).hexdigest() == reviewed["sha256"],
                "SDK review artifact failed independent hash/size verification")
        require(saved_review["source"]["revision"] == 2 and saved_review["source"]["native_build"] == reviewed["native_build"],
                "SDK review lost revision or native build provenance")
        require(len(reviewed["report"]["parts"]) == 2 and reviewed["report"]["coordinate_policy"] == "source_features_and_saved_occurrences",
                "SDK review lost source roll-up or coordinate policy")
        for part in reviewed["report"]["parts"]:
            findings = {c["id"]: c for c in part["checks"]}
            require(findings["global_minimum_wall"]["status"] == "unknown" and findings["cnc_toolpath_and_stock"]["status"] == "unknown",
                    "SDK review turned unsupported process checks into passes")
        require((await call(client, definitions, "cad_read", {"document_id": aid}))["model"] == metadata_source["model"],
                "SDK measured review changed saved source intent")
        supplier_step = await call(client, definitions, "cad_export", {"document_id": aid, "revision": 2, "format": "step"})
        supplier_bytes = Path(supplier_step["path"]).read_bytes()
        supplier_hash = hashlib.sha256(supplier_bytes).hexdigest()
        supplier = {"supplier": "SDK fixture supplier", "part_number": "ASSEMBLY-1", "source_url": "https://example.invalid/ASSEMBLY-1"}
        supplier_job = await call(client, definitions, "cad_job", {"action": "submit", "request_id": "sdk_purchased_import",
            "tool": "cad_import", "arguments": {"document_id": "sdk_purchased", "path": supplier_step["path"],
                "purchase": supplier, "expected_sha256": supplier_hash}})
        supplier_done = await poll(client, definitions, supplier_job["job_id"])
        require(supplier_done["state"] == "succeeded", "SDK purchased import job failed")
        bound_supplier = dict(supplier, artifact_sha256=supplier_hash)
        supplier_source = await call(client, definitions, "cad_read", {"document_id": "sdk_purchased", "revision": supplier_done["result"]["revision"]})
        require(supplier_source["model"]["features"][0]["purchase"] == bound_supplier,
                "SDK importer did not bind the measured raw artifact hash")
        await call(client, definitions, "cad_import", {"document_id": "sdk_purchase_mismatch", "path": supplier_step["path"],
            "purchase": supplier, "expected_sha256": "a" * 64}, expected_error="artifact_mismatch")
        bought_package = await call(client, definitions, "cad_manufacture", {"document_id": "sdk_purchased", "revision": 1,
            "options": {"drawings": False}})
        bought_manifest = json.loads(Path(bought_package["path"]).read_text())
        bought_part = bought_manifest["parts"][0]
        require(bought_part["purchase"] == bound_supplier and bought_part["source_artifact"]["sha256"] == supplier_hash,
                "SDK package lost the supplier/source identity link")
        require((Path(bought_package["directory"]) / bought_part["source_artifact"]["path"]).read_bytes() == supplier_bytes,
                "SDK manufacturing re-encoded the original supplier STEP artifact")
        balloon_recipe = json.loads((example_dir / "assembly-bom.drawing.json").read_text())
        balloon_recipe["revision"] = 2
        balloon_recipe["drawing"]["notes"] = []
        balloon_drawing = await call(client, definitions, "cad_drawing", balloon_recipe)
        require({b["part_id"]: b["item_number"] for b in balloon_drawing["balloons"]} ==
                {"base": 9, "cover": 9, "spacer_a": 1, "spacer_b": 1},
                "SDK balloons did not use the edited shared BOM item numbers")
        files = {a["format"]: Path(a["path"]).read_bytes() for a in balloon_drawing["artifacts"]}
        require(set(files) == {"svg", "pdf", "dxf", "json", "csv"}, "BOM drawing omitted sidecars or requested formats")
        require(json.loads(files["json"])["bom"] == balloon_drawing["bom"], "Drawing BOM JSON differs from response")
        require(list(csv.DictReader(io.StringIO(files["csv"].decode("ascii"))))[-1]["description"] == metadata["description"],
                "Quoted drawing BOM metadata did not survive independent CSV parsing")
        require(b"BILL OF MATERIALS" in files["pdf"] and b"BALLOONS" in files["dxf"],
                "SDK drawing files omitted rendered tables or balloons")
        require((await call(client, definitions, "cad_bom", {"document_id": aid, "revision": 1}))["bom"] == bom["bom"],
                "Historical BOM changed after metadata edit")
        await call(client, definitions, "cad_apply", {"document_id": aid, "expected_revision": 2, "operations": [
            {"op": "remove_bom_item", "assembly_id": "assembly", "input": "plate"}]})
        require((await call(client, definitions, "cad_bom", {"document_id": aid, "revision": 3}))["bom"] == bom["bom"],
                "BOM metadata removal did not restore automatic rows")
        await call(client, definitions, "cad_bom", {"document_id": aid, "revision": 3, "feature_id": "plate"},
                   expected_error="invalid_argument")
        await call(client, definitions, "cad_open", {"document_id": aid, "view_id": "sdk_visibility"})
        for _ in range(500):
            display = await call(client, definitions, "cad_viewer", {"action": "sync", "view_id": "sdk_visibility"})
            if display["state"] != "loading":
                break
            await asyncio.sleep(.01)
        require(display["state"] == "ready" and display["hidden_part_ids"] == [], "SDK assembly visibility does not default to show all")
        measurement_args={"document_id":aid,"revision":3,"evaluation_id":display["evaluation_id"],"feature_id":display["feature_id"],
            "query":{"action":"pair","targets":[{"kind":"part","part_id":"spacer_a"},{"kind":"part","part_id":"spacer_b"}],"minimum_clearance_mm":1}}
        measured=await call(client,definitions,"cad_measure",measurement_args)
        require(math.isclose(measured["report"]["minimum_distance_mm"],math.hypot(40,10)-10,abs_tol=1e-6),"SDK exact spacer gap differs from independent analytic cylinder distance")
        require(measured["report"]["status"]=="pass" and measured["report"]["pairs"][0]["interference"] is False,"SDK separated parts report material overlap or failed explicit threshold")
        measurement_job=await call(client,definitions,"cad_job",{"action":"submit","request_id":"sdk_measure","tool":"cad_measure","arguments":measurement_args})
        measured_job=await poll(client,definitions,measurement_job["job_id"])
        require(measured_job["state"]=="succeeded" and math.isclose(measured_job["result"]["report"]["minimum_distance_mm"],math.hypot(40,10)-10,abs_tol=1e-6),"SDK durable measurement does not retain actual source distance")
        viewer_measure={"action":"measure","view_id":"sdk_visibility","evaluation_id":display["evaluation_id"],"query":measurement_args["query"]}
        live_measure=await call(client,definitions,"cad_viewer",viewer_measure)
        for _ in range(500):
            live_measure=await call(client,definitions,"cad_viewer",{k:v for k,v in viewer_measure.items() if k!="query"})
            if live_measure["state"] not in {"queued","running","cancelling"}:break
            await asyncio.sleep(.01)
        require(live_measure["state"]=="succeeded" and live_measure["result"]["report"]["status"]=="pass","SDK viewer did not return a native asynchronous measurement")
        require((await call(client,definitions,"cad_context",{"view_id":"sdk_visibility"}))["measurement"]["job_id"]==live_measure["job_id"],"SDK context lost its qualified measurement job")
        review_appearance={"default_color":[.2,.3,.4],"parts":[{"part_id":"spacer_a","color":[1,0,0]},{"part_id":"spacer_b","color":[0,1,0]}]}
        review_camera={"yaw":.2,"pitch":.4,"zoom":2,"pan":[.1,.2]}
        appearance_context={"action":"context","view_id":"sdk_visibility","evaluation_id":display["evaluation_id"],"selection":None,"appearance":review_appearance,"camera":review_camera}
        colored=await call(client,definitions,"cad_viewer",appearance_context)
        require(colored["appearance"]==review_appearance and colored["measurement"]["job_id"]==live_measure["job_id"],"SDK appearance lost source qualification or independent exact measurement")
        preset_action={"action":"preset","view_id":"sdk_visibility","evaluation_id":display["evaluation_id"]}
        saved=await call(client,definitions,"cad_viewer",dict(preset_action,operation="save",name="Assembly review"))
        require(saved["presets"][0]["appearance"]==review_appearance and saved["presets"][0]["camera"]==review_camera,"SDK preset did not capture complete current review settings")
        await call(client,definitions,"cad_viewer",dict(appearance_context,appearance={"default_color":[.4,.4,.4],"parts":[]}))
        restored=await call(client,definitions,"cad_viewer",dict(preset_action,operation="apply",name="Assembly review"))
        require(restored["appearance"]==review_appearance and restored["camera"]==review_camera and restored["selection"] is None,"SDK preset apply failed to restore native review settings atomically")
        synced=await call(client,definitions,"cad_viewer",{"action":"sync","view_id":"sdk_visibility","known_evaluation_id":display["evaluation_id"]})
        require(synced["appearance"]==review_appearance and synced["camera"]==review_camera and len(synced["presets"])==1,"SDK ready sync cannot reconcile a lost preset acknowledgement")
        require(len((await call(client,definitions,"cad_viewer",dict(preset_action,operation="list")))["presets"])==1,"SDK preset list changed saved review settings")
        require((await call(client,definitions,"cad_viewer",dict(preset_action,operation="delete",name="Assembly review")))["presets"]==[],"SDK preset delete did not remove the named entry")

        await call(client,definitions,"cad_viewer",dict(viewer_measure,query=None))
        default_presentation = {"clip": None, "explode": {"distance_mm": 0, "directions": []}}
        require(display["presentation"] == default_presentation, "SDK live view lacks default presentation")
        presentation = {"clip": {"normal": [0, 0, 1], "offset_mm": 4, "keep": "negative"},
                        "explode": {"distance_mm": 20, "directions": [{"part_id": "cover", "direction": [0, 0, 1]}]}}
        mesh_parts, offset = [], 0
        while offset is not None:
            chunk = await call(client, definitions, "cad_viewer", {"action": "mesh", "view_id": "sdk_visibility",
                "evaluation_id": display["evaluation_id"], "offset": offset})
            mesh_parts.append(chunk["data"])
            offset = chunk["next_offset"]
        frozen = json.loads("".join(mesh_parts))
        hidden_face = next(face for face in frozen["topology"]["faces"] if face["part_id"] == "cover")
        hidden_pick = {"document_id": aid, "revision": 3, "evaluation_id": display["evaluation_id"],
                       "feature_id": display["feature_id"], "kind": "face", "entity_id": hidden_face["id"]}
        visibility_args = {"action": "context", "view_id": "sdk_visibility", "evaluation_id": display["evaluation_id"],
                           "selection": None, "hidden_part_ids": ["cover", "spacer_b"], "presentation": presentation}
        hidden_context = await call(client, definitions, "cad_viewer", visibility_args)
        require(hidden_context["hidden_part_ids"] == ["cover", "spacer_b"], "SDK context omitted hidden assembly IDs")
        hidden_sync = await call(client, definitions, "cad_viewer", {"action": "sync", "view_id": "sdk_visibility",
            "known_evaluation_id": display["evaluation_id"]})
        require(hidden_sync["changed"] is False and hidden_sync["hidden_part_ids"] == ["cover", "spacer_b"],
                "SDK same-evaluation sync lost presentation state")
        require(hidden_context["presentation"] == hidden_sync["presentation"] == presentation,
                "SDK saved presentation does not match same-evaluation sync")
        omitted_presentation = {key: value for key, value in visibility_args.items() if key != "presentation"}
        hidden_context = await call(client, definitions, "cad_viewer", omitted_presentation)
        require(hidden_context["presentation"] == presentation,
                "SDK omitted presentation reset the saved view")
        for invalid_presentation in [dict(presentation, clip=dict(presentation["clip"], normal=[0, 0, 0])),
                dict(presentation, explode={"distance_mm": 1, "directions": [{"part_id": "missing", "direction": [1, 0, 0]}]})]:
            await call(client, definitions, "cad_viewer", dict(visibility_args, presentation=invalid_presentation), expected_error="invalid_argument")
        require((await call(client, definitions, "cad_context", {"view_id": "sdk_visibility"})) == hidden_context,
                "SDK invalid presentation changed saved context")
        await call(client, definitions, "cad_viewer", {**visibility_args, "selection": hidden_pick}, expected_error="invalid_argument")
        require((await call(client, definitions, "cad_context", {"view_id": "sdk_visibility"})) == hidden_context,
                "SDK hidden-part selection changed saved presentation state")
        omitted_visibility = {key: value for key, value in visibility_args.items() if key != "hidden_part_ids"}
        require((await call(client, definitions, "cad_viewer", omitted_visibility))["hidden_part_ids"] == ["cover", "spacer_b"],
                "SDK context update without visibility cleared hidden parts")
        require((await call(client, definitions, "cad_viewer", {**visibility_args, "hidden_part_ids": [], "selection": hidden_pick}))["hidden_part_ids"] == [],
                "SDK show-all did not permit selecting the revealed part")
        await call(client, definitions, "cad_viewer", visibility_args)
        for invalid_hidden in ["cover", ["cover", "cover"], [1], ["cover"] * 65]:
            require(not Draft202012Validator(definitions["cad_viewer"].input_schema).is_valid(
                {**visibility_args, "hidden_part_ids": invalid_hidden}), "SDK schema accepted malformed hidden-part IDs")
        for bad in [{"op": "set_bom_item", "assembly_id": "assembly", "item": {"input": "plate", "item_number": 1000}},
                    {"op": "set_bom_item", "assembly_id": "assembly", "item": {"input": "plate", "description": "line\n"}}]:
            require(not Draft202012Validator(definitions["cad_apply"].input_schema).is_valid(
                {"document_id": aid, "expected_revision": 3, "operations": [bad]}),
                "SDK advertised schema accepts invalid BOM metadata")
        # Independent typed SDK calls exercise source-scoped native note metadata.
        await call(client,definitions,"cad_create",{"document_id":"sdk_annotations","model":box_model()})
        await call(client,definitions,"cad_open",{"document_id":"sdk_annotations","view_id":"sdk_annotations"})
        deadline=asyncio.get_running_loop().time()+15
        while True:
            annotation_view=await call(client,definitions,"cad_viewer",{"action":"sync","view_id":"sdk_annotations"})
            if annotation_view["state"]=="ready":break
            require(annotation_view["state"]=="loading" and asyncio.get_running_loop().time()<deadline,"SDK annotation source did not load")
            await asyncio.sleep(.02)
        annotation_args={"action":"annotation","view_id":"sdk_annotations","evaluation_id":annotation_view["evaluation_id"]}
        annotation_result=await call(client,definitions,"cad_viewer",dict(annotation_args,operation="add",anchor={"kind":"model"},text="Inspect native model center <plain text>"))
        annotation_note=annotation_result["annotations"][0]
        require(annotation_note["status"]=="current" and annotation_note["anchor_lifetime"]=="evaluation","SDK annotation lost evaluation lifetime")
        require(annotation_note["anchor"]["position_semantics"]=="bounds_center" and annotation_note["anchor"]["part_id"] is None,"SDK overview invents entity ownership")
        annotation_topology=await call(client,definitions,"cad_query",{"document_id":"sdk_annotations","revision":1,"kind":"topology"})
        annotation_face=annotation_topology["topology"]["faces"][0]
        annotation_ref={"document_id":"sdk_annotations","revision":1,"evaluation_id":annotation_view["evaluation_id"],"feature_id":"base","kind":"face","entity_id":annotation_face["id"]}
        annotation_result=await call(client,definitions,"cad_viewer",dict(annotation_args,operation="add",anchor={"kind":"entity","reference":annotation_ref},text="Native resolved face inspection"))
        require(annotation_result["annotations"][1]["anchor"]["point_mm"]==annotation_face["center_mm"],"SDK entity center differs from native evidence")
        await call(client,definitions,"cad_viewer",dict(annotation_args,operation="add",anchor={"kind":"part","part_id":"missing"},text="bad"),expected_error="invalid_argument")
        await call(client,definitions,"cad_viewer",dict(annotation_args,operation="add",anchor={"kind":"model"},text="é"*257),expected_error="invalid_argument")
        updated=await call(client,definitions,"cad_viewer",dict(annotation_args,operation="update",annotation_id=annotation_note["id"],text="Edited SDK review"))
        require(updated["annotations"][0]["anchor"]==annotation_note["anchor"],"SDK text update changed annotation geometry")
        require((await call(client,definitions,"cad_viewer",dict(annotation_args,operation="list")))["annotations"]==updated["annotations"],"SDK note list disagrees with saved context")
        await call(client,definitions,"cad_apply",{"document_id":"sdk_annotations","expected_revision":1,"operations":[{"op":"set_parameter","name":"height","value":7}]})
        annotation_history=(await call(client,definitions,"cad_context",{"view_id":"sdk_annotations"}))["annotations"]
        require(all(note["status"]=="retired" and note["evaluation_id"]==annotation_view["evaluation_id"] for note in annotation_history),"SDK source change rebound inspection anchors")
        arguments = {"document_id": "part", "model": box_model(), "request_id": "sdk_create"}
        created = await call(client, definitions, "cad_create", arguments)
        require(created["revision"] == 1, "Initial native revision is not one")
        require(math.isclose(created["summary"]["volume_mm3"], 1200, abs_tol=1e-6), "Incorrect initial volume")
        require(await call(client, definitions, "cad_create", arguments) == created,
                "Request ID did not replay the original result through the SDK")
        read = await call(client, definitions, "cad_read", {"document_id": "part"})
        require(read["model"] == box_model(), "Editable source did not survive the MCP round trip")
        edited = await call(client, definitions, "cad_apply", {
            "document_id": "part", "expected_revision": 1, "request_id": "sdk_edit",
            "operations": [{"op": "set_parameter", "name": "height", "value": 8}],
        })
        require(edited["revision"] == 2 and math.isclose(edited["summary"]["volume_mm3"], 1600, abs_tol=1e-6),
                "Semantic edit did not rebuild and commit revision two")
        for file_format in ("step", "stl"):
            artifact = await call(client, definitions, "cad_export", {
                "document_id": "part", "revision": 2, "format": file_format})
            path = Path(artifact["path"])
            require(path.is_file() and path.stat().st_size == artifact["bytes"] > 0,
                    f"SDK export did not produce an independent {file_format} file")
        ring_model={"schema_version":1,"units":"mm","parameters":{"radius":5},"features":[
            {"id":"outer","type":"cylinder","radius":{"parameter":"radius"},"height":10},
            {"id":"inner","type":"cylinder","radius":2,"height":10},
            {"id":"ring","type":"cut","left":"outer","right":"inner"}],"output":"ring"}
        await call(client,definitions,"cad_create",{"document_id":"sdk_section","model":ring_model})
        section_eval=await call(client,definitions,"cad_query",{"document_id":"sdk_section","revision":1,"kind":"topology"})
        section_query={"action":"section","plane":{"normal":[0,0,1],"offset_mm":5}}
        section_args={"document_id":"sdk_section","revision":1,"evaluation_id":section_eval["evaluation_id"],"feature_id":"ring","query":section_query}
        native_section=await call(client,definitions,"cad_measure",section_args)
        require(math.isclose(native_section["report"]["area_mm2"],21*math.pi,abs_tol=1e-6) and native_section["report"]["regions"][0]["wire_count"]==2,"SDK native section lost exact annular area or its interior wire")
        require(len(native_section["report"]["curves"]) == 2,
                "SDK annular section must expose both circular boundaries")
        for curve in native_section["report"]["curves"]:
            require(curve["closed"] is True and curve["radius_mm"] in (2, 5),
                    "SDK annular section lost exact circle closure or radius")
            require(len(curve["endpoints_mm"]) == 2 and all(
                math.isclose(p[2], 5, abs_tol=1e-7) and
                math.isclose(math.hypot(p[0], p[1]), curve["radius_mm"], abs_tol=1e-7)
                for p in curve["endpoints_mm"]), "SDK section endpoints left their analytic circle")
        section_validator = Draft202012Validator(definitions["cad_measure"].output_schema)
        for field, value in [("closed", "yes"), ("endpoints_mm", [[5, 0, 5]]), ("unchecked_extra", True)]:
            malformed = json.loads(json.dumps(native_section))
            malformed["report"]["curves"][0][field] = value
            require(not section_validator.is_valid(malformed),
                    f"Section schema accepted malformed {field}")
        section_job=await call(client,definitions,"cad_job",{"action":"submit","request_id":"sdk_section_job","tool":"cad_measure","arguments":section_args})
        section_completed=await poll(client,definitions,section_job["job_id"])
        require(section_completed["state"]=="succeeded" and section_completed["result"]["report"]["mesh"]["triangles"],"SDK asynchronous section did not return actual native cap triangles")
        await call(client,definitions,"cad_open",{"document_id":"sdk_section","view_id":"sdk_section"})
        async def section_ready():
            for _ in range(500):
                result=await call(client,definitions,"cad_viewer",{"action":"sync","view_id":"sdk_section"})
                if result["state"]=="ready":return result
                require(result["state"]=="loading","SDK section view failed before becoming ready");await asyncio.sleep(.01)
            raise AssertionError("SDK section view did not load")
        section_view=await section_ready()
        section_presentation={"clip":{"normal":[0,0,1],"offset_mm":5,"keep":"negative"},"explode":{"distance_mm":0,"directions":[]}}
        section_context={"action":"context","view_id":"sdk_section","evaluation_id":section_view["evaluation_id"],"selection":None,"presentation":section_presentation}
        await call(client,definitions,"cad_viewer",section_context)
        section_action={"action":"section","view_id":"sdk_section","evaluation_id":section_view["evaluation_id"]}
        live_section=await call(client,definitions,"cad_viewer",dict(section_action,query=section_query))
        for _ in range(500):
            live_section=await call(client,definitions,"cad_viewer",section_action)
            if live_section["state"] not in ["queued","running"]:break
            await asyncio.sleep(.01)
        require(live_section["state"]=="succeeded" and math.isclose(live_section["result"]["report"]["area_mm2"],21*math.pi,abs_tol=1e-6),"SDK live section did not qualify its actual material cap")
        require((await call(client,definitions,"cad_context",{"view_id":"sdk_section"}))["section"]["job_id"]==live_section["job_id"],"SDK context lost the current native section job")
        section_presentation["clip"]["keep"]="positive"
        require((await call(client,definitions,"cad_viewer",section_context))["section"]["job_id"]==live_section["job_id"],"SDK kept-side reversal incorrectly retired unchanged cap geometry")
        await call(client,definitions,"cad_viewer",dict(section_action,query={"action":"section","plane":{"normal":[0,0,1],"offset_mm":4}}),expected_error="stale_selection")
        require((await call(client,definitions,"cad_viewer",dict(section_action,query=None)))["state"]=="empty","SDK clear did not retire the native cap reference")
        await call(client,definitions,"cad_apply",{"document_id":"sdk_section","expected_revision":1,"operations":[{"op":"set_parameter","name":"radius","value":6}]})
        await call(client,definitions,"cad_measure",section_args,expected_error="stale_selection")
        refreshed_section=await section_ready()
        old_section_context=await call(client,definitions,"cad_context",{"view_id":"sdk_section"})
        require(old_section_context["stale"] and old_section_context["evaluation_id"]!=refreshed_section["evaluation_id"],"SDK fixture did not retain ordinary stale saved context after source refresh")
        refreshed_preset={"action":"preset","view_id":"sdk_section","evaluation_id":refreshed_section["evaluation_id"]}
        for operation in ["list","save","delete"]:
            args=dict(refreshed_preset,operation=operation)
            if operation!="list":args["name"]="Current source"
            action=await call(client,definitions,"cad_viewer",args)
            require(action["document_id"]=="sdk_section" and action["revision"]==2 and action["evaluation_id"]==refreshed_section["evaluation_id"] and action["feature_id"]=="ring" and not action["stale"],"SDK preset response adopted stale saved source headers")
            require(action["selection"] is None and "camera" not in action,"SDK preset response restored an old source camera or pick")
        require((await call(client,definitions,"cad_context",{"view_id":"sdk_section"}))["evaluation_id"]==old_section_context["evaluation_id"],"SDK preset response qualification rewrote ordinary saved stale context")

        playback_fixture = json.loads((example_dir / "articulated-arm.create.json").read_text())
        playback_fixture["document_id"] = "sdk_playback"
        playback_original = await call(client, definitions, "cad_create", playback_fixture)
        await call(client, definitions, "cad_open", {"document_id": "sdk_playback", "view_id": "sdk_playback"})
        for _ in range(500):
            timeline = await call(client, definitions, "cad_viewer", {"action": "sync", "view_id": "sdk_playback"})
            if timeline["state"] == "ready": break
            await asyncio.sleep(.01)
        require(timeline["state"] == "ready", "SDK playback source did not become ready")
        def playback_frame(time_s, angle, travel, distance):
            return {"time_s": time_s, "presentation": {"clip": None, "explode": {"distance_mm": distance, "directions": []}},
                    "joints": [{"assembly_id": "mechanism", "values": [{"mate_id": "hinge", "coordinate": "angle_deg", "value": angle}, {"mate_id": "spindle_joint", "coordinate": "travel_mm", "value": travel}]}]}
        sequence = {"name": "SDK coordinated playback", "frames": [playback_frame(0, 0, 0, 0), playback_frame(2, 90, 18, 10)]}
        sequence_base = {"action": "sequence", "view_id": "sdk_playback", "evaluation_id": timeline["evaluation_id"]}
        saved = await call(client, definitions, "cad_viewer", dict(sequence_base, operation="save", sequence=sequence))
        require(saved["sequences"][0]["source"]["evaluation_id"] == timeline["evaluation_id"], "SDK saved timeline lost original source identity")
        options = await call(client, definitions, "cad_viewer", dict(sequence_base, operation="options", name=sequence["name"], speed=1.5, loop=True))
        require(options["playback"]["state"] == "unapplied", "SDK options falsely claimed a displayed sample")
        invalid = json.loads(json.dumps(sequence)); invalid["frames"][0]["time_s"] = .5
        await call(client, definitions, "cad_viewer", dict(sequence_base, operation="save", sequence=invalid), expected_error="invalid_argument")
        await call(client, definitions, "cad_viewer", dict(sequence_base, operation="seek", name=sequence["name"], time_s=1))
        for _ in range(500):
            timeline = await call(client, definitions, "cad_viewer", {"action": "sync", "view_id": "sdk_playback"})
            if timeline["state"] == "ready": break
            await asyncio.sleep(.01)
        require(timeline["state"] == "ready" and timeline["draft"] and timeline["playback"]["state"] == "displayed" and timeline["playback"]["time_s"] == 1, "SDK sync lost the displayed native time and draft identity")
        dofs = timeline["summary"]["assembly"]["motion"]["dofs"]
        require(next(v["value"] for v in dofs if v["mate_id"] == "hinge") == 45 and next(v["value"] for v in dofs if v["mate_id"] == "rail") == 4.5, "SDK playback failed joint interpolation or coupling")
        require(timeline["presentation"]["explode"]["distance_mm"] == 5, "SDK exploded interpolation failed")
        current_source = await call(client, definitions, "cad_read", {"document_id": "sdk_playback"})
        require(current_source["revision"] == 1 and current_source["model"] == playback_fixture["model"], "SDK playback mutated editable source")
        sequence_base["evaluation_id"] = timeline["evaluation_id"]
        listed = await call(client, definitions, "cad_viewer", dict(sequence_base, operation="list"))
        require(listed["evaluation_id"] == timeline["evaluation_id"] and not listed["stale"], "SDK timeline list reused old committed display headers")
        require((await call(client, definitions, "cad_context", {"view_id": "sdk_playback"}))["playback"]["time_s"] == 1, "SDK paused agent context omitted playback time")
        require((await call(client, definitions, "cad_viewer", dict(sequence_base, operation="delete", name=sequence["name"])))["sequences"] == [], "SDK timeline delete failed")

        drawing_recipe = {
            "title": "SDK plate", "sheet": "A4", "formats": ["svg", "pdf", "dxf"],
            "views": [{"id": "front", "orientation": "front"}],
            "dimensions": [{"view": "front", "kind": "width"}, {"view": "front", "kind": "height"}],
        }
        drawing = await call(client, definitions, "cad_drawing", {
            "document_id": "part", "revision": 2, "drawing": drawing_recipe})
        require(drawing["units"] == "mm" and drawing["revision"] == 2,
                "Drawing lost committed revision or document units")
        require({item["kind"]: item["value_mm"] for item in drawing["dimensions"]} == {"width": 20, "height": 8},
                "SDK drawing dimensions do not match edited geometry")
        require(json.loads(Path(drawing["recipe_path"]).read_text())["drawing"] == drawing_recipe,
                "Drawing sidecar did not retain the replayable recipe")
        formats = {item["format"]: Path(item["path"]).read_bytes() for item in drawing["artifacts"]}
        require(set(formats) == {"svg", "pdf", "dxf"}, "SDK drawing did not return all requested vector formats")
        require(formats["pdf"].startswith(b"%PDF-") and b"<svg" in formats["svg"] and b"ENTITIES" in formats["dxf"],
                "Native drawing files do not contain the declared formats")
        angular = await call(client, definitions, "cad_drawing", {
            "document_id": "part", "revision": 2, "drawing": {
                "views": [{"id": "front", "orientation": "front"}],
                "general_tolerances": {"linear": .1},
                "dimensions": [{"view": "front", "kind": "angular", "arc_radius": 4,
                    "lines": [{"from": [0, 0], "to": [20, 0]}, {"from": [0, 0], "to": [0, 8]}],
                    "manufacturing_tolerance": {"type": "symmetric", "value": .25}},
                    {"view": "front", "kind": "width"}]}})
        require(angular["dimensions"][0]["value_deg"] == 90 and "value_mm" not in angular["dimensions"][0],
                "SDK angular dimension lost degree units")
        require(angular["dimensions"][0]["lower_limit_deg"] == 89.75 and
                angular["dimensions"][1]["lower_limit_mm"] == 19.9,
                "SDK drawing did not preserve explicit and inherited manufacturing limits")
        require(angular["dimensions"][0]["label"] == "90 +/-0.25 deg",
                "SDK drawing did not report its printed tolerance label")
        await call(client, definitions, "cad_drawing", {"document_id": "part", "revision": 2, "drawing": {
            "views": [{"id": "front", "orientation": "front"}],
            "dimensions": [{"view": "front", "kind": "diameter", "center": [100, 100], "radius": 1}],
        }}, expected_error="drawing_reference_not_found")
        drawing_job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_drawing", "tool": "cad_drawing",
            "arguments": {"document_id": "part", "revision": 2, "drawing": {"formats": ["svg"]}},
        })
        drawing_done = await poll(client, definitions, drawing_job["job_id"])
        require(drawing_done["state"] == "succeeded" and drawing_done["result"]["revision"] == 2,
                "Asynchronous drawing failed through the independent SDK")
        await call(client, definitions, "cad_apply", {
            "document_id": "part", "expected_revision": 1,
            "operations": [{"op": "set_parameter", "name": "height", "value": 9}],
        }, expected_error="revision_conflict")
        await call(client, definitions, "cad_apply", {
            "document_id": "part", "expected_revision": 2,
            "operations": [{"op": "set_parameter", "name": "height", "value": -1}],
        }, expected_error="invalid_model")
        require((await call(client, definitions, "cad_read", {"document_id": "part"}))["revision"] == 2,
                "Failed SDK edits changed HEAD")
        job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_cancel", "tool": "cad_apply",
            "arguments": {"document_id": "part", "expected_revision": 2, "operations": heavy_edits()},
            "budget": {"timeout_ms": 10000, "memory_mb": 2048},
        })
        require(job["state"] == "queued", "SDK submission waited for geometry instead of returning a job")
        try:
            await wait_for_native_build(workspace)
            await asyncio.wait_for(client.session.send_ping(), timeout=5)
            require(True, "Independent SDK ping responded during native geometry work")
            live = await call(client, definitions, "cad_read", {"document_id": "part"}, timeout=5)
            require(live["revision"] == 2, "SDK read did not retain committed HEAD during worker activity")
        finally:
            await call(client, definitions, "cad_job", {"action": "cancel", "job_id": job["job_id"]})
            cancelled = await poll(client, definitions, job["job_id"])
        require(cancelled["state"] == "cancelled", f"SDK cancellation did not stop the worker: {cancelled}")
        query_job = await call(client, definitions, "cad_job", {
            "action": "submit", "request_id": "sdk_query", "tool": "cad_query",
            "arguments": {"document_id": "part", "revision": 2},
        })
        completed = await poll(client, definitions, query_job["job_id"])
        require(completed["state"] == "succeeded", f"SDK query job failed: {completed}")
        require(math.isclose(completed["result"]["summary"]["volume_mm3"], 1600, abs_tol=1e-6),
                "Completed SDK job reported incorrect geometry")
    # Fresh native subprocess and explicit legacy SDK mode establish that source,
    # immutable revisions, dedup receipts and job results survive session teardown.
    async with Client(params, mode="legacy", read_timeout_seconds=15) as reopened:
        definitions = await discover(reopened)
        require((await call(reopened,definitions,"cad_context",{"view_id":"sdk_annotations"}))["annotations"]==annotation_history,"SDK restart changed historical review note evidence")
        deadline=asyncio.get_running_loop().time()+15
        while True:
            annotations_reopened=await call(reopened,definitions,"cad_viewer",{"action":"sync","view_id":"sdk_annotations"})
            if annotations_reopened["state"]=="ready":break
            require(annotations_reopened["state"]=="loading" and asyncio.get_running_loop().time()<deadline,"SDK reopened annotations did not load")
            await asyncio.sleep(.02)
        annotation_current=dict(annotation_args,evaluation_id=annotations_reopened["evaluation_id"])
        deleted=await call(reopened,definitions,"cad_viewer",dict(annotation_current,operation="delete",annotation_id=annotation_note["id"]))
        require(len(deleted["annotations"])==1 and deleted["revision"]==2 and deleted["selection"] is None,"SDK historical note delete lost current response qualification")
        require(not (await call(reopened,definitions,"cad_viewer",dict(annotation_current,operation="clear")))["annotations"],"SDK clear left historical notes")
        current = await call(reopened, definitions, "cad_read", {"document_id": "part"})
        require(current["revision"] == 2, "Reopened SDK session lost committed HEAD")
        historical = await call(reopened, definitions, "cad_read", {"document_id": "part", "revision": 1})
        require(historical["model"] == box_model(), "Historical revision changed after restart")
        require(await call(reopened, definitions, "cad_create", {
            "document_id": "part", "model": box_model(), "request_id": "sdk_create"}) == created,
                "Durable mutation deduplication failed across SDK sessions")
        persisted = await call(reopened, definitions, "cad_job", {"action": "get", "job_id": "sdk_query"})
        require(persisted["state"] == "succeeded", "Job result was not durable across SDK sessions")
        historical_section=await call(reopened,definitions,"cad_job",{"action":"get","job_id":"sdk_section_job"})
        require(historical_section["result"]==section_completed["result"],"Restart/source edit changed a qualified historical section result")
        persisted_bom = await call(reopened, definitions, "cad_job", {"action": "get", "job_id": "sdk_assembly_bom"})
        require(persisted_bom["state"] == "succeeded" and persisted_bom["result"]["bom"] == bom["bom"],
                "Asynchronous BOM result changed across SDK sessions")
        require((await call(reopened, definitions, "cad_context", {"view_id": "sdk_visibility"}))["hidden_part_ids"] == ["cover", "spacer_b"],
                "Assembly visibility did not persist across SDK sessions")
        require((await call(reopened, definitions, "cad_context", {"view_id": "sdk_visibility"}))["presentation"] == presentation,
                "Clipping and explosion did not persist across SDK sessions")
        await call(reopened, definitions, "cad_show", {"view_id": "sdk_visibility", "document_id": "part"})
        require((await call(reopened, definitions, "cad_context", {"view_id": "sdk_visibility"}))["hidden_part_ids"] == [],
                "SDK retargeting did not clear assembly visibility")
        require((await call(reopened, definitions, "cad_context", {"view_id": "sdk_visibility"}))["presentation"] == default_presentation,
                "SDK retargeting did not reset clipping and explosion")


def main():
    if len(sys.argv) not in {2, 3}:
        raise SystemExit("Usage: python tests/mcp_sdk_smoke.py /path/to/agent-3d-cad [native-process-fixture]")
    installed = importlib.metadata.version("mcp")
    if installed != EXPECTED_SDK:
        raise SystemExit(f"Install the pinned developer requirements; expected mcp=={EXPECTED_SDK}, got {installed}")
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="cad-mcp-sdk-") as directory:
        asyncio.run(asyncio.wait_for(smoke(executable, Path(directory)), timeout=90))
    print(f"MCP SDK {installed}: {checks} interoperability checks passed (native stdio, auto + legacy lifecycle)")


if __name__ == "__main__":
    main()
