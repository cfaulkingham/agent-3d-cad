# Progressive native component viewing

Committed native live views negotiate `cad_viewer` sync with `progressive: true`.
They first receive an evaluation-qualified component manifest with **no triangle
or polyline tessellation**. Each unique leaf source is then tessellated separately
by a bounded native geometry worker, in its local millimeter coordinates. The
viewer can draw the first source and all of its occurrences before the remaining
source workers have run. This is separate geometry generation, not chunking an
already generated assembly mesh.

## Source and selection contract

`cad_query` has two additional kinds:

- `component_manifest`: returns the ordinary immutable evaluation identity,
  exact summary, source topology descriptors and `components`. Its source
  inventory includes feature IDs, local bounds, a SHA-256 of source topology,
  and three fixed display tolerance levels. Its occurrence inventory has a
  leaf path (null for a standalone part), source ID, row-major rigid transform,
  and arrays mapping local source face/edge IDs to that evaluation's global IDs.
- `component_mesh`: requires `evaluation_id`, `geometry_id` and integer `level`
  from 0 to 2. It returns `component_mesh` under the same evaluation identity.
  Only the requested source is tessellated. Current HEAD, native build, model
  hash, source membership and topology hash must match the manifest. A changed
  source fails with `stale_selection`.

Occurrence correspondence comes from OCCT's `ModifiedShape` transformation
history. Cached assemblies reapply placements to cached exact source B-reps to
rederive that correspondence; no assumption about source/copy enumeration is
used. Fingerprint numeric descriptors are canonicalized at 1e-9 resolution so
machine-precision B-rep serialization differences do not change correspondence;
exact source bytes and native build are independently qualified. The IDs remain local to one evaluation and are not stable design names.
Mesh levels and occurrences preserve the same exact global face/edge
references. Existing exact measurement recovery still verifies descriptors
uniquely against the current B-rep.

`cad_viewer` action `component` transports one source with `geometry_id`, `level`
and an opaque client `request_id`. It returns `component_state` loading, ready,
error or cancelled. Ready results use the existing bounded 128 KiB ASCII JSON
chunk envelope. The source is rechecked under the document/view locks before
returning each chunk. Repeating a request ID observes the same native job.
`cancel: true` cancels that request's native job, including after retargeting.

## Detail, budgets and failure behavior

The three linear tolerances are 1%, 0.2% and 0.04% of the source's largest bound,
with a 0.005 mm lower bound and 1e-9 mm canonicalization for cache round trips. Angular tolerances are 0.8, 0.4 and 0.2 radians.
The mesher clears prior triangulations before producing a requested level, so
coarsening creates a genuinely smaller mesh. Periodic-face display retries use
the same requested tolerances. None of these operations edits exact B-reps,
feature intent, committed history, manufacturing exports or model mass.

The browser projects each occurrence's bounds using the active camera, viewport
and exploded placement. Perspective projection uses the same lens and exact
visible-occurrence depth extent as the renderer. Projection and lens changes
reschedule visibility. It selects detail by projected linear error, refining
above 2 pixels and coarsening below 0.8 pixels. A short scheduling delay and this
hysteresis prevent continuous zoom jitter from starting redundant workers.
Visible missing sources get their coarse geometry before further refinements;
repeated occurrences reuse one local transfer and cache entry. Hidden/offscreen
sources are not newly requested; offscreen resident geometry may be coarsened
or evicted under memory pressure. If visible fine meshes consume the cache, the
scheduler downgrades lower-priority visible sources to coarse detail to admit
missing sources. Detail caps last until the camera, visibility or render settings
change, preventing a repeated refine/downgrade cycle. A refinement that cannot
fit retains its usable coarse geometry and reports the pressure cap. Meaningful
view changes retry memory refusals when hiding or moving parts makes room; genuine
geometry failures remain recorded.

Display data has explicit independent budgets:

| Data | Limit |
|---|---:|
| Unique source features | 256 |
| Nested leaf occurrences | 1,024 |
| Progressive exact topology descriptors, total | 100,000 |
| One source topology | 10,000 |
| One source/combined resident vertices and triangles | 200,000 each |
| Combined resident edge points | 200,000 |
| Conservatively charged local source cache | 32 MiB |
| Conservatively charged occurrence geometry staging | 64 MiB |
| One native transfer / manifest JSON | 64 MiB |

The source cache charge accounts for parsed JSON and geometry staging; the
occurrence charge includes CPU/GPU display representations. They are
application budgets, not a claim that browser process RSS equals these values.
The original exact build still obeys native worker time/memory/admission budgets.

Under the occurrence budget the scheduler prioritizes on-screen geometry and
reports how many occurrences it cannot display. Users can hide parts or change
the view to inspect them. It never reports missing geometry as fully loaded.
Unresolved visible source failures remain in status after other sources succeed,
and capture readiness rejects them. Capture also rejects visible occurrence
omissions caused by the combined staging limits; offscreen omissions do not
block capture. Explicit pre-admission `workspace_busy` and
`queue_full` refusals get a bounded retry with the same job ID and offset; unknown
errors and uncertain acknowledgements are not retried. Invalid data or failed
refinements retain the prior successful geometry. GPU
uploads are staged before deleting prior buffers, so allocation failure retains
the last frame. Retargeting, source changes and disposal cancel pending component
work and discard delayed replies. Source-wide summary bounds keep camera framing
stable throughout initial loading and every refinement.

## Scope and verification

The older monolithic `cad_query` mesh, standalone offline `cad_view`, draft
motion previews and external artifact reviews retain their existing contracts.
Progressive mode applies to saved native parts and assemblies, including
single-source parts. It does not yet replace the full exact assembly build or
topology-descriptor pass preceding the manifest; it removes aggregate
assembly tessellation and full-mesh transfer from time to first geometry.
Occurrences currently share transfer/cache geometry; the existing rendering
pipeline expands their resident buffers rather than using GPU instanced draws.

The `progressive` native suite covers exact transformed mapping, repeated
sources, independent curved levels and coarsening, cache restoration, a
1,024-leaf scene beyond the old global topology ceiling, native service/job/chunk
identity, cancellation, stale revisions and source immutability. The
`progressive_ui` suite covers first-source publication, cache reuse, adaptive
refinement/coarsening, stable identity and framing, hysteresis, hostile manifests,
late completion/cancellation, memory-pressure downgrade, perspective visibility,
visible omission refusal and retention of previous geometry.
Executed results and actual browser evidence are recorded in HANDOFF.

Independent JSON Schema validation against actual native and live results:

```sh
.deps/mcp-sdk-test/bin/python tests/progressive_schema_tests.py build/agent-3d-cad
```
