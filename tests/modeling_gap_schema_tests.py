"""Validate real combined native modeling calls with independent Draft 2020-12.

Developer only: use the pinned jsonschema in tests/mcp_sdk_requirements.txt.
Run: python tests/modeling_gap_schema_tests.py /absolute/path/to/agent-3d-cad
The executable must include the complete core, sheet-metal and surface phases.
No Python dependency is added to the native service or distributed bundles.
--available-only runs only discoverable models for intermediate binary confidence;
it omits the combined lifecycle assertions and is not final acceptance evidence.
"""
import base64
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from jsonschema import Draft202012Validator


check_args = len(sys.argv) in (2, 3) and (len(sys.argv) == 2 or sys.argv[2] == "--available-only")
if not check_args:
    raise SystemExit("Usage: modeling_gap_schema_tests.py EXECUTABLE [--available-only]")
available_only = len(sys.argv) == 3
exe = str(Path(sys.argv[1]).resolve())
catalog = json.loads(subprocess.check_output([exe, "tools"], text=True, timeout=40))
tools = {tool["name"]: tool for tool in catalog}
checks = 0
covered = set()
for definition in tools.values():
    for kind in ("inputSchema", "outputSchema"):
        Draft202012Validator.check_schema(definition[kind])
        checks += 1


def check(condition, reason):
    global checks
    assert condition, reason
    checks += 1


def validate(tool, kind, value):
    global checks
    try:
        Draft202012Validator(tools[tool][kind]).validate(value)
    except Exception as error:
        raise AssertionError(f"{tool} {kind} failed: {error}") from error
    checks += 1


def rejected(tool, kind, value):
    check(not Draft202012Validator(tools[tool][kind]).is_valid(value),
          f"{tool} {kind} accepted an invalid contract: {json.dumps(value)}")


def discovery_field(schema, root, field):
    """Read discovery structure without discarding Draft 2020-12 ref siblings.

    This is introspection only; all acceptance checks use the untouched native
    catalog through Draft202012Validator. Never flatten or merge its assertions.
    """
    seen = set()
    while field not in schema:
        check("$ref" in schema, f"Discovery schema supplies {field}")
        reference = schema["$ref"]
        check(reference not in seen, "Discovery lookup has no reference cycle")
        seen.add(reference)
        check(reference.startswith("#"), "Discovery references stay local")
        if reference.startswith("#/"):
            schema = root
            for token in reference[2:].split("/"):
                schema = schema[token.replace("~1", "/").replace("~0", "~")]
        else:
            matches = [body for body in root.get("$defs", {}).values()
                       if isinstance(body, dict) and body.get("$anchor") == reference[1:]]
            check(len(matches) == 1, "Discovery anchor resolves uniquely")
            schema = matches[0]
    return schema[field]


