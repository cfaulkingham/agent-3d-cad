# Native slicing with an installed OrcaSlicer

`cad_slice` plans and runs a fixed OrcaSlicer **2.4.2** CLI workflow for one
committed source solid. It preserves the editable source, reviewed inputs,
actual G-code, effective settings, execution evidence and a portable hash ledger.
Both actions use the same native Service from MCP and CLI and support cancellable
durable `cad_job` execution. This operation does not contact a printer.

## Explicit setup and plan

Install OrcaSlicer separately. Ordinary native CAD creation, exports and static
G-code review do not need it. Supply absolute regular non-symlink paths and the
actual raw SHA-256 of the native executable and each machine/process/filament
profile. Scripts and interpreter wrappers are rejected. The measured executable
identity is checked before and after both the version probe and slice invocation.
It is not a file-descriptor guarantee against adversarial executable replacement.

The current driver accepts self-contained JSON profiles with matching `type`
(`machine`, `process`, `filament`) and nonempty `name`. Nonempty `inherits` is
unsupported: resolve inheritance visibly before planning and retain original
profiles in the sourcing workflow. Machine semantics must explicitly say
`gcode_flavor: marlin` and `printer_technology: FFF`. Process and filament must
list the machine name in `compatible_printers`; compatibility expressions are
not executed. `post_process` may only be an array of empty strings. The driver
currently supports explicit **High Temp Plate** selection.

Call `cad_slice` with these closed fields:

```json
{
  "document_id": "part",
  "revision": 1,
  "feature_id": "body",
  "action": "plan",
  "options": {
    "backend": "orcaslicer",
    "version": "2.4.2",
    "executable": {"path": "/absolute/native/executable", "expected_sha256": "ACTUAL_RAW_HASH"},
    "profiles": {
      "machine": {"path": "/absolute/machine.json", "expected_sha256": "ACTUAL_RAW_HASH"},
      "process": {"path": "/absolute/process.json", "expected_sha256": "ACTUAL_RAW_HASH"},
      "filament": {"path": "/absolute/filament.json", "expected_sha256": "ACTUAL_RAW_HASH"}
    },
    "bed_type": "High Temp Plate",
    "review": {
      "firmware": "marlin",
      "machine": {"name": "Explicit fixture limits", "motion_bounds_mm": [[0,250],[-3,210],[0,210]]},
      "material": {"name": "Explicit assessed targets", "nozzle_temperature_c": [220,221], "bed_temperature_c": [60,61]},
      "initial": {"units": "mm", "xyz_mode": "absolute", "extrusion_mode": "absolute"}
    }
  }
}
```

Replace hash/path placeholders and illustrative review limits with verified
inputs. Omit unknown initial positions, extruder position and home endpoints;
do not guess a physical frame. Review limits are caller assumptions, separate
from native slicer settings. They include purge/park travel, not only the bed.

`feature_id` defaults to the document output. Assembly/compound plates and
multiple solids are unsupported; select one source part from a manufacturing
inventory. The plan returns `action`, document/revision/feature/model/kernel/build
identity, `directory`, `path`, raw `sha256`, `execution` and
`physical_print_started: false`. Its package contains a complete `source.json`,
native `input.stl`, three exact profile snapshots and `plan.json`. The plan
records a relative five-artifact ledger, exact-source summary and fixed argument
template. Planning does not run the external executable.

The fixed CLI arranges XY on the selected bed, preserves the source's Z
orientation, uses a private datadir, slices plate 0, writes effective settings
and requests plain G-code. There are no user/model-supplied flags, shell commands,
automatic profile selection or implicit scale/orientation changes. Orca's settings
separator requires workspace paths without semicolons.

## Run the reviewed plan

Call with only `document_id`, `revision`, `action: run`, the returned absolute
`plan_path` and its exact `expected_sha256`. The driver verifies the plan bytes,
source revision/model/build identity, complete saved record, profile snapshots,
relative input ledger and fixed execution template. Original caller profile
files can be removed after planning; the installed executable must remain.

The run regenerates its actual STL from the same committed native source.
Exact volume, area, center of mass and bounds must match the reviewed summary
within `1e-6 + 1e-9 * max(abs(values))`; solid/face/edge counts must match exactly.
Cache restoration can change triangulation or serialization bytes without
changing the exact solid. The complete original plan package remains under
`reviewed-plan/`, while `execution.json` identifies both planned and executed
mesh hashes and the numerical source check. No plan-supplied replacement mesh
is executed. This check does not claim triangle-for-triangle identity.

The native version probe must return `OrcaSlicer-2.4.2:`. Slicing must exit 0,
produce exactly one plain `output/plate_1.gcode`, preserve captured mesh/profile
bytes and write actual effective JSON settings. Effective machine/process/
filament IDs, bed, firmware, FFF semantics and disabled post-processing must
match the reviewed inputs. The full effective settings remain available for
further inspection; this identity check is not complete field-by-field profile
equivalence or firmware simulation.

Native static G-code inspection follows [GCODE_REVIEW.md](GCODE_REVIEW.md).
Failed or unknown findings remain failed or unknown in a completed slicing
package; they are not printer approval. Homing, leveling and unsupported macros
can leave the full swept envelope unknown even when commanded heaters pass.

The run returns `action`, source identity, `directory`, manifest `path`,
`gcode_path`, `gcode_sha256`, `gcode_bytes`, `plan_sha256`, native `report` and
`physical_print_started: false`. The package retains source, actual mesh, three
profiles, complete reviewed plan, version/slice stdout/stderr, slicer logs,
effective settings, review and execution evidence. Its manifest hashes every
published regular file except itself with portable relative paths. It records
`printer_approval: not_evaluated`.

## Containment and failure

The external native process occupies one of the existing four bounded worker
slots. A private native supervisor uses direct argv and separate bounded logs;
OCCT remains serial in independent geometry workers. POSIX process groups and
Windows Job Objects contain descendants. Cancellation, deadline, supervisor
death and coordinator death stop the contained processes. Publication occurs
only in the coordinator under the document lock after validation.

Job budgets retain 1–300,000 ms and 128–4,096 MiB ranges (defaults 30 seconds /
2,048 MiB). Aggregate staging is bounded to 128 MiB / 4,096 entries and logs to
2 MiB, sampled every 10 ms; sample overshoot is possible. Native profile inputs
are at most 1 MiB, executables at most 512 MiB and plain G-code at most 64 MiB.
macOS samples group physical footprint; Linux sums group RSS with shared pages
counted per process; Windows enforces per-process and aggregate job limits.
These are local trusted-tool controls, not a hostile executable sandbox.

Missing files, changed hashes, incompatible/unresolved profiles, version mismatch,
slicer exit failure, missing/contradictory output and resource exhaustion fail
explicitly and publish no slicing package. Source HEAD/history and existing
exports remain unchanged. Abrupt coordinator death can leave private staging
files; durable job recovery reports interrupted work instead of promoting them.
Completed job replay retains its result even after the caller removes the plan.

Native process-control fixtures test execution and containment, not slicing
accuracy. Actual Orca integration and independently checked package evidence are
recorded in HANDOFF. Other backends, bed types, inherited profiles, multi-solid
plates, automatic sourcing and intentional printer handoff remain pending.
