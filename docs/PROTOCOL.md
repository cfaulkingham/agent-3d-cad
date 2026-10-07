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

`workplane` requires `origin`, `normal`, `x_direction`; directions must be nonzero
and perpendicular. A profile is `{"type":"rectangle","width":20,"height":10}`,
`{"type":"circle","radius":5}`, or `{"type":"polygon","points":[[0,0],[10,0],[0,5]]}`
with 3–128 points. Rectangle begins at local (0,0); circle is centered there.
An axis object has `origin` and `direction`. A rotation has `origin`, `axis`,
`angle_deg`; rotation precedes translation. Pattern results are compound solids,
not assemblies or fused unions. Boolean fusion is explicit. Imported STEP is an
opaque solid feature, not recovered source design intent. Saved content makes
imports independent of their original file path.

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
| `cad_drawing` | `revision`, optional `drawing` recipe | Native PDF/SVG sheet, per-view DXF, aligned first/third-angle layouts, section hatching, measured linear/angular dimensions, explicit tolerances and saved recipe/manifest paths |
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
`remove_feature(id)`, and `set_output(feature_id)`. Validate the final batch graph.
A valid unchanged batch still creates a revision. Restore never rewinds HEAD.

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

Version 1 topology has `faces` and `edges` with evaluation-local string IDs,
surface/curve types, centers, bounds, area or length, and applicable directions.
Provenance records available OCCT shape history for loft sections and other
operations. Pattern history includes a zero-based `instance_index` for each copy.
Source and result entity IDs in this history are evaluation-local evidence, not
persistent design references; `history_truncated` reports the 10,000-entry cap.
A mesh contains `positions`, `triangles` (zero-based vertex indices),
`triangle_faces` (one B-rep face ID per triangle) and edge polylines `{id,points}`.
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
  includes evaluation/feature identity and summary; the complete editable model
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
  or null), optional `camera` and `prompt`. Camera has finite `yaw`, `pitch`,
  `zoom`, and two-element `pan`; angles are radians, pan is in viewport fractions.
  Prompt is bounded to 8,192 UTF-8 bytes. Selection resolution and a final locked
  HEAD/evaluation recheck precede publication. Null clears selection. Omitted
  camera/prompt fields retain prior values only within the same evaluation.

`cad_context` returns view/document identity, displayed or selected revision and
evaluation, current `head_revision`, `stale`, and `selection`. Validated geometry
and a selector, when available, appear in `resolved_selection`. Saved camera,
prompt and timestamp are optional. Old context is retained with `stale: true`
until replaced; never interpret it as a reference to the new revision.

The UI verifies the full transfer identity and rechecks sync before displaying
it. Same-document changes preserve the camera and clear old picks; switching
documents clears the old mesh while loading. Reopening restores a saved camera
and selection only when context still matches the current evaluation. A feature
tree displays source intent; it does not imply assembly or hide/isolate semantics.

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
preview/view/drawing. Drawing contracts, first-/third-angle and grid arrangements,
section hatching, angular references and degree results, explicit manufacturing
tolerances, view placement coordinates, references,
regeneration and export limits are specified in [DRAWINGS.md](DRAWINGS.md).
At most eight active jobs and four geometry workers are admitted
per workspace. Excess admission returns `queue_full`. Jobs persist after CLI/MCP
exit and can be inspected from a new process. List returns at most 1,000 records.

Budgets: timeout 1–300,000 ms; memory 128–4,096 MiB. Defaults 30 seconds/2,048 MiB.
Queue time counts toward the asynchronous deadline. Synchronous geometry uses
default budgets. Workers run via native process spawning, without model-supplied
code. Linux enforces address-space limits; Windows uses Job Objects; macOS checks
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
lock rather than a bare PID, and restores a successful committed mutation from its
receipt. A job interrupted before commit can be resubmitted with the same ID.
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
  jobs/<request_id>/state.json
  views/<view_id>/state.json        # workspace-scoped association and context
  views/<view_id>/evaluations/      # frozen mesh JSON for live transfer
  .workers/                         # bounded worker slots and temporary files
  .cache/<content-key>.json          # disposable exact geometry / view-set projections
```

HEAD defines visibility; snapshots beyond it are uncommitted candidates. Document
records preserve schema version, units, kernel version, feature IDs and revision.
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
job_interrupted, worker_failed, memory_limit, export_failed, storage_error, internal_error.
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