def same_geometry(left, right):
    """Native cache serialization can change the last floating-point bit."""
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(same_geometry(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(same_geometry(a, b) for a, b in zip(left, right))
    if type(left) in (int, float) and type(right) in (int, float):
        return math.isclose(left, right, rel_tol=1e-8, abs_tol=1e-6)
    return left == right


create_schema = tools["cad_create"]["inputSchema"]
model_properties = discovery_field(create_schema["properties"]["model"], create_schema, "properties")
feature_schema = discovery_field(model_properties["features"], create_schema, "items")
discovered = set()
for alternative in discovery_field(feature_schema, create_schema, "oneOf"):
    properties = discovery_field(alternative, create_schema, "properties")
    discriminator = properties["type"]
    discovered.update([discriminator["const"]] if "const" in discriminator else discriminator["enum"])


def plane(origin=(0, 0, 0), normal=(0, 0, 1), x_direction=(1, 0, 0)):
    return {"origin": list(origin), "normal": list(normal), "x_direction": list(x_direction)}


def sketch(name="profile", width=10, height=20, origin=(0, 0, 0), radius=None):
    profile = {"type": "circle", "radius": radius} if radius is not None else {
        "type": "rectangle", "width": width, "height": height}
    return {"id": name, "type": "sketch", "workplane": plane(origin), "profile": profile}


def model(features, output=None, parameters=None):
    return {"schema_version": 1, "units": "mm", "parameters": parameters or {},
            "features": copy.deepcopy(features), "output": output or features[-1]["id"]}


def face(source, normal=(0, 0, 1)):
    return {"type": "geometric", "feature_id": source, "surface_kind": "plane", "expected_count": 1,
            "normal": {"vector": list(normal), "tolerance": .000001}}


def edge(source="profile", center=(10, 30, 0)):
    return {"type": "geometric", "feature_id": source, "curve_kind": "line", "expected_count": 1,
            "center": {"point": list(center), "tolerance": .000001}}


def wire(start, end):
    return {"type": "wire", "segments": [{"type": "line", "start": list(start), "end": list(end)}]}


def patch(name="patch", poles=None):
    return {"id": name, "type": "surface_bezier", "control_points": poles or [
        [[0, 0, 0], [0, 20, 0]], [[10, 0, 0], [10, 20, 0]]]}


cases = {}
stock = {"id": "stock", "type": "box", "size": [20, 30, 10]}
for kind, options in {
    "shell": {"thickness": -2, "faces": face("stock"), "join": "intersection"},
    "offset": {"distance": 1, "join": "intersection"},
    "thicken": {"thickness": 2, "faces": face("stock")},
    "fillet": {"radius": 1, "edges": "all"},
    "chamfer": {"distance": 1, "edges": "all"},
    "hole": {"origin": [10, 15, 0], "axis": [0, 0, 1], "radius": 2, "depth": 10},
}.items():
    cases[kind] = model([stock, {"id": "result", "type": kind, "input": "stock", **options}])
cases["sealed_shell"] = model([stock, {"id": "result", "type": "shell", "input": "stock", "thickness": -2, "faces": []}])
cases["sketch_thicken"] = model([sketch(), {"id": "result", "type": "thicken", "input": "profile", "thickness": 2}])
cases["extrude_distance"] = model([sketch(), {"id": "result", "type": "extrude", "input": "profile", "distance": 5}])
cases["sweep_default"] = model([sketch(radius=2), {"id": "result", "type": "sweep", "input": "profile", "path": [[0, 0, 0], [0, 0, 10]]}])
for name, options in {
    "directional": {"distance": 10, "direction": [.6, 0, .8]},
    "both": {"distance": 5, "both": True},
    "taper": {"distance": 10, "taper_deg": 5},
}.items():
    cases["extrude_" + name] = model([sketch(radius=5 if name == "taper" else None),
        {"id": "result", "type": "extrude", "input": "profile", **options}])
for extent in ("first", "last"):
    cases["extrude_" + extent] = model([sketch(width=10, height=10),
        {"id": "target", "type": "box", "size": [14, 14, 3], "origin": [-2, -2, 5]},
        {"id": "result", "type": "extrude", "input": "profile", "until": extent, "target": "target"}])
for name, options in {
    "corrected": {"orientation": "corrected_frenet"}, "frenet": {"orientation": "frenet"},
    "fixed": {"orientation": "fixed"}, "binormal": {"binormal": [1, 0, 0]},
    "guide": {"guide": wire((3, 0, 0), (3, 0, 10))},
}.items():
    cases["sweep_" + name] = model([sketch(radius=2), {"id": "result", "type": "sweep",
        "input": "profile", "path": wire((0, 0, 0), (0, 0, 10)), "transition": "transformed", **options}])
cases["variable_sweep"] = model([sketch("a", radius=2), sketch("b", radius=4, origin=(0, 0, 10)),
    {"id": "result", "type": "sweep", "sections": ["a", "b"], "path": wire((0, 0, 0), (0, 0, 10))}])
for kind in ("sketch_cut", "sketch_fuse", "sketch_intersection"):
    cases[kind] = model([sketch("a"), sketch("b", origin=(5, 0, 0)),
        {"id": "combined", "type": kind, "left": "a", "right": "b"},
        {"id": "result", "type": "extrude", "input": "combined", "distance": 5}])
for kind, options in {
    "sketch_offset": {"distance": 1, "join": "intersection"},
    "sketch_fillet": {"radius": 2, "vertices": "all"},
    "sketch_chamfer": {"distance": 2, "vertices": {"type": "geometric", "feature_id": "profile",
        "point": [0, 0, 0], "tolerance": .000001, "expected_count": 1}},
    "sketch_transform": {"translation": [4, 0, 0]},
    "sketch_instance": {"translation": [4, 0, 0]},
    "sketch_mirror": {"plane": plane(normal=(1, 0, 0), x_direction=(0, 1, 0))},
}.items():
    cases[kind] = model([sketch(), {"id": "derived", "type": kind, "input": "profile", **options},
        {"id": "result", "type": "extrude", "input": "derived", "distance": 5}])
for kind in ("sketch_face", "sketch_projection"):
    options = {"workplane": plane()} if kind == "sketch_projection" else {}
    cases[kind] = model([stock, {"id": "profile", "type": kind, "input": "stock", "faces": [face("stock")], **options},
        {"id": "result", "type": "extrude", "input": "profile", "distance": 2}])
for kind in ("cut", "fuse", "intersection"):
    cases[kind] = model([stock, {"id": "tool", "type": "cylinder", "radius": 5, "height": 10, "origin": [10, 15, 0]},
        {"id": "result", "type": kind, "left": "stock", "right": "tool"}])
for kind, options in {
    "mirror": {"plane": plane(normal=(1, 0, 0), x_direction=(0, 1, 0))},
    "split": {"plane": plane(origin=(0, 0, 5)), "keep": "both"},
    "transform": {"translation": [2, 0, 0]}, "instance": {"translation": [2, 0, 0]},
    "pattern": {"count": 2, "step": [25, 0, 0]},
    "circular_pattern": {"count": 3, "axis": {"origin": [-100, 0, 0], "direction": [0, 0, 1]}, "angle_deg": 120},
}.items():
    cases[kind] = model([stock, {"id": "result", "type": kind, "input": "stock", **options}])
cases["revolve"] = model([sketch(width=2, height=3, origin=(3, 0, 0)),
    {"id": "result", "type": "revolve", "input": "profile", "axis": {"origin": [0, 0, 0], "direction": [0, 1, 0]}, "angle_deg": 360}])
cases["loft"] = model([sketch("a", radius=2), sketch("b", radius=4, origin=(0, 0, 10)),
    {"id": "result", "type": "loft", "sections": ["a", "b"], "ruled": False}])
cases["external_thread"] = model([{"id": "result", "type": "external_thread", "major_diameter": 12, "pitch": 1, "length": 2}])
cases["assembly"] = model([stock, {"id": "result", "type": "assembly", "parts": [
    {"id": "left", "input": "stock"}, {"id": "right", "input": "stock", "placement": {"translation": [25, 0, 0]}}]}])
sheet = model([sketch(width=20, height=30), {"id": "formed", "type": "sheet_metal", "input": "profile",
    "thickness": {"parameter": "wall"}, "k_factor": {"parameter": "k"}, "flanges": [{"id": "top", "edge": edge(),
        "inside_radius": 3, "angle_deg": 90, "length": 10}]}, {"id": "flat", "type": "sheet_unfold", "input": "formed"}],
    parameters={"wall": 2, "k": .5})
cases["sheet"] = sheet
cases["bezier"] = model([patch()])
splined = {**patch(), "type": "surface_bspline", "degree_u": 1, "degree_v": 1,
    "knots_u": [0, 1], "knots_v": [0, 1], "multiplicities_u": [2, 2], "multiplicities_v": [2, 2]}
cases["bspline"] = model([splined])
cases["surface_trim"] = model([splined, {"id": "trimmed", "type": "surface_trim", "input": "patch",
    "u_range": [.25, .75], "v_range": [.25, .75]}])
cases["surface_thicken"] = model([patch(), {"id": "result", "type": "thicken", "input": "patch", "thickness": 2}])
box_patches = [
    patch("bottom", [[[0, 0, 0], [10, 0, 0]], [[0, 20, 0], [10, 20, 0]]]),
    patch("top", [[[0, 0, 5], [0, 20, 5]], [[10, 0, 5], [10, 20, 5]]]),
    patch("front", [[[0, 0, 0], [0, 0, 5]], [[10, 0, 0], [10, 0, 5]]]),
    patch("back", [[[10, 20, 0], [10, 20, 5]], [[0, 20, 0], [0, 20, 5]]]),
    patch("left", [[[0, 20, 0], [0, 20, 5]], [[0, 0, 0], [0, 0, 5]]]),
    patch("right", [[[10, 0, 0], [10, 0, 5]], [[10, 20, 0], [10, 20, 5]]]),
]
closed = {"id": "shell", "type": "surface_shell", "inputs": [f["id"] for f in box_patches], "tolerance": .0000001, "closed": True}
cases["closed_surface_shell"] = model([*box_patches, closed])
cases["surface_solid"] = model([*box_patches, closed, {"id": "result", "type": "surface_solid", "input": "shell"}])
cases["open_surface_shell"] = model([box_patches[0], box_patches[2], {**closed, "inputs": ["bottom", "front"], "closed": False}])


# Complete parity coverage is deliberately part of the combined suite: every
# discovered feature must survive create/read/query and unknown-field rejection.
for kind, options in {
    "scale": {"origin": [1, 2, 3], "factors": [2, 3, .5]},
    "draft": {"faces": face("stock", (1, 0, 0)), "angle_deg": 5,
              "direction": [0, 0, 1], "neutral_plane": plane()},
}.items():
    cases[kind] = model([stock, {"id": "result", "type": kind, "input": "stock", **options}])
cases["twist_extrude"] = model([sketch(width=4, height=2),
    {"id": "result", "type": "twist_extrude", "input": "profile", "distance": -10,
     "angle_deg": 90, "center": [1, 1, 0]}])
weighted = {"id": "curve", "type": "curve", "path": {"type": "wire", "segments": [
    {"type": "bezier", "points": [[2, 0, 0], [2, 2, 0], [0, 2, 0]], "weights": [1, math.sqrt(.5), 1]}]}}
cases["curve"] = model([weighted])
cases["curve_helix"] = model([{"id": "result", "type": "curve_helix", "frame": plane(),
    "radius": 2, "pitch": 3, "turns": 1.5}])
for kind, options in {
    "curve_trim": {"start": .2, "end": .8},
    "curve_tangent_line": {"position": .5, "length": 2},
}.items():
    cases[kind] = model([weighted, {"id": "result", "type": kind, "input": "curve", **options}])
cases["curve_tangent_arc"] = model([{"id": "curve", "type": "curve", "path": wire((0, 0, 0), (2, 0, 0))},
    {"id": "result", "type": "curve_tangent_arc", "input": "curve", "position": 1, "end": [3, 1, 0]}])
cases["curve_extract"] = model([stock, {"id": "result", "type": "curve_extract", "input": "stock",
    "edges": edge("stock", (10, 0, 0))}])
for name, features in {
    "sketch_hull": [sketch("left", radius=1), sketch("right", radius=1, origin=(4, 0, 0)),
        {"id": "derived", "type": "sketch_hull", "inputs": ["left", "right"], "workplane": plane()}],
    "sketch_trace": [weighted, {"id": "derived", "type": "sketch_trace", "input": "curve", "workplane": plane(), "width": .4}],
    "sketch_full_round": [sketch(width=10, height=4),
        {"id": "derived", "type": "sketch_full_round", "input": "profile", "edges": edge("profile", (10, 2, 0))}],
}.items():
    cases[name] = model([*features, {"id": "result", "type": "extrude", "input": "derived", "distance": 1}])

def segments(points):
    return [wire(p, points[(i+1) % len(points)])["segments"][0] for i, p in enumerate(points)]

cases["surface_fill"] = model([{"id": "result", "type": "surface_fill", "tolerance": 1e-5,
    "boundaries": [{"curve": c, "continuity": "C0"} for c in segments([[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0]])]}])
cases["surface_gordon"] = model([{"id": "result", "type": "surface_gordon",
    "u_curves": [wire((0, y, 0), (10, y, 0))["segments"][0] for y in (0, 10)],
    "v_curves": [wire((x, 0, 0), (x, 10, 0))["segments"][0] for x in (0, 10)],
    "u_parameters": [0, 1], "v_parameters": [0, 1], "tolerance": 1e-6}])
cylindrical = {"id": "patch", "type": "surface_bezier",
    "control_points": [[[10, 0, 0], [10, 0, 5]], [[10, 10, 0], [10, 10, 5]], [[0, 10, 0], [0, 10, 5]]],
    "weights": [[1, 1], [math.sqrt(.5)]*2, [1, 1]]}
for kind, path in {
    "curve_project": wire((15, 2, 2), (15, 8, 2)),
    "surface_project": {"type": "wire", "segments": segments([[15, 2, 1], [15, 8, 1], [15, 8, 4], [15, 2, 4]])},
}.items():
    cases[kind] = model([cylindrical, {"id": "curve", "type": "curve", "path": path},
        {"id": "result", "type": kind, "input": "curve", "target": "patch",
         "faces": {"type": "geometric", "feature_id": "patch", "surface_kind": "bezier", "expected_count": 1},
         "direction": [-1, 0, 0]}])
font_bytes = (Path(__file__).parent / "fixtures" / "authoring-test.ttf").read_bytes()
cases["text_on_path"] = model([
    {"id": "letters", "type": "sketch", "workplane": plane(), "profile": {"type": "text", "text": "BB", "height": 10,
        "font": {"content_base64": base64.b64encode(font_bytes).decode(), "sha256": hashlib.sha256(font_bytes).hexdigest()}}},
    {"id": "path", "type": "curve", "path": wire((10, 20, 0), (110, 20, 0))},
    {"id": "placed", "type": "text_on_path", "input": "letters", "path": "path", "start": 5, "offset": 2},
    {"id": "result", "type": "extrude", "input": "placed", "distance": 3}])

if available_only:
    available_validator = Draft202012Validator(create_schema)
    skipped = [name for name, intent in cases.items()
               if not available_validator.is_valid({"document_id": name, "model": intent})]
    cases = {name: intent for name, intent in cases.items() if name not in skipped}


with tempfile.TemporaryDirectory(prefix="cad-modeling-gap-schema-") as directory:
    workspace = Path(directory) / "workspace"

    def invoke(tool, args, expected_error=None, check_input=True):
        if check_input:
            validate(tool, "inputSchema", args)
        deadline = time.monotonic() + 5
        while True:
            run = subprocess.run([exe, "call", tool, "--workspace", str(workspace), "--input", "-"],
                input=json.dumps(args), text=True, capture_output=True, timeout=60)
            if not run.returncode:
                check(expected_error is None, f"{tool} unexpectedly accepted invalid intent")
                result = json.loads(run.stdout)
                validate(tool, "outputSchema", result)
                return result
            failure = json.loads(run.stderr)["error"]
            if failure["code"] == "workspace_busy" and time.monotonic() < deadline:
                time.sleep(.02)
                continue
            check(expected_error is not None and failure["code"] in expected_error,
                  f"{tool} failed: {run.stderr}")
            check(isinstance(failure.get("message"), str) and isinstance(failure.get("details"), dict),
                  "Native modeling failure retains structured evidence")
            return failure

    records = {}
    for name, intent in cases.items():
        records[name] = invoke("cad_create", {"document_id": name, "model": intent})
        covered.update(f["type"] for f in intent["features"])
        saved = invoke("cad_read", {"document_id": name, "revision": records[name]["revision"]})
        check(saved["model"] == intent, f"{name} saves exact editable intent")
        query = invoke("cad_query", {"document_id": name, "revision": 1})
        check(same_geometry(query["summary"], records[name]["summary"]), f"{name} query reproduces committed geometric summary within native tolerance")
        for feature in intent["features"]:
            bad = copy.deepcopy(intent)
            next(f for f in bad["features"] if f["id"] == feature["id"])["unknown"] = True
            rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": bad})
    for kind, source in (("import_step", "transform"), ("import_step_surface", "bezier")):
        exported = invoke("cad_export", {"document_id": source, "revision": 1, "format": "step"})
        content = Path(exported["path"]).read_text()
        imported = model([{"id": "result", "type": kind, "content": content,
            "sha256": hashlib.sha256(content.encode()).hexdigest()}])
        receipt = invoke("cad_create", {"document_id": kind, "model": imported})
        saved = invoke("cad_read", {"document_id": kind, "revision": receipt["revision"]})
        check(saved["model"] == imported, f"{kind} preserves exact captured source and hash")
        covered.add(kind)
    check(discovered == covered, f"Actual combined calls cover every discovered feature kind; missing={discovered-covered}, extra={covered-discovered}")
    if available_only:
        print(f"Intermediate available-schema confidence: {checks} checks / {len(cases)+2} models / {len(covered)} feature kinds")
        print(f"Skipped undiscovered models: {', '.join(skipped)}; combined lifecycle not tested")
        raise SystemExit(0)

    def near(actual, expected):
        check(math.isclose(actual, expected, rel_tol=1e-7, abs_tol=1e-4), f"Expected {expected}, received {actual}")

    near(records["shell"]["summary"]["volume_mm3"], 6000-16*26*8)
    near(records["offset"]["summary"]["volume_mm3"], 22*32*12)
    near(records["extrude_first"]["summary"]["volume_mm3"], 500)
    near(records["extrude_last"]["summary"]["volume_mm3"], 800)
    near(records["variable_sweep"]["summary"]["volume_mm3"], math.pi*10/3*(4+8+16))
    near(records["surface_solid"]["summary"]["volume_mm3"], 1000)
    check(records["bezier"]["summary"]["solid_count"] == 0 and records["bezier"]["summary"]["volume_mm3"] == 0,
          "Surface output has honest non-solid summary")
    near(records["surface_trim"]["summary"]["area_mm2"], 50)
    sheet_report = records["sheet"]["summary"]["sheet_metal"]
    near(sheet_report["bends"][0]["bend_allowance_mm"], 2*math.pi)
    check(sheet_report["mode"] == "flat" and sheet_report["source_feature_id"] == "formed"
          and sheet_report["volume_preservation_assumed"] is False, "Flat summary retains explicit engineering intent")
    formed = invoke("cad_query", {"document_id": "sheet", "revision": 1, "feature_id": "formed"})
    check(formed["summary"]["sheet_metal"]["mode"] == "formed", "Formed report survives source-qualified query")

    for source in (cases["extrude_first"], cases["sweep_fixed"], cases["sheet"], cases["bspline"]):
        invalid = copy.deepcopy(source)
        invalid["features"][-1]["unknown"] = 1
        for tool, args in {
            "cad_create": {"document_id": "invalid", "model": invalid},
            "cad_apply": {"document_id": "sheet", "expected_revision": 1, "operations": [
                {"op": "add_feature", "feature": invalid["features"][-1]}]},
            "cad_preview": {"document_id": "sheet", "expected_revision": 1, "operations": [
                {"op": "replace_feature", "id": invalid["features"][-1]["id"], "feature": invalid["features"][-1]}]},
        }.items():
            rejected(tool, "inputSchema", args)

    for extra in ({"distance": 10}, {"both": True}, {"taper_deg": 5}):
        invalid = copy.deepcopy(cases["extrude_first"])
        invalid["features"][-1].update(extra)
        rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    for missing in ("until", "target"):
        invalid = copy.deepcopy(cases["extrude_first"])
        del invalid["features"][-1][missing]
        rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    for extra in ({"binormal": [1, 0, 0]}, {"guide": wire((3, 0, 0), (3, 0, 10))}, {"sections": ["profile", "profile"]}):
        invalid = copy.deepcopy(cases["sweep_fixed"])
        invalid["features"][-1].update(extra)
        rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    invalid = copy.deepcopy(cases["sweep_guide"])
    invalid["features"][-1]["binormal"] = [1, 0, 0]
    rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    for selector_change in ({"expected_count": 0}, {"feature_id": False}, {"unknown": True}):
        invalid = copy.deepcopy(cases["shell"])
        invalid["features"][-1]["faces"].update(selector_change)
        rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    invalid = copy.deepcopy(cases["mirror"])
    invalid["features"][-1]["plane"]["x_direction"] = [0, 1]
    rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    invalid = copy.deepcopy(cases["bspline"])
    del invalid["features"][0]["degree_u"]
    rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    invalid = copy.deepcopy(cases["surface_trim"])
    invalid["features"][-1]["u_range"] = [0, .5, 1]
    rejected("cad_create", "inputSchema", {"document_id": "invalid", "model": invalid})
    for name in ("extrude_first", "surface_trim", "sheet", "shell"):
        invalid = copy.deepcopy(cases[name])
        invalid["features"][-1]["input"] = "missing"
        failure = invoke("cad_create", {"document_id": "bad_reference_"+name, "model": invalid}, {"invalid_model"})
        check(failure["details"]["feature_id"] == invalid["features"][-1]["id"], "Missing dependencies produce feature-level errors")
    invalid = copy.deepcopy(cases["sheet"])
    invalid["features"][1]["flanges"][0]["edge"]["feature_id"] = "formed"
    invoke("cad_create", {"document_id": "wrong_edge_scope", "model": invalid}, {"invalid_model"})
    invalid = copy.deepcopy(cases["shell"])
    invalid["features"][-1]["faces"]["feature_id"] = "result"
    invoke("cad_create", {"document_id": "wrong_face_scope", "model": invalid}, {"invalid_model"})
    invalid = copy.deepcopy(cases["surface_solid"])
    invalid["features"][-1]["input"] = "bottom"
    invoke("cad_create", {"document_id": "wrong_surface_scope", "model": invalid}, {"invalid_model"})

    operations = [{"op": "set_parameter", "name": "k", "value": .3}]
    before = invoke("cad_read", {"document_id": "sheet"})
    for kind in ("mesh", "view"):
        preview = invoke("cad_preview", {"document_id": "sheet", "expected_revision": 1, "operations": operations, "kind": kind})
        check(preview["draft"] is True and preview["summary"]["sheet_metal"]["k_factor"] == .3,
              "Preview returns full draft sheet report")
        check(invoke("cad_read", {"document_id": "sheet"}) == before, "Preview preserves committed record")
    changed = invoke("cad_apply", {"document_id": "sheet", "expected_revision": 1, "operations": operations})
    check(changed["revision"] == 2 and changed["summary"]["sheet_metal"]["k_factor"] == .3,
          "Apply publishes revised sheet report")
    saved = invoke("cad_read", {"document_id": "sheet"})
    invoke("cad_apply", {"document_id": "sheet", "expected_revision": 2, "operations": [
        {"op": "set_parameter", "name": "wall", "value": 0}]}, {"invalid_model"})
    check(invoke("cad_read", {"document_id": "sheet"}) == saved, "Invalid modeling preserves HEAD")
    bad_output = copy.deepcopy(changed)
    bad_output["summary"]["sheet_metal"]["bends"][0]["unknown"] = True
    rejected("cad_apply", "outputSchema", bad_output)
    bad_output = copy.deepcopy(changed)
    bad_output["summary"]["sheet_metal"]["volume_preservation_assumed"] = True
    rejected("cad_apply", "outputSchema", bad_output)
    bad_output = copy.deepcopy(saved)
    bad_output["model"]["features"][1]["flanges"][0]["unknown"] = True
    rejected("cad_read", "outputSchema", bad_output)

    for document in ("sheet", "bezier"):
        view_id = "schema_" + document
        invoke("cad_open", {"view_id": view_id, "document_id": document})
        deadline = time.monotonic() + 30
        while True:
            sync = invoke("cad_viewer", {"action": "sync", "view_id": view_id})
            check(sync["state"] != "error", f"Viewer evaluation failed: {sync}")
            if sync["state"] == "ready":
                break
            check(time.monotonic() < deadline, "Live viewer evaluation completes")
            time.sleep(.02)
        if document == "sheet":
            check(sync["summary"]["sheet_metal"]["k_factor"] == .3, "Live sync publishes revised sheet report")
        else:
            check(sync["summary"]["solid_count"] == 0, "Live sync preserves surface classification")
        unchanged = invoke("cad_viewer", {"action": "sync", "view_id": view_id, "known_evaluation_id": sync["evaluation_id"]})
        check(unchanged["changed"] is False, "Known live evaluation sync obeys unchanged contract")
        offset, payload = 0, ""
        while offset is not None:
            chunk = invoke("cad_viewer", {"action": "mesh", "view_id": view_id,
                "evaluation_id": sync["evaluation_id"], "offset": offset})
            check(chunk["offset"] == offset, "Frozen native mesh preserves chunk offset")
            payload += chunk["data"]
            offset = chunk["next_offset"]
        check(len(payload.encode()) == chunk["total_bytes"], "Frozen mesh chunks cover the declared bytes")
        evaluation = json.loads(payload)
        check(evaluation["evaluation_id"] == sync["evaluation_id"], "Native face pick comes from the displayed evaluation")
        query = invoke("cad_query", {"document_id": document, "revision": sync["revision"], "kind": "topology"})
        entity = evaluation["topology"]["faces"][0]
        pick = {"document_id": document, "revision": sync["revision"], "evaluation_id": sync["evaluation_id"],
                "feature_id": sync["feature_id"], "kind": "face", "entity_id": entity["id"]}
        context = invoke("cad_viewer", {"action": "context", "view_id": view_id,
            "evaluation_id": sync["evaluation_id"], "selection": pick})
        check(context["resolved_selection"]["selector"]["surface_kind"] == entity["surface_kind"],
              "Live resolved face selection publishes its persistent selector")
        invoke("cad_context", {"view_id": view_id})
        resolve_pick = {**pick, "evaluation_id": query["evaluation_id"], "entity_id": query["topology"]["faces"][0]["id"]}
        invoke("cad_resolve_selection", resolve_pick)
        bad_context = copy.deepcopy(context)
        bad_context["resolved_selection"]["selector"]["unknown"] = True
        rejected("cad_viewer", "outputSchema", bad_context)

print(f"Modeling gap schemas: {checks} checks / {len(cases)+2} models / {len(covered)} feature kinds / {len(tools)} tools")
