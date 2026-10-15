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


## Minimize a modeling failure

`cad_failure` also accepts `action: "minimize"`, with the same `failure_id` or
absolute report `path` plus `expected_sha256` addressing as replay:

```json
{"action":"minimize","failure_id":"failure-REPLACE_WITH_RETURNED_ID",
 "max_attempts":128,"budget":{"timeout_ms":60000,"memory_mb":4096}}
```

The native reducer first reproduces the original error in a cold workspace.
It then tries removing contiguous feature groups, bypassing an intermediate
feature with its `input`, `left` or `right` operand, removing component provenance,
and removing unused parameters. Every candidate passes ordinary model validation
before a fresh worker evaluates it. A reduction is retained only if the error
code, exact message and failing feature ID match. Feature IDs are preserved;
known dependency/selector fields are redirected for a bypass. Numeric dimensions,
imported bytes and the worker request are not simplified. If a deleted suffix
contained the output, the failing feature becomes output only when that model
validates. These are diagnostic candidates, not repairs to the user's intent.

Supported inputs are feature-qualified `kernel_failure`, `invalid_shape`,
`invalid_model`, `selection_missing`, `selection_ambiguous` and
`selection_count_mismatch` errors from native `summary`, `topology`, `view` or
`curve` evaluation. This covers model builds during create/apply/preview and
native queries. Resource limits, process crashes and artifact-specific requests
are explicitly rejected with `unsupported_minimization`; they remain available
to ordinary replay. Uncaptured external inputs retain the existing
`unsupported_replay` rejection. A baseline that no longer matches
returns `not_reproduced` and does not search or publish a derived package.

`max_attempts` is 1–512, default 128, including the baseline and invalid candidate
trials. Duplicate candidates are skipped. `budget.timeout_ms` bounds the whole
search, not each trial; each worker receives only the remaining time and the
selected memory cap. Limits default to the captured package's budget. A queued
`cad_job` supplies the effective aggregate budget, supports cancellation, and
returns its original durable result on retry. Cancellation or an expired outer
job prevents publication. A synchronous search can return its best verified
reduction when its attempt/time limit is reached.

Results report `outcome` (`reduced`, `unchanged`, `not_reproduced`), `stop_reason`
(`fixed_point`, `attempt_limit`, `time_limit`, `baseline_not_reproduced`), trial and
worker counts, before/after feature/parameter/byte counts, removed IDs, original
and observed errors, build identities and elapsed search time. `fixed_point`
means only that the supported reductions found no further accepted candidate;
it is not a globally smallest model or proof of an identical root cause.

A `reduced` result includes `minimized_package` with a new failure ID, report path
and SHA-256. It uses the same portable two-file format and can be inspected or
replayed normally. Its context records the parent package and reduction report;
its source snapshot is null because the original remains in the parent. The
minimizer never rewrites the parent or saved documents, and its publication
excludes the parent from retention deletion. Normal later retention still applies.
Trial workspaces are removed, rejected candidates do not create packages, and
unchanged/non-reproduced searches create no new package.
