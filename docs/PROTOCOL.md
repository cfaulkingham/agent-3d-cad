# Model, CLI, and MCP protocol

Implemented contracts for the native preview. Runtime discovery (`tools` or MCP
`tools/list`) publishes input and output JSON Schemas. Model definitions live in
`model_definitions()`; service contracts live in `tool_definitions()`.

## Document and scalar values

```json
{"schema_version":1,"units":"mm","parameters":{"height":6},"features":[
  {"id":"base","type":"box","size":[20,10,{"parameter":"height"}]}
],"output":"base"}
```

Unknown fields are errors. Identifiers match `[A-Za-z][A-Za-z0-9_-]{0,63}`.
A document has at most 128 finite numeric parameters and 256 ordered features.
Dependencies name earlier features; IDs survive parameter edits. Every feature
is validated, including branches outside `output`. The output must be solid.
An intermediate numeric sketch is validated as a planar face, not as a solid.

A scalar is a finite number within ±1,000,000, a parameter reference, or a bounded
arithmetic tree. Dimensions use mm; directions are dimensionless; rotation uses
degrees; selection angular tolerance uses radians. Numeric literals and parameter
references take their argument's unit. Explicit expressions must declare the
matching unit; no strings, scripts or implicit conversion are evaluated.

```json
{"expression":{"op":"multiply","args":[{"parameter":"height"},2],"unit":"mm"}}
```

`add` and `subtract` use two operands in the result unit. `multiply` and `divide`
scale the first operand by a dimensionless second operand. Each tree permits
128 nodes and 16 levels, rejects division by zero, and bounds intermediate results.
Primitive dimensions and radii are at least 0.00001 mm. Vectors have three scalars.

## Features

Every feature requires `id` and `type`. Fields below are additional fields.

| Type | Required fields | Optional fields / meaning |
|---|---|---|
| `box` | `size` | `origin` defaults to `[0,0,0]`; minimum corner |
| `cylinder` | `radius`, `height` | `origin` defaults to base center `[0,0,0]`; +Z |
| `external_thread` | `major_diameter`, `pitch`, `length` | +Z; optional `origin`, `handedness` (`right` default or `left`) |
| `cut`, `fuse` | `left`, `right` | Earlier solid features |
| `fillet` | `input`, `radius`, `edges` | `edges` is `"all"` or a geometric selector |
| `sketch` | `workplane`, `profile` | Numeric profile; no constraint solver |
| `extrude` | `input`, `distance` | Sketch along workplane normal; signed distance |
| `revolve` | `input`, `axis`, `angle_deg` | Sketch; angle in (0,360] |
| `loft` | `sections` | 2–32 sketches; optional `ruled` boolean |
| `sweep` | `input`, `path` | Sketch swept along 2–64 world-coordinate points |
| `transform`, `instance` | `input` | Optional `translation`, `rotation`; solid reuse |
| `pattern` | `input`, `count`, `step` | 2–64 translated copies including original |
| `hole` | `input`, `origin`, `axis`, `radius`, `depth` | Cylinder cut along explicit direction |
| `import_step` | `content`, `sha256` | Embedded STEP text ≤512 KiB with matching SHA-256 |
| `assembly` | `parts` | 1–64 named instances of earlier solid features; optional acyclic rigid `mates` and source-keyed `bom` metadata |

`workplane` requires `origin`, `normal`, `x_direction`; directions must be nonzero
and perpendicular. A profile is `{"type":"rectangle","width":20,"height":10}`,
`{"type":"circle","radius":5}`, or `{"type":"polygon","points":[[0,0],[10,0],[0,5]]}`
with 3–128 points. Rectangle begins at local (0,0); circle is centered there.
An axis object has `origin` and `direction`. A rotation has `origin`, `axis`,
`angle_deg`; rotation precedes translation. Pattern results are compound solids,
not assemblies or fused unions. Boolean fusion is explicit. Imported STEP is an
opaque solid feature, not recovered source design intent. Saved content makes
imports independent of their original file path.

An assembly part is `{id,input,placement?}`. Placement has optional `translation`
and `rotation` with the transform convention above. A rigid mate is
`{id,type:"rigid",parent,child,parent_frame,child_frame,offset?,angle_deg?}`;
frames use the workplane fields in each source part's coordinates. Offset is in
the parent datum frame and rotation is about its +Z. An unmated root uses its
placement (identity when absent); a mated child must omit placement. Each child
has one parent at most, cycles fail, and parts/mates need not be ordered.
Assemblies permit 63 mates and the document permits 256 total assembly parts.
Nested assembly inputs and solid operations consuming assemblies are rejected;
edit source parts before assembling them. Full semantics and examples are in
[ASSEMBLIES.md](ASSEMBLIES.md).

