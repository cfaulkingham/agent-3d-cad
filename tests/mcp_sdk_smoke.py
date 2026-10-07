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
import importlib.metadata
import json
import math
import os
from pathlib import Path
import sys
import tempfile

from jsonschema import Draft202012Validator
from mcp import Client, StdioServerParameters


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
    edits = []
    previous = "base"
    for index in range(3):
        feature_id = f"copies{index}"
        step = [0, 0, 0]
        step[index] = 30
        edits.append({"op": "add_feature", "feature": {
            "id": feature_id, "type": "pattern", "input": previous, "count": 64, "step": step}})
        previous = feature_id
    return edits + [{"op": "set_output", "feature_id": previous}]


async def discover(client):
    require(client.protocol_version == "2025-11-25", "SDK did not negotiate the server's advertised baseline")
    require(client.session.initialize_result is not None, "SDK did not complete the initialize handshake")
    require(client.server_info.name == "agent-3d-cad", "Unexpected native server identity")
    require(client.server_capabilities.tools is not None, "Server did not advertise tools")
    require(client.server_capabilities.resources is not None, "Server did not advertise app resources")
    result = await client.list_tools()
    definitions = {tool.name: tool for tool in result.tools}
    require({"cad_create", "cad_read", "cad_apply", "cad_export", "cad_drawing", "cad_job", "cad_open", "cad_show", "cad_context", "cad_list", "cad_viewer"} <= definitions.keys(),
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
    require(len(result.content) == 1 and result.content[0].type == "text",
            f"{name} did not return the compatible JSON text content")
    require(json.loads(result.content[0].text) == value, f"{name} text and structured content disagree")
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
        current = await call(reopened, definitions, "cad_read", {"document_id": "part"})
        require(current["revision"] == 2, "Reopened SDK session lost committed HEAD")
        historical = await call(reopened, definitions, "cad_read", {"document_id": "part", "revision": 1})
        require(historical["model"] == created["model"], "Historical revision changed after restart")
        require(await call(reopened, definitions, "cad_create", {
            "document_id": "part", "model": box_model(), "request_id": "sdk_create"}) == created,
                "Durable mutation deduplication failed across SDK sessions")
        persisted = await call(reopened, definitions, "cad_job", {"action": "get", "job_id": "sdk_query"})
        require(persisted["state"] == "succeeded", "Job result was not durable across SDK sessions")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tests/mcp_sdk_smoke.py /path/to/agent-3d-cad")
    installed = importlib.metadata.version("mcp")
    if installed != EXPECTED_SDK:
        raise SystemExit(f"Install the pinned developer requirements; expected mcp=={EXPECTED_SDK}, got {installed}")
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="cad-mcp-sdk-") as directory:
        asyncio.run(asyncio.wait_for(smoke(executable, Path(directory)), timeout=90))
    print(f"MCP SDK {installed}: {checks} interoperability checks passed (native stdio, auto + legacy lifecycle)")


if __name__ == "__main__":
    main()
