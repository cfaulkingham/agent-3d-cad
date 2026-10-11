"""Developer-only independent GIF decode, native CLI and JSON-schema evidence."""
import hashlib
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import time
from PIL import Image
from jsonschema import Draft202012Validator

exe = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0
native_env = dict(os.environ)
if os.environ.get("AGENTCAD_ORACLE_EMPTY_PATH") == "1":
    native_env["PATH"] = ""
    for key in ("DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "LD_LIBRARY_PATH", "CASROOT", "CSF_OCCTResourcePath"):
        native_env.pop(key, None)
definitions = json.loads(subprocess.check_output([exe, "tools"], text=True, env=native_env))
tools = {item["name"]: item for item in definitions}
for definition in definitions:
    for key in ("inputSchema", "outputSchema"):
        Draft202012Validator.check_schema(definition[key])
        checks += 1

with tempfile.TemporaryDirectory(prefix="cad-visual-oracle-") as workspace:
    def call(tool, args):
        global checks
        Draft202012Validator(tools[tool]["inputSchema"]).validate(args)
        deadline = time.monotonic() + 5
        while True:
            run = subprocess.run([exe, "call", tool, "--workspace", workspace, "--input", "-"],
                                 input=json.dumps(args), text=True, capture_output=True, timeout=60, env=native_env)
            if run.returncode == 0:
                break
            error = json.loads(run.stderr).get("error", {})
            if error.get("code") != "workspace_busy" or time.monotonic() >= deadline:
                raise AssertionError(run.stderr)
            time.sleep(.01)
        result = json.loads(run.stdout)
        Draft202012Validator(tools[tool]["outputSchema"]).validate(result)
        checks += 2
        return result

    model = {"schema_version": 1, "units": "mm", "parameters": {},
             "features": [{"id": "box", "type": "box", "size": [20, 10, 6]},
                          {"id": "assembly", "type": "assembly", "parts": [{"id": "plate", "input": "box"}]}],
             "output": "assembly"}
    call("cad_create", {"document_id": "box", "model": model})
    original = call("cad_read", {"document_id": "box"})
    call("cad_open", {"document_id": "box", "view_id": "gif"})
    for _ in range(500):
        sync = call("cad_viewer", {"action": "sync", "view_id": "gif"})
        if sync["state"] == "ready":
            break
        assert sync["state"] == "loading", sync
        time.sleep(.01)
    else:
        raise AssertionError("View did not settle")

    def presentation(offset):
        return {"clip": {"normal": [1, 0, 0], "offset_mm": offset, "keep": "positive"},
                "explode": {"distance_mm": 0, "directions": []}}

    sequence = {"name": "Clipping oracle", "frames": [
        {"time_s": 0, "presentation": presentation(0), "joints": []},
        {"time_s": 1, "presentation": presentation(20), "joints": []}]}
    call("cad_viewer", {"action": "sequence", "operation": "save", "view_id": "gif",
                        "evaluation_id": sync["evaluation_id"], "sequence": sequence})
    settings = {"width": 128, "height": 128,
                "camera": {"yaw": 0, "pitch": math.pi / 2, "zoom": 1, "pan": [0, 0]},
                "render": {"lighting": {"enabled": False}, "background": {"color": [1, 1, 1]},
                           "grid": False, "axes": False, "edges": False},
                "appearance": {"default_color": [1, 0, 0], "parts": []}}
    args = {"source": {"document_id": "box", "revision": 1}, "format": "gif", "loop": True,
            "animation": {"view_id": "gif", "sequence_name": sequence["name"],
                          "sample_rate_hz": 4, "max_frames": 4}, "settings": settings}
    result = call("cad_render", args)
    data = pathlib.Path(result["path"]).read_bytes()
    assert hashlib.sha256(data).hexdigest() == result["sha256"]
    assert result["frame_count"] == 4 and result["duration_s"] == 1 and result["loop"] is True
    assert result["downloads"][0]["mimeType"] == "image/gif"
    with Image.open(result["path"]) as decoded:
        assert decoded.format == "GIF" and decoded.size == (128, 128)
        assert decoded.n_frames == 4 and decoded.info["loop"] == 0
        duration = 0
        for frame in range(4):
            decoded.seek(frame)
            duration += decoded.info["duration"]
            pixels = decoded.convert("RGB")
            static_settings = dict(settings, presentation=presentation(frame * 5))
            static = call("cad_render", {"source": args["source"], "settings": static_settings})
            with Image.open(static["path"]) as png:
                assert pixels.tobytes() == png.convert("RGB").tobytes()
            # A 20x10 mm top view has scale .68*128/20. Clipping raises
            # its left boundary by exactly 5 mm each quarter second.
            threshold = 64 + (frame * 5 - 10) * (.68 * 128 / 20)
            red = 0
            for y in range(128):
                for x in range(128):
                    expected = (20 <= x <= 107 and 42 <= y <= 85 and x + .5 >= threshold)
                    actual = pixels.getpixel((x, y)) == (255, 0, 0)
                    assert expected == actual, (frame, x, y, threshold)
                    red += actual
            assert red > 0
            checks += 128 * 128 + 5
        assert duration == 1000
    assert call("cad_read", {"document_id": "box"}) == original
    job = call("cad_job", {"action": "submit", "request_id": "gif_oracle", "tool": "cad_render", "arguments": args})
    for _ in range(500):
        job = call("cad_job", {"action": "get", "job_id": job["job_id"]})
        if job["state"] not in ("queued", "running", "cancelling"):
            break
        time.sleep(.01)
    assert job["state"] == "succeeded" and job["result"]["sha256"] == result["sha256"], job
    checks += 8
print(f"Independent native visual oracle: {checks} checks; 4 GIF frames/PNG parity, analytic clipping, 1000ms timing, source, job and schemas pass")