A sweep starts at its sketch origin with the first path segment perpendicular to
the sketch plane. Invalid/self-intersecting profiles and failed sweeps fail
explicitly. STEP readers normalize source units to document millimeters.

### External threads

```json
{"id":"stud","type":"external_thread","major_diameter":20,"pitch":2.5,
 "length":21,"origin":[0,0,17],"handedness":"right"}
```

This primitive makes one continuous helical B-rep solid: a 60-degree ridge swept
along a cylindrical-surface helix and fused to its cylindrical root core. It is
not a stack of rings or a polygonal substitute. Its dimensional fields accept
the usual millimeter scalars, parameters, and bounded expressions. Length is
measured along +Z from the base-center `origin` (default `[0,0,0]`).

The nominal profile has crest width `pitch/8`, pitch diameter
`major_diameter - 0.649519053 × pitch`, and external minor diameter
`major_diameter - 1.226869322 × pitch`. The root is flat. M20 coarse uses a
2.5 mm pitch, giving nominal pitch/minor diameters 18.3762/16.9328 mm; compare
[TR Fastenings' metric coarse dimensions](https://www.trfastenings.com/knowledge-base/thread-geometries/metric-coarse-standard).
This is a nominal geometry feature, without manufacturing allowance, a rounded
rolled-thread root, or certification of a 6g fit class.

Ends are clipped at the specified length. The free +Z tip has a 45-degree lead
chamfer whose axial length is the lesser of the radial thread depth and
`length/4`. The base remains full profile for attachment to another solid.
An explicit `fuse` joins it to a knob or shaft; a small intentional overlap is
supported. The example above overlaps a head ending at Z=18 by 1 mm and leaves
20 mm exposed, including the lead chamfer.

Bounds are `pitch >= 0.1 mm`, `major_diameter <= 200 mm`,
`3 <= major_diameter/pitch <= 100`, and `1 <= length/pitch <= 16`.
These bound sweep complexity and keep detail above kernel tolerances. Invalid
inputs or a failed/invalid sweep return a feature-level error without publishing
a revision. Handedness reverses the helical advance; arbitrary thread standards,
multiple starts, fit classes, and internal threads are not implemented.

## Persistent selectors and evaluated picks

```json
{"type":"geometric","feature_id":"base","curve_kind":"line","expected_count":4,
 "direction":{"vector":[0,0,1],"tolerance":0.000001}}
```

The selector's feature must equal the fillet input. Curve kinds are `line`,
`circle`, `ellipse`, `hyperbola`, `parabola`, `bezier`, `bspline`, `offset`, `other`.
`expected_count` is mandatory (1–10,000). Optional predicates are:

- `direction`: nonzero `vector`, angular `tolerance` in radians; unoriented parallel lines only.
- `center`: `point` and distance `tolerance` in mm, against edge center of mass.
- `length`: `value` and `tolerance` in mm.

Tolerances are ≥1e-9; direction tolerance is ≤π/2. Scalar parameter references and
expressions are accepted. A missing or ambiguous match reports candidates and
feature context; it never selects an arbitrary member or silently drops a fillet.

Evaluated picks instead contain `document_id`, `revision`, `evaluation_id`,
`feature_id`, `kind` (`face`/`edge`), and `entity_id`. IDs such as `edge-1` are valid
only within that stored evaluation. They are never persistent design references.
Rebuilding the same revision creates a new evaluation identity. Stored evaluation
metadata remains resolvable after restart; resolving a pick requires current HEAD
to still equal its revision. Draft picks, missing evaluations and identity
mismatches fail explicitly. Geometry selectors can be suggested for unique edges;
faces return measurements but have no face-based editing operation yet.

## Tools

All document operations require `document_id`. Revisions are positive JSON-safe
integers. See runtime schemas for exact closed field definitions.

| Tool | Arguments beyond document_id | Result |
|---|---|---|
| `cad_create` | `model`, optional `request_id` | Committed record + summary, revision 1 |
| `cad_read` | optional `revision` (defaults HEAD) | Committed editable record; no geometry build |
| `cad_apply` | `expected_revision`, `operations`, optional `request_id` | New committed record + summary |
| `cad_restore` | `expected_revision`, `source_revision`, optional `request_id` | Historical intent rebuilt as a new revision |
| `cad_import` | `path`, optional `request_id` | New document with immutable embedded STEP feature |
| `cad_query` | `revision`, optional `kind`, `feature_id` | Summary, topology or mesh of that revision |
| `cad_export` | `revision`, `format` (`step`/`stl`) | Artifact path, bytes, units and identity |
| `cad_bom` | `revision`, optional assembly `feature_id` (defaults output) | Source-grouped BOM with instance quantities, JSON/CSV artifacts and manifest path |
| `cad_drawing` | `revision`, optional `drawing` recipe | Native PDF/SVG sheet, per-view DXF, aligned layouts, sections, measured dimensions, tolerances, optional BOM/balloons and saved recipe/manifest paths |
| `cad_view` | `revision`, optional `feature_id` | Offline HTML path, .view.json path, summary and evaluation identity |
| `cad_preview` | `expected_revision`, `operations`, optional `feature_id` | Draft view artifacts; no commit |
| `cad_resolve_selection` | Remaining evaluated pick fields | Measurements and available persistent selector |
| `cad_compare` | `from_revision`, `to_revision` | Parameter/feature changes and volume/area deltas |
| `cad_job` | Job action fields below; no document_id at top level | Durable job status/result |
| `cad_list` | No arguments | Up to 1,000 document IDs and committed HEAD revisions; `truncated` flag |
| `cad_open` | Optional `document_id`, `view_id` | Open an MCP App; `{view_id, document_id, resource_uri}` |
| `cad_show` | `document_id`, optional `view_id` | Retarget a workspace view without opening another app |
| `cad_context` | Optional `view_id` | Validated selection, camera, prompt and explicit stale state |
| `cad_viewer` | App-only actions below | Asynchronous view state, mesh chunks, context publication |

`cad_apply`/`cad_preview` accept 1–256 operations: `set_parameter(name,value)`,
`add_feature(feature)`, `replace_feature(id,feature)` (preserves ID),
`remove_feature(id)`, `set_output(feature_id)`,
`set_part_placement(assembly_id,part_id,placement)`,
`set_mate(assembly_id,mate)`, `remove_mate(assembly_id,mate_id)`,
`set_bom_item(assembly_id,item)`, and `remove_bom_item(assembly_id,input)`.
`set_mate` upserts a complete mate by its ID; removal requires an existing mate.
Placement replaces the complete root placement. Detaching a child and choosing
its placement can be combined in either order. Validate the final batch graph.
A valid unchanged batch still creates a revision. Restore never rewinds HEAD.

`set_bom_item` replaces or adds the complete metadata entry keyed by `item.input`;
`remove_bom_item` requires an existing entry. Assembly `bom` accepts at most 64
unique used inputs with optional unique `item_number` (1–999), `part_number`
(printable ASCII, 0–64), `description` (0–120) and `material` (0–64). Quantities
are derived from instances. Automatic numbers follow lexical input order while
skipping reserved explicit numbers; results sort by item number. `cad_bom` returns
revision identity, `bom: {assembly_id,items,total_quantity}`, `artifacts` and a
manifest `path`. Items contain `item_number`, `input`, `quantity`, `part_ids` and
supplied metadata. JSON/CSV exports preserve these rows and do not change HEAD.
See [ASSEMBLIES.md](ASSEMBLIES.md).

Drawing `bom: true` adds a sheet table and JSON/CSV sidecars. Optional `balloons`
specify `view`, `part_id`, source-local `anchor` (3 mm scalars) and projected
`label` (2 mm scalars); item numbers are derived from the BOM. Exact boundary
and visibility checks reject missing, occluded or ambiguous anchors. Section
balloons are unsupported. Responses include `bom` and resolved `balloons` with
projected `anchor_mm`/`label_mm`; PDF/SVG and DXF `BALLOONS` geometry share those
values. Full limits, fit rules and regeneration are in [DRAWINGS.md](DRAWINGS.md).

Mutation `request_id` values are bound to tool + canonical arguments excluding
the ID. An identical retry returns the original result even after HEAD advances;
a different payload under that ID returns `request_conflict`. Receipts reside in
committed immutable revisions, so interruption after publication cannot duplicate
an edit. IDs are scoped to a document for synchronous mutations and workspace-wide
for jobs. Without request_id, reread HEAD after an uncertain outcome.

### Geometry, views and previews

Summary reports `valid`, `units`, volume, area, center of mass, bounds, and unique
solid/face/edge counts. `cad_query.kind` defaults `summary`; `topology` returns
face/edge geometric descriptors; `mesh` includes topology and tessellation from
the **same** evaluated shape. `feature_id` scopes the shape (defaults to output)
and is always included in query results, including summaries.

Assembly summaries additionally include `assembly.parts` with `id`, source
`input`, a 16-number row-major world `transform`, `bounds_mm`, and `volume_mm3`,
plus `assembly.mates` with each rigid mate's `id`, `type`, `parent` and `child`.
Transforms multiply source point column vectors; translation is at indices 3,
7 and 11. Assembly measurements sum individual solids, including any overlap.

Version 1 topology has `faces` and `edges` with evaluation-local string IDs,
surface/curve types, centers, bounds, area or length, and applicable directions.
Provenance records available OCCT shape history for loft sections and other
operations. Pattern history includes a zero-based `instance_index` for each copy.
Assembly history and face/edge descriptors include the owning `part_id`; distinct
instances retain distinct topology even when coincident. Assembly edges omit
suggested fillet selectors; source features remain the geometry edit targets.
Source and result entity IDs in this history are evaluation-local evidence, not
persistent design references; `history_truncated` reports the 10,000-entry cap.
A mesh contains `positions`, `triangles` (zero-based vertex indices),
`triangle_faces` (one B-rep face ID per triangle) and edge polylines `{id,points}`.
Assembly mesh edges additionally carry `part_id`, and triangle part ownership
follows the corresponding face descriptor.
The mesh and edge mappings share the topology identity. The hard bounds are 10,000
combined faces/edges, 200,000 vertices, 200,000 triangles and 200,000 edge points.
Persisted evaluation metadata, worker results and artifacts are each bounded to
64 MiB. Document inputs remain limited to 1 MiB. Exceeding a bound is explicit
`limit_exceeded`, not truncation of geometry.

View artifacts need only a browser. Orbit/zoom, select faces/edges, copy or save a
pick reference, and load another `.view.json` in the same viewer to review an
updated model. No HTTP endpoint or network connection is needed. Draft views
carry a visible draft label and cannot resolve committed edit references.

The viewer depth-tests faces and edges, rejects indistinguishable overlapping
picks, validates loaded mesh mappings/bounds, and clips off-screen edges before
sampling. Render work is bounded (4 million pixels, 50 million triangle pixel
tests, 4 million edge samples); a model exceeding this requires a feature-scoped
view. Browser artifacts accept coordinates up to ±1e12 mm. These are review
limits, not changes to the exact saved geometry.

STEP exports exact solids. Binary STL uses 0.1 mm linear and 0.5 rad angular
meshing tolerance. Files are independent of service memory/source documents.
Assembly exports retain positioned solids as a compound; editable part IDs and
mate semantics remain in the saved JSON, without a promised exchange hierarchy.
Artifact destinations are service-generated beneath `exports`; arbitrary export
paths are not accepted. Re-export of a revision replaces its derived STEP/STL.

### Live MCP App views

`cad_open` declares `_meta.ui.resourceUri = ui://agent-3d-cad/viewer.html`.
The resource is self-contained HTML, CSS and JavaScript embedded in the native
executable, served by `resources/read` as `text/html;profile=mcp-app`. It has no
external resource or connection origins. Clipboard permission is requested;
denied clipboard access falls back to selectable request text. Browser code uses
the MCP Apps 2026-01-26 `postMessage` handshake with its parent host. Native MCP
stdio retains protocol version 2025-11-25. There is no HTTP listener.

Open/show/context default `view_id` to `main`. IDs are **workspace-scoped**, not
implicitly conversation-scoped: independent chats should choose distinct IDs.
Open without a document retains the previous association or creates an empty
view. Show can initialize a view, but does not cause a host to mount an app;
call open once when a UI is needed. Show on the same failed document explicitly
retries its evaluation. Ordinary committed edits need no additional show call.

The app polls `cad_viewer` with one of three closed actions:

- `sync`: `view_id`, optional `known_evaluation_id`. Returns `state` (`empty`,
  `loading`, `ready`, `error`), document/revision, and `changed`. Ready state also
  includes evaluation/feature identity, summary and `hidden_part_ids` (default
  `[]`), including when `changed` is false; the complete editable model
  is included when the caller's known evaluation differs. Sync starts or observes
  a durable mesh job and does not wait for kernel completion. Publication checks
  both the view generation and current HEAD under locks. An obsolete result never
  replaces the displayed current revision.
- `mesh`: `view_id`, `evaluation_id`, optional integer `offset` (default zero).
  Returns `{data, offset, next_offset, total_bytes}`. `data` contains at most
  128 KiB of an ASCII-escaped JSON evaluation (64 MiB maximum). Offsets count
  bytes, which equal JavaScript string offsets for this encoding. Concatenate
  chunks before parsing; `next_offset: null` means complete. Every chunk checks
  displayed evaluation and HEAD. A changed view/revision fails explicitly.
- `context`: `view_id`, `evaluation_id`, `selection` (a complete evaluated pick
  or null), optional `camera`, `prompt`, and `hidden_part_ids`. Camera has finite `yaw`, `pitch`,
  `zoom`, and two-element `pan`; angles are radians, pan is in viewport fractions.
  Prompt is bounded to 8,192 UTF-8 bytes. Selection resolution and a final locked
  HEAD/evaluation recheck precede publication. Null clears selection. Omitted
  camera/prompt fields retain prior values only within the same evaluation.

`hidden_part_ids` is a view-level array of at most 64 unique IDs from the
currently displayed assembly. `[]` shows all parts; omitting the field retains
the current view state. Unknown IDs, duplicates, non-arrays, and a pick whose
resolved `geometry.part_id` is hidden fail with `invalid_argument`. Send null
selection when hiding the selected part. Visibility and selection are checked
together under the final document/view locks; a rejected or stale request
changes neither. Hiding affects presentation and picking, with the complete
mesh, saved model, exact measurements, exports and BOM unchanged.

Hidden IDs persist across service restart and same-document revisions. When a
new display publishes, IDs for removed parts are pruned; switching to a solid
output clears the array. Retargeting to a different document clears visibility,
and returning to the earlier document does not restore its former mask. A same-
document `cad_show` retry retains the array. The array is stored separately from
the evaluation-qualified selection context.

`cad_context` returns view/document identity, displayed or selected revision and
evaluation, current `head_revision`, `stale`, `selection`, and `hidden_part_ids`.
The hidden IDs describe current displayed presentation even if a saved pick is
stale. Validated geometry
and a selector, when available, appear in `resolved_selection`. Saved camera,
prompt and timestamp are optional. Old context is retained with `stale: true`
until replaced; never interpret it as a reference to the new revision.

The UI verifies the full transfer identity and rechecks sync before displaying
it. Same-document changes preserve the camera and clear old picks; switching
documents clears the old mesh while loading. Reopening restores a saved camera
and selection only when context still matches the current evaluation. Assembly
part controls hide individual instances, isolate one instance, or show all.
Isolating uses the same hidden-ID array for the other displayed parts.

Read-only app polling treats `workspace_busy`, `queue_full` and a
`stale_selection` during transfer as temporary waits. It disables old selections
and retries a complete, revalidated transfer on the next poll while retaining
the last rendered solid and camera. Other failures remain visible. This recovery
does not retry context publication, modeling mutations or message delivery.

Quick Edit is an explicit user action. It publishes validated context and sends
`ui/message` only when the host advertises messaging. Optional PNG capture requires
image-message support and is limited to 2 MiB. A successful `ui/message`
acknowledgment may mean the host prepared a chat composer draft; the UI directs
the user to press Send there. It does not claim automatic posting or editing.
Uncertain delivery is never retried
automatically. Hosts without messaging use Copy request. Selection changes use
`ui/update-model-context` when supported; that optional acknowledgment does not
block revision polling. The app never directly calls modeling mutations.

The bridge accepts messages only from its parent window, pins the responding
origin, bounds pending requests with timeouts, and disposes them on teardown.
WebGL2/WebGL1 rendering and CPU ray picking use the exact evaluation mapping,
depth-tested occlusion and explicit overlap ambiguity. GPU work is bounded to
four million framebuffer pixels; pick traversal has a two-million-work budget.
Native geometry/payload limits continue to apply. Tests exercise math and mocked
GPU lifecycle. Real Codex-host rendering, a human edge pick, selective edit,
same-view refresh, camera retention and stale-pick clearing were observed on
macOS arm64; host message routing used a user-submitted composer draft. Other
hosts and native platforms retain their own acceptance gates.

### Jobs

```json
{"action":"submit","request_id":"plate_edit_2","tool":"cad_apply",
 "arguments":{"document_id":"plate","expected_revision":1,"operations":[
   {"op":"set_parameter","name":"thickness","value":8}]},
 "budget":{"timeout_ms":30000,"memory_mb":2048}}
```

Use `{"action":"get","job_id":"plate_edit_2"}`, `cancel` with the same job_id,
or `{"action":"list"}`. Submit supports create/apply/restore/import/query/export/
preview/view/drawing/bom. Drawing contracts, first-/third-angle and grid arrangements,
section hatching, angular references and degree results, explicit manufacturing
tolerances, view placement coordinates, references,
regeneration and export limits are specified in [DRAWINGS.md](DRAWINGS.md).
At most eight active jobs and four geometry workers are admitted
per workspace. Excess admission returns `queue_full`. Jobs persist after CLI/MCP
exit and can be inspected from a new process. List returns at most 1,000 records.
Submit validates `arguments` with the same closed field sets, identifiers and
revision numbers as a direct call, so malformed input fails immediately with
`invalid_argument` and records no job. `get`/`cancel` of an unknown job ID return
`not_found`.

Retention: terminal jobs (`succeeded`, `failed`, `cancelled`, `interrupted`,
or damaged records) are collected during submit admission once they were last
updated more than **7 days** ago, or when they fall beyond the newest **256**
terminal records. Queued, running and cancelling jobs, records whose coordinator
still holds its ownership lock, and the request ID being submitted are never
collected. A collected ID returns `not_found`; resubmitting a mutation with the
same request_id still replays its committed receipt rather than editing twice.

A damaged, unreadable or symlinked job record never makes `list` or `submit`
fail. It reads as a `failed` job with error `job_record_corrupt`, and counts
toward admission only while a live coordinator still owns it. Use a new
request_id instead of a damaged one.

Budgets: timeout 1–300,000 ms; memory 128–4,096 MiB. Defaults 30 seconds/2,048 MiB.
Queue time counts toward the asynchronous deadline. Synchronous geometry uses
default budgets. Workers run via native process spawning, without model-supplied
code. Coordinators and workers execute the running service image itself, whatever
its file name (on Linux, `/proc/self/exe` after an in-place upgrade). A detached
coordinator holds no caller pipe; Windows children inherit only their NUL streams. Linux enforces address-space limits; Windows uses Job Objects; macOS checks
physical footprint every 10 ms (there can be sampling overshoot). Worker-local
watchdogs also bound work if a coordinator exits. Geometry is serial in each
worker; no OCCT object is shared between workers.

Repeated operations automatically reuse a disposable `.cache` inside the
workspace. Exact geometry is keyed by complete model contents, native build/SDK
fingerprints, kernel and cache-format versions. Drawing projections additionally
key the ordered views, hidden lines, section planes and hatch extraction. Changing
labels, dimensions, tolerances, layout or export formats rerenders from those
projections. Changing model contents invalidates both. Evaluation identities and
revision checks are always fresh; cached data never serves as an editable source.

Entries carry SHA-256 checksums. Geometry snapshots contain every feature's exact
B-rep and provenance, with shapes validated when restored. Snapshot B-rep data
is capped at 32 MiB and encoded cache entries at 64 MiB. The shared cache keeps at
most 128 entries / 256 MiB, evicting oldest publications under a native lock.
Busy, missing, unwritable, symlinked, corrupt and oversize cache entries are
skipped and geometry is rebuilt as needed. The cache may be removed while the
service is stopped; documents and exports are unaffected. The first projection
of a complex threaded part can still require an explicit long-running job budget.
No additional MCP tools, public response fields, or cache-management API are added.

States are `queued`, `running`, `cancelling`, `succeeded`, `failed`, `cancelled`,
`interrupted`; progress is coarse phase progress rather than a predicted percent.
Terminal success contains `result`; failures contain structured `error`. Cancel
and mutation publication share the document lock: a commit already published is
reported as success; otherwise cancellation prevents publication. Reads remain
available while geometry runs. Publication rechecks expected_revision under the
writer lock, so concurrent builds cannot silently overwrite one another.

Duplicate submits with identical tool/arguments/budget return the same job.
A changed payload/budget with the same ID returns `request_conflict`. Recovery
checks abandoned nonterminal jobs after five seconds, uses a coordinator ownership
lock rather than a bare PID, and reports success when the job's `result.json` is
present and belongs to the job, or restores a successful committed mutation from
its receipt. A job interrupted before commit can be resubmitted with the same ID.
The coordinator publishes `result.json` before the state that reports success.
If any later step fails, it logs the failure to stderr and persists a `failed`
state with the error, unless a durable result already exists for recovery.
Failed/cancelled jobs remain terminal; use a new request_id for a deliberate retry.
This application job API does not advertise the MCP Tasks extension.

## Persistence and errors

```text
workspace/
  .lock
  .locks/<document_id>.lock
  documents/<document_id>/HEAD.json
  documents/<document_id>/revisions/<revision>.json
  evaluations/<evaluation_id>.json
  exports/<document>-r<revision>.step
  exports/<document>-<evaluation>.html
  exports/<document>-<evaluation>.view.json
  jobs/.lock                        # job admission
  jobs/<request_id>/state.json      # small job record: state, error, result digest
  jobs/<request_id>/request.json    # immutable submitted tool arguments
  jobs/<request_id>/result.json     # result (≤64 MiB), published before success
  jobs/<request_id>/.lock           # coordinator ownership
  jobs/<request_id>/cancel          # cancellation request
  views/<view_id>/state.json        # workspace-scoped association and context
  views/<view_id>/evaluations/      # frozen mesh JSON for live transfer
  .workers/                         # bounded worker slots and temporary files
  .cache/<content-key>.json          # disposable exact geometry / view-set projections
```

HEAD defines visibility; snapshots beyond it are uncommitted candidates. Document
records preserve schema version, units, kernel version, feature IDs and revision.
Job `state.json` (`schema_version: 2`) stays small: identity, tool, budget, state,
progress, timestamps, structured error and the SHA-256/size of `result.json`.
Admission and list read only these records, never results. `request.json` and
`result.json` repeat the job ID and request fingerprint; a result is trusted only
when it matches them and the recorded digest. Job directories written before this
layout (arguments and result embedded in `state.json`) remain readable, and a
terminal one is rewritten in the current layout the first time it is read.
POSIX writes fsync then rename then fsync the parent; Windows uses file flush and
write-through replacement. Atomic publication has an uncertain outcome if the OS
reports a durability error after rename; idempotent receipts let callers reconcile.

Use a trusted local workspace; managed-path symlinks are rejected. This is not a
hostile-user or network-filesystem sandbox. No HTTP/remote multi-tenant service is
exposed. Abrupt exit may leave temporary files; these are never documents.

Domain errors are `{"code":"...","message":"...","details":{...}}`. Important
codes include invalid_json, invalid_argument, invalid_model, limit_exceeded,
unsupported_schema, unsupported_feature, invalid_shape, kernel_failure,
selection_missing, selection_ambiguous, selection_count_mismatch, stale_selection,
draft_selection, already_exists, not_found, revision_conflict, request_conflict,
kernel_mismatch, workspace_busy, queue_full, job_timeout, job_cancelled,
job_interrupted, job_record_corrupt, worker_failed, memory_limit, export_failed,
storage_error, internal_error.
Modeling errors identify their failed feature. Missing/ambiguous selectors include
candidate descriptors for repair. A failure never silently changes modeling intent.

## MCP stdio

Baseline `2025-11-25`; initialize then notifications/initialized. `ping`, tools/list,
and tools/call are supported. Input is newline-delimited UTF-8 JSON, ≤1 MiB and
64 nesting levels; no JSON-RPC batches. String/integer request IDs are preserved.
Notifications have no reply, and tools/call notifications never execute tools.
Malformed messages do not end the stream; a final complete frame at EOF is accepted.
All stdout lines are protocol JSON; diagnostics use stderr.

Tool replies contain JSON text and structuredContent; domain failures set isError.
The server advertises the MCP Apps extension and resources with no subscriptions.
`resources/list` exposes the single viewer resource; `resources/read` accepts
only its exact URI. Tools include the same UI metadata in CLI discovery.
`cad_viewer` has `_meta.ui.visibility: ["app"]`; the host filters model visibility
and app calls. The service still accepts the tool through CLI/stdio for tests and
other adapters. HTTP, sampling, streaming progress and MCP Tasks are not exposed.
Use cad_job for asynchronous application work.
