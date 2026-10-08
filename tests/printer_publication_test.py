#!/usr/bin/env python3
"""Exercise offline artifact publication followed by job-result storage failure.

The real document lock blocks publication while result.json is made a directory.
No native hooks, hardware commands, source modifications, or race-winning sleeps
are used. The standard-library flock barrier runs on POSIX hosts only.
"""
import argparse
import fcntl
import hashlib
import json
import pathlib
import subprocess
import tempfile
import time


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_snapshot(root):
    return {str(path.relative_to(root)): (path.stat().st_size, sha256(path))
            for path in root.rglob("*") if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--expected-executable-sha256")
    parser.add_argument("--evidence-path", type=pathlib.Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    executable_sha = sha256(executable)
    checks = 0

    def require(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    if args.expected_executable_sha256:
        require(executable_sha == args.expected_executable_sha256,
                "The pinned private executable changed")
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="agentcad-printer-publication-") as temporary:
        root = pathlib.Path(temporary)
        workspace = root / "workspace"

        def call(tool, arguments):
            process = subprocess.run([str(executable), "call", tool, "--workspace",
                                      str(workspace), "--input", "-"],
                                     input=json.dumps(arguments), text=True,
                                     capture_output=True, timeout=100)
            if process.returncode:
                raise AssertionError(f"{tool} failed: {process.stderr}")
            return json.loads(process.stdout)

        def read_job(job_id):
            # Readers may encounter the short admission lock. Retry only this
            # explicitly read-only contention; never replay a failed plan.
            deadline = time.monotonic() + 10
            while True:
                process = subprocess.run([str(executable), "call", "cad_job", "--workspace",
                                          str(workspace), "--input", "-"],
                                         input=json.dumps({"action": "get", "job_id": job_id}),
                                         text=True, capture_output=True, timeout=15)
                if process.returncode == 0:
                    return json.loads(process.stdout)
                error = json.loads(process.stderr.strip().splitlines()[-1])["error"]
                if error["code"] != "workspace_busy" or time.monotonic() >= deadline:
                    raise AssertionError(f"Job read failed: {error}")
                time.sleep(.01)

        def wait_for(job_id, predicate):
            deadline = time.monotonic() + 90
            while True:
                value = read_job(job_id)
                if predicate(value):
                    return value
                if value["state"] not in {"queued", "running", "cancelling"}:
                    raise AssertionError(f"Job ended before the required checkpoint: {value}")
                if time.monotonic() >= deadline:
                    raise AssertionError("Printer publication fixture exceeded its wait bound")
                time.sleep(.01)

        model = {"schema_version": 1, "units": "mm", "parameters": {},
                 "features": [{"id": "body", "type": "box", "size": [10, 10, 2]}],
                 "output": "body"}
        created = call("cad_create", {"document_id": "part", "model": model})
        require(created["revision"] == 1, "The fixture did not create committed source")
        profiles = {}
        for role in ("machine", "process", "filament"):
            path = root / (role + ".json")
            path.write_text(json.dumps({"type": role, "name": "Explicit fixture " + role,
                                        "post_process": []}) + "\n")
            profiles[role] = {"path": str(path), "expected_sha256": sha256(path)}
        review = {"firmware": "marlin", "machine": {"name": "Analytical fixture",
                    "motion_bounds_mm": [[-20, 20], [-20, 20], [0, 100]]},
                  "material": {"name": "Analytical PLA range",
                    "nozzle_temperature_c": [190, 230], "bed_temperature_c": [50, 70]},
                  "initial": {"units": "mm", "xyz_mode": "absolute",
                    "extrusion_mode": "absolute", "position_mm": [0, 0, 0], "extruder_mm": 0}}
        gcode = root / "actual.gcode"
        good = b"G21\nG90\nM83\nM104S210\nM140S60\nG1X10Y5Z1E1\n"
        gcode.write_bytes(good)
        plan_arguments = {"document_id": "part", "revision": 1, "action": "plan",
                          "path": str(gcode), "expected_sha256": sha256(gcode), "options": {
                              "printer": {"backend": "manual", "id": "fixture_printer",
                                          "model": "Explicit fixture", "nozzle_diameter_mm": .4,
                                          "bed_type": "Assessed fixture plate", "handoff": "plain_gcode"},
                              "profiles": profiles, "review": review}}
        prior = call("cad_printer_handoff", plan_arguments)
        prior_directory = pathlib.Path(prior["directory"])
        prior_bytes = tree_snapshot(prior_directory)
        source_bytes = tree_snapshot(workspace / "documents")
        exports = workspace / "exports"
        prior_entries = {path.name for path in exports.iterdir()}
        require(prior_entries == {prior_directory.name}, "Unexpected fixture exports")

        # Keep the plan genuinely large without exceeding line/count limits.
        with gcode.open("wb") as stream:
            stream.write(good)
            block = b";" + b"x" * 1022 + b"\n"
            for _ in range(32768):
                stream.write(block)
        plan_arguments["expected_sha256"] = sha256(gcode)
        require(gcode.stat().st_size > 32 * 1024 * 1024, "The large input was not created")
        submission = {"action": "submit", "request_id": "result_storage_failure",
                      "tool": "cad_printer_handoff", "arguments": plan_arguments,
                      "budget": {"timeout_ms": 90000}}

        # This is the same flock file used by DocumentLock. A running planner
        # cannot rename its stage until the barrier is released, regardless of
        # machine speed or how far inspection has progressed.
        with (workspace / ".locks" / "part.lock").open("r+b") as publication_lock:
            fcntl.flock(publication_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            admitted = call("cad_job", submission)
            require(admitted["job_id"] == submission["request_id"], "Job identity changed")
            running = wait_for(admitted["job_id"], lambda value: value["state"] == "running")
            require(running["state"] == "running", "No running native planner was observed")
            result_path = workspace / "jobs" / admitted["job_id"] / "result.json"
            require(not result_path.exists(), "Result unexpectedly preceded the publication barrier")
            result_path.mkdir()
            require(result_path.is_dir(), "The deterministic result storage obstruction is missing")
            visible = {path.name for path in exports.iterdir() if not path.name.startswith(".pending-")}
            require(visible == prior_entries, "A package published while the document lock was held")
            fcntl.flock(publication_lock, fcntl.LOCK_UN)

        failed = wait_for(admitted["job_id"], lambda value: value["state"] not in
                          {"queued", "running", "cancelling"})
        require(failed["state"] == "failed", "Result storage failure was hidden")
        require(failed["error"]["code"] == "storage_error", "Failure lost its explicit storage error")
        require("Publish file" in failed["error"]["message"],
                "The terminal error did not identify the obstructed atomic result publication")
        require("result" not in failed, "A failed job exposed an unpersisted result")
        require(result_path.is_dir(), "The fault obstruction was unexpectedly removed")
        new_entries = {path.name for path in exports.iterdir()} - prior_entries
        require(len(new_entries) == 1, "Expected one published package despite result storage failure")
        orphan = exports / next(iter(new_entries))
        require(orphan.is_dir() and not orphan.name.startswith(".pending-"),
                "The leftover package is only an unpublished private stage")
        require(not any(path.name.startswith(".pending-") for path in exports.iterdir()),
                "Completed planning left a private stage")
        plan_path = orphan / "plan.json"
        plan = json.loads(plan_path.read_bytes())
        require(plan["source"]["document_id"] == "part" and plan["source"]["revision"] == 1,
                "Published package lost committed source qualification")
        require(plan["gcode"]["sha256"] == plan_arguments["expected_sha256"],
                "Published package lost exact input identity")
        manifest = json.loads((orphan / "manifest.json").read_bytes())
        for entry in manifest["artifacts"]:
            artifact = orphan / entry["path"]
            require(entry["bytes"] == artifact.stat().st_size and entry["sha256"] == sha256(artifact),
                    "Published package ledger differs from actual bytes")
        require(plan["hardware_contact"] is False and plan["physical_print_started"] is False,
                "An offline planner claimed physical effects")
        verified = call("cad_printer_handoff", {"document_id": "part", "revision": 1,
                        "action": "verify", "plan_path": str(plan_path), "expected_sha256": sha256(plan_path)})
        require(verified["action"] == "verify" and verified["report"] == plan["report"],
                "The published package cannot be independently re-verified")
        require(verified["hardware_contact"] is False and verified["physical_print_started"] is False,
                "Verification claimed physical effects")
        require(tree_snapshot(workspace / "documents") == source_bytes,
                "Result storage failure changed source HEAD/history/receipts")
        require(tree_snapshot(prior_directory) == prior_bytes,
                "Result storage failure changed prior export bytes")
        replay = call("cad_job", submission)
        require(replay["state"] == "failed" and replay["job_id"] == failed["job_id"],
                "A terminal failed request was silently retried")
        require({path.name for path in exports.iterdir()} == prior_entries | new_entries,
                "Reading/replaying the failed job published another package")
        orphan_bytes = tree_snapshot(orphan)
        deliberate_retry = dict(submission, request_id="deliberate_retry")
        retried = call("cad_job", deliberate_retry)
        succeeded = wait_for(retried["job_id"], lambda value: value["state"] not in
                             {"queued", "running", "cancelling"})
        require(succeeded["state"] == "succeeded", "A deliberate retry did not complete")
        retry_result = succeeded["result"]
        require(pathlib.Path(retry_result["directory"]) != orphan and
                retry_result["gcode_sha256"] == plan_arguments["expected_sha256"],
                "A deliberate retry did not create its own package from the same input")
        require(retry_result["hardware_contact"] is False and retry_result["physical_print_started"] is False,
                "A deliberate retry claimed hardware effects")
        require(len(list(exports.iterdir())) == len(prior_entries) + 2,
                "A deliberate retry did not leave exactly the prior and two new packages")
        require(tree_snapshot(orphan) == orphan_bytes and tree_snapshot(prior_directory) == prior_bytes,
                "A deliberate retry changed a prior package")
        require(tree_snapshot(workspace / "documents") == source_bytes,
                "A deliberate retry changed source HEAD/history/receipts")
        require(sha256(executable) == executable_sha, "The executable changed during the fixture")
        evidence = {"checks": checks, "elapsed_seconds": round(time.monotonic() - started, 2),
                    "executable_sha256": executable_sha, "native_build": plan["source"]["native_build"],
                    "gcode_bytes": gcode.stat().st_size, "job_state": failed["state"],
                    "error_code": failed["error"]["code"], "error_message": failed["error"]["message"],
                    "published_package_sha256": sha256(plan_path),
                    "publication_barrier": "native_document_flock", "hardware_contact": False,
                    "physical_print_started": False, "source_and_prior_exports_preserved": True,
                    "deliberate_retry_created_another_package": True}
        if args.evidence_path:
            args.evidence_path.write_text(json.dumps(evidence, indent=2) + "\n")
        print(json.dumps(evidence))


if __name__ == "__main__":
    main()
