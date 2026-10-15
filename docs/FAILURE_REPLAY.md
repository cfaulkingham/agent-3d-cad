# Geometry failure packages

Native geometry failures, including worker termination, wall deadlines and memory
failures, are captured locally when possible. The original error code, message
and feature details remain unchanged; `details.failure_package` adds
`failure_id`, an absolute `report.json` path and the SHA-256 of that report.
Cancellation, busy admission and full worker queues do not create packages.
Capture is best effort: unavailable storage or a size limit adds
`details.failure_capture` and never masks the original modeling error.

## Inspect and replay

The shared Service exposes `cad_failure` through CLI and MCP:

```json
{"action":"list","limit":10}
```

List returns the newest packages, explicit unreadable entries, a limit and
`truncated`. Get verifies the payload's bytes before returning its report:

```json
{"action":"get","failure_id":"failure-REPLACE_WITH_RETURNED_ID"}
```

Replay runs the captured candidate and worker request in an isolated temporary
workspace. It never calls the original mutation or publishes document revisions:

```json
{"action":"replay","failure_id":"failure-REPLACE_WITH_RETURNED_ID"}
```

Optional `budget` uses the normal job timeout/memory limits; omitted fields retain
the captured budget. Submit `cad_failure` replay through `cad_job` for responsive
asynchronous work. The outer job's budget controls the entire replay; `get` and
`list` are immediate and cannot be submitted as jobs. Durable replay results are
reused on identical request IDs like other jobs.

Replay outcomes are `reproduced` (same error code, message and feature ID),
`different_failure`, or `succeeded`. Reports include both original and observed
errors and build identities. A matching signature does not establish an identical
stack trace or prove the root cause. A successful replay does not fix the saved
design or apply the failed edit. Native build changes are allowed and reported by
`same_build`; a different kernel version is rejected.

For a copied package, pass an absolute `report.json` `path` and its actual
`expected_sha256` instead of `failure_id`. Both files must stay together. Resolve
platform path aliases before calling; symlinks in the package path are rejected.
The package is verified before geometry runs. No original project folder,
original external STEP file, Python runtime or repository is needed to replay a
captured native model.

## Contents and bounds

Each private `workspace/failures/failure-...` folder contains:

- `report.json`: format `agentcad-failure/1`, timestamp, native/kernel/service
  identity, original error, originating tool arguments and job identity, limits,
  model/request hashes and a payload hash/byte ledger. Large create arguments
  reference the captured model by hash instead of duplicating it in the report.
- `payload.json`: the evaluated candidate model, internal worker request and,
  when available, the exact committed source revision preceding the failed edit
  or used by the query. Captured STEP/font/sketch content remains embedded in the
  model. Missing original source is reported in context; replay uses the actual
  captured candidate.

Reports are bounded to 1 MiB and payloads to 64 MiB of actual bytes, including
embedded imports. The newest 32 generated packages are retained within a
256 MiB total budget, collected during capture. Export/copy a package you need
to keep. These files can contain the user's design and paths; they stay local and
are never uploaded automatically. Unrelated, malformed and symlinked entries
are not removed by retention.

## Replay scope

Replay starts with an empty cache; native rendering, drawing, export,
manufacturing and robot output paths are redirected into temporary scratch and
discarded afterward. Geometry inspection, selection, projection, captured
sketch/STEP inspection and self-contained external rendering/recognition use
their captured data. No cached B-rep, process address, executable path or shell
command is accepted from a package. Replays remain subject to normal native
worker containment and cancellation and do not create recursive failure reports.

An uncaptured external-input worker request, currently G-code file review, is
inspectable but explicitly not replayable. The tool never reopens an old external
path to guess the bytes. Tool-admission/model validation errors before worker
evaluation, coordinator crashes and external slicer processes are outside this
capture boundary. Cache-sensitive or transient process failures may not recur in
a cold replay; the returned outcome is evidence, not a guarantee of reproduction.
