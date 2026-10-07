# Implementation handoff

Updated: 2026-10-07. **M0–M3, M4 live viewing, M5 drawings, and M6 assemblies are native previews.**
Actual Codex-host rendering and select–edit–refresh are demonstrated on macOS
arm64. Earlier preview sources passed all five macOS/Linux/Windows CI lanes and
independent Arch Linux x86_64 validation. Each increment below records its own
validation scope. Public release preparation remains separate.

## Current increment — exact HLR root streaming and face-grid reuse

Implemented `agentcad-hlr-midpoint-v2` in the pinned OCCT 8.0.1 recipe:

- Existential midpoint checks and `Compare` can stop solving ray/surface roots
  after the first root passes the existing depth, periodic-UV and trimmed-face
  classification. Rejected roots continue to later candidates. The sorted
  brackets, parameter bounds, exact solver, point construction, duplicate
  suppression and tolerances are the same as v1. Midpoints retain their dense
  first attempt and original-grid fallback; `Compare` retains its original grid.
- A private owner retains both the dense and original polyhedra for the currently
  loaded face. Alternating queries select the same sampling dimensions instead of
  repeatedly evaluating the surface and rebuilding identical grids. At most two
  grids are retained; `Load` and destruction release both. No global cache, copied
  polyhedra or new geometry approximation is introduced.
- Count-dependent classification retains the complete original intersection
  path. Existing native symbols and object layouts remain unchanged; the additive
  `PerformUntil` method supplies only an existence result, not a complete root
  inventory. Model documents and transport/tool contracts remain unchanged.
- The checked five-file script accepts upstream sources, upgrades exact known v1
  sources, reapplies safely to v2 and rejects unknown input. SDK and bundle
  manifests now identify v2 and verify the selected TKHLR hash. Complete modified
  source, upstream attribution and the reproducible patch accompany bundles.
- Direct native regressions compare streaming and complete root inventories,
  all-rejected queries, first/later accepted roots, empty queries, repeated
  grid/face changes against independently rebuilt grids, analytic supports and
  exact source-surface residuals.
  Older SDKs explicitly report that these additive-method checks are unavailable.

Executed on macOS arm64 with OpenCascade 8.0.1:

- Rebuilt TKHLR in the existing pinned Release dependency tree and staged its
  matching native headers/notices alongside the SDK's unchanged dependencies.
  The staged library uses sibling loader resolution. No global installation.
- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **28/28 passed in
  44.95 seconds**, including **41,074 performance geometry/root checks** in
  **14.40 seconds**.
- Compared exact projected B-rep curves against the frozen v1 patch on **29
  views**, covering handedness, pitch/length, rotated parts, overlapping assemblies,
  fused geometry and a 100x scale case. **255,020 bidirectional samples** matched
  both visible and raw hidden curves within 2e-5 mm; the largest sampled deviation
  was **8.388e-08 mm**. This is finite numerical evidence, not a continuous-domain
  proof. The three previously timed-out long axial diagnostic baselines were not
  rerun in this matrix. Report: `build/drawing-perf-next/final-matrix-report.json`.
- A fresh native full M20 knob, default four-view drawing completed cold in
  **189.782 seconds**, versus **196.185 seconds** for the frozen v1 service/SDK:
  a **3.26% reduction** in this local paired case. Both used empty caches, the
  unchanged source, 300000 ms bounded job budgets and no concurrent geometry/build
  work. This is a modest measured improvement; complex first drawings still take
  minutes. Command: `python3 -u tests/cache_benchmark.py
  build-app-protocol/agent-3d-cad build/drawing-perf-next/native-final
  --views standard --cold-only --timeout-ms 300000`.
- All **40,962** native projection comparisons matched with zero deviation, and
  all six cold PDF/SVG/DXF files were byte-identical to v1. Three cached redraws
  took **0.153, 0.151 and 0.154 seconds** (median **0.153**), with identical files.
  A3 first-angle restyling with dimensions took **0.150 seconds**, reused unchanged
  cache files and preserved the revision-1 source. Report:
  `build/drawing-perf-next/final-paired-report.json`; native result:
  `build/drawing-perf-next/native-final/cache-benchmark-ilisjwpn/report.json`.
- Fresh, CRLF and known-v1 source application, repeated v2 application and
  unexpected-source rejection passed. Regenerating SDK notices removed the obsolete
  v1 binary manifest and retained the matching v2 manifest. The dependency recipe
  configured.
- Native `bundle-check` passed relocation and empty-PATH create/edit/reopen,
  geometry, drawings, assemblies/BOM/balloons, MCP and embedded app-resource smoke.
  Pairing the previous v1 TKHLR with v2 notices was rejected before publication.
  Logs: `build/drawing-perf-next/dual-grid-ctest.log`,
  `dual-grid-bundle.log`, `final-mismatch.log` and
  `final-patch-checks/report.json`.

Limits: exact HLR retains its existing numerical limitations; count-dependent
ray solving still exhausts the full root inventory. These sources and the modified
SDK have local macOS evidence, not a new Windows/Linux certification. Existing SDKs
need the updated dependency recipe to use streaming. No remote push or release.

Next: profile remaining exact root solving and trim classification before another
optimization; validate the updated SDK on the other native lanes. Full-count ray
queries were inexpensive in the top-view probe. Repeated-ray memoization and
spatial-index reuse did not provide a worthwhile benefit and are not included.

## Previous increment — assembly visibility and cold drawing optimization

Implemented live-view presentation controls:

- Assembly parts have Hide/Show and Isolate controls in the inspector, with
  Show all and an all-hidden recovery action. Controls remain available in narrow
  layouts. Rendering, picking, highlights and captures exclude hidden parts;
  Fit frames the visible geometry without changing the camera when hiding a part.
- `hidden_part_ids` persists per `view_id`, is returned by `cad_context` and ready
  `cad_viewer` sync responses, and is included in agent request context. The native
  service validates part IDs and the current displayed evaluation under the final
  view/document locks. Hiding a selected part clears its pick; attempts to select
  hidden geometry fail explicitly.
- Same-document revisions retain surviving part IDs and prune removed instances.
  Changing documents resets visibility. Reloading restores the current mask even
  when an older saved selection is stale. The controller protects local visibility
  changes from older in-flight sync responses, surfaces save failures and allows an
  explicit retry. Presentation does not edit the model, revision, BOM or exports.
- The renderer retains the immutable native mesh and ownership IDs, rebuilding
  visible buffers and its picking tree only when needed. Offline `cad_view`
  artifacts retain their existing behavior. Separate clients sharing a `view_id`
  use the last explicitly published mask; independent chats should use distinct IDs.

Implemented cold-projection and output improvements:

- CPU sampling located the M20 knob bottleneck in exact hidden-line ray
  classification against long thread BSpline faces. The pinned OCCT 8.0.1 recipe
  now applies `agentcad-hlr-midpoint-v1`: two midpoint checks that discard root
  counts use bounded denser initial brackets and stop at the first exact qualified
  occluder. An unsuccessful attempt repeats the original grid; every count-dependent
  caller retains the original path. Source geometry, topology, tolerances and
  existing public symbols/object layouts remain unchanged.
- The five-file patch verifies upstream and resulting hashes, accepts CRLF source,
  rejects unknown input and is idempotent. Dependency stamps track patch/notice
  scripts. Installed notices retain attribution, the patch and all five complete
  modified files. Portable packaging verifies its selected TKHLR hash against the
  SDK modification manifest before claiming the patch in provenance. The cache
  identity already includes SDK binaries, so disposable caches invalidate safely.
- Drawing geometry now paints hidden curves before visible curves in PDF/SVG and
  emits DXF entities in the same order. Coincident hidden dashes cannot overlay
  visible outlines in the native sheet. Center marks and other annotations follow
  geometry; cached projections stay unchanged. Curved-geometry regressions cover
  all three formats and `hidden_lines: false`.
- `tests/cache_benchmark.py` supports `--cold-only`, explicit `--timeout-ms`,
  durable failure timing and executable/source hashes. Resume rejects a changed
  executable or view scenario.

Executed on macOS arm64 / OpenCascade 8.0.1 with the modified SDK:

- `cmake --build build-app-protocol --parallel 4` passed, followed by
  `ctest --test-dir build-app-protocol --output-on-failure`: **28/28 passed in
  50.01 seconds**. The new performance geometry suite passed **39,991 checks**
  in 15.22 seconds, covering left-handed/fine-pitch threads, finite projections,
  source/topology nonmutation, restored snapshots, exact sub-chord-tolerance
  visibility and independent rejection of an occluded balloon anchor.
- Native live context **110 checks**, embedded app protocol **93**, UI state
  **103**, real stdio MCP/controller loop **27**, WebGL renderer/native assembly
  mesh **47**, and drawing **1,947** checks passed within that suite.
  **291 schema checks across 19 tools** and **507 official MCP SDK 2.3.0 checks**
  passed (poll counts vary).
- Native relocation `bundle-check` passed with empty PATH, including visibility
  controls, assemblies, BOMs, balloons and verified modification provenance.
  Pairing the original unmodified TKHLR with patched notices was rejected before
  publication. Fresh/CRLF patch application, repeat application, unexpected-source
  rejection and dependency recipe configuration also passed.
- A temporary loopback MCP host served the embedded app from the actual native
  executable. Eight browser interactions verified hiding the cover, isolating and
  fitting a spacer, selecting its visible geometry, clearing a hidden pick,
  all-hidden recovery, Show all, and persisted visibility/camera after reload.
  The document remained at revision 1 with identical source. Evidence and a
  screenshot are in `build/hide-isolate-demo/`; the server was stopped. This is
  browser/native-service evidence, not a new installed Codex-host test.
- The unchanged full M20 knob, default four-view orthographic drawing completed
  cold in **202.674 seconds**, versus the original build exhausting its budget at
  **240.012 seconds**. The original result is a timeout, not its completion time.
  The final run used the integrated service and selected SDK with no loader
  override, fresh caches and no concurrent geometry/build tests. Command:
  `python3 -u tests/cache_benchmark.py build-app-protocol/agent-3d-cad
  build/drawing-perf --views standard --cold-only --timeout-ms 240000`.
- Three cached redraws took **0.157, 0.158 and 0.156 seconds** (median **0.157**)
  with byte-identical PDF/SVG/DXF files. A3 first-angle restyling with dimensions
  took **0.152 seconds** and reused unchanged cache files. **131 native/cache,
  ezdxf/XML and history checks** plus **20 independent pypdf checks** passed.
  All eight DXF audits had zero errors or fixes; projected dimensions matched the
  source. Poppler inspection confirmed complete four-view A4/A3 sheets and legible
  dimensions. A thread-length edit changed geometry and retained revision 1 source.
- Evidence: `build/drawing-perf/cache-benchmark-jljxl88z/report.json` and its
  artifacts/previews, `build/drawing-perf/final-*.log`, plus the original timeout
  in `build/drawing-perf/cache-benchmark-koctfa5m/report.json`. The executable hash
  remained identical after final packaging regeneration. `git diff --check`
  passed. No global installation, remote push or release.

Additional numerical validation compared original and modified exact projected
BRep curves on threads of different hands/pitches/lengths, rotated parts, fused
geometry, overlapping assemblies and a 100x scaled stress case. Of **29 completed
comparisons**, 26 matched visible and uncovered-hidden geometry within 2e-5 mm;
three long-D20 views removed baseline-visible intervals. Source-solid ray audits
found only physically blocked samples in those intervals (right front: 20 samples
across four intervals after scanning all 152 edges; left isometric: five samples
after scanning all 70 edges; left front: a sampled subset of an earlier 116/116
blocked-point audit). No newly visible sample exceeded that tolerance. Three
long top-view baselines timed out at their 35-second diagnostic limit and remain
uncompared. These are finite numerical checks, not continuous-domain proofs.
Details are in `build/drawing-perf/axial-variants/FINAL-EXISTENTIAL.md`.

Rejected faster experiments included splitting surfaces and changing grids for
all classifiers: they exposed occluded edges or changed count-dependent results.
They are not included. The accepted patch retains OCCT's numerical limitations;
first drawings of complex threads still take minutes and need an adequate bounded
`cad_job` budget. Hide/isolate is presentation-only in the live viewer; exported
models, drawings and BOMs still include every part. The modified SDK and these
sources have local macOS evidence, not a new Windows/Linux certification. Existing
unmodified developer SDKs must be rebuilt with the supplied dependency recipe for
the optimization, new geometry regressions and verified portable packaging.

Next: further cold HLR improvements only with exact visibility evidence;
nested/linked assemblies, additional mate types, or multi-sheet/automatic
annotation layout as separately scoped workflows.

## Previous increment — assembly BOMs and part balloons

Implemented through the shared Service, native CLI/MCP and drawing worker:

- Optional assembly `bom` metadata is keyed by source `input`, with explicit
  item numbers, part numbers, descriptions and materials. Rows group repeated
  source instances and count instances rather than solids. Deterministic automatic
  numbering sorts inputs and skips explicit reservations; fixed item numbers
  survive membership changes. `set_bom_item` and `remove_bom_item` use the existing
  atomic revision path, including final-state validation for number swaps.
- `cad_bom` exports committed or historical assembly inventory as JSON/CSV and a
  manifest without rebuilding geometry. Optional `feature_id` selects a named
  assembly. CSV preserves metadata with quoted fields, doubled quotes and CRLF.
  Publication uses a fresh directory, cancellation checks and a final document
  lock. Durable `cad_job` also accepts this tool.
- Drawing `bom: true` adds a wrapped table to PDF/SVG and independent `bom.json`
  and `bom.csv` artifacts, including DXF-only requests. Balloons derive their item
  numbers from the same inventory; callers specify a part, a source-local surface
  point and projected label coordinates. They follow placement, rigid mates and
  view explosion. The kernel snaps within 1e-5 mm of an exact boundary and rejects
  missing, ambiguous or occluded attachment points. These checks do not mutate
  assembled geometry or the saved revision.
- PDF/SVG and 1:1 mm DXF share numbered circles, arrows and leaders; DXF puts these
  on `BALLOONS`. Circles clear geometry bounds, other circles and dimension
  strokes/text, and leaders cannot cross another balloon circle. Layout includes
  annotations without altering measured geometry extents. Invalid or oversized
  layouts fail explicitly instead of clipping or dropping rows/balloons.
- Saved recipes retain parameterized anchors and labels. Anchor/explosion changes
  invalidate projection cache entries; moving labels rerenders cached projections.
  Metadata changes retain complete-model cache invalidation. Old recipes, source
  revisions and artifacts remain immutable. Added an assembled/exploded example
  and updated protocol, assembly, drawing and bundled agent guidance.

Executed locally on macOS arm64 / OpenCascade 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **27/27 passed in
  28.57 seconds**. New suites passed **68 BOM checks**, **88 balloon kernel checks**
  and **58 BOM drawing checks**. Coverage includes grouping, numbering, quoted
  metadata, semantic edits/rollback, historical exports, exact boundary attachment,
  placement/explosion, hidden/ambiguous anchors, annotation collisions, cache
  invalidation/reuse, native formats and asynchronous regeneration.
- **270 schema checks across 19 tools** and **401 official MCP SDK 2.3.0 checks**
  passed (polling can affect counts), including asynchronous BOM export and
  persisted job results across process restart.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  native relocation with empty PATH, including BOM JSON/CSV, quoted metadata edits,
  history, and the sheet table/balloons alongside prior smoke coverage.
- Generated the four-instance plate/spacer example with demonstration part numbers
  and descriptions. Poppler visual inspection confirms the two-row table and four
  correctly numbered balloons. Independent pypdf, ezdxf, XML and CSV readers passed
  **47 checks**: a single A3 page, table metadata, artifact byte counts, matching
  inventories, DXF units/radii/centers/numbers and zero DXF audit errors or fixes.
  Evidence and sample artifacts are under `build/bom-demo/`.
- `git diff --check` passed. No global installation, remote push or release.

Current limits: flat same-document BOMs; at most 64 source rows and 64 balloons,
item numbers 1–999, printable ASCII metadata, one balloon per part per view, and
no section balloons. Label placement is explicit and conservatively clears the
whole projected geometry bounding rectangle. Tables must fit one sheet, including
DXF-only requests; table pagination, automatic balloon routing, hierarchical BOMs,
and part-specific quantity overrides are not implemented. DXFs contain balloons;
the complete table is in the sheet and JSON/CSV sidecars. These sources have local
macOS evidence, not a new multi-platform certification.

Next: nested/linked assemblies, additional mate types, per-part hide/isolate,
or multi-sheet/automatic annotation layout as explicit follow-up workflows.

## Previous increment — editable assemblies and exploded drawings

Implemented through the existing shared native Service, CLI/MCP and bounded
workers, without adding a separate transport or runtime:

- An `assembly` feature contains 1–64 named part instances of earlier editable
  solid features. Repeated inputs share source intent while placed copies retain
  separate exact solids and topology, including coincident instances. Rotation
  about an explicit axis precedes translation. Parts remain a compound; touching
  or overlapping geometry is never silently fused. Aggregate volume includes
  every part, including overlap.
- Named `rigid` mates align explicit source-coordinate datum frames with an
  optional parent-frame offset and rotation. The parent forest resolves in any
  serialization order, fixing all six relative degrees of freedom. Missing
  references, cycles, multiple incoming mates, invalid frames and conflicting
  child placements fail explicitly. Roots use their saved placement or identity.
  A model permits at most 256 total assembly parts and 63 mates per assembly.
- `set_part_placement`, `set_mate` (upsert) and `remove_mate` are atomic semantic
  edits. Existing parameter/feature edits update source parts and datum values;
  previews, revisions, restore, compare, jobs and expected-revision checks use
  the same existing transaction path. Detaching a child returns it to identity
  unless the same batch supplies a placement. Explicit placements are never
  silently discarded when adding a mate.
- Summaries include part inventory, source feature IDs, row-major world matrices,
  bounds, volume and mate relationships. Topology/provenance and mesh edges carry
  part ownership; triangles map through their face IDs. Assembly picks retain
  evaluation/revision lifetime and expose their owning part. They do not propose
  fillet selectors in the wrong coordinate system. The live viewer lists part
  sources and mate parents and displays the selected part.
- Per-view `explode` offsets translate named parts in world mm after mate
  placement, including section views. Parameterized recipes survive regeneration.
  PDF/SVG/DXF views identify exploded geometry, whose measured dimensions include
  the offsets. Drawing generation does not mutate assembled geometry, HEAD,
  ordinary exports or historical artifacts. Projection cache keys include the
  offsets. Cached assemblies reconstruct placement/ownership from exact cached
  source B-reps and saved intent, avoiding redundant compound snapshots or
  reliance on deserialized enumeration ownership.
- Added `docs/ASSEMBLIES.md`, bundled guidance and an editable four-part
  plate/spacer example with assembled and exploded front/isometric drawings.
  STEP/STL preserve placed geometry; saved JSON retains the assembly semantics.

Executed locally on macOS arm64 with OpenCascade 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **24/24 passed in
  27.33 seconds**. New suites passed **88 service checks**, **85 model checks**,
  **5,385 kernel checks**, and **59 drawing checks**. Service counts include job
  polling and can vary. Coverage includes independent/coincident instances,
  full frame composition and inverse child orientation, parameter edits, graph
  rejection, rollback, detach/placement, historical source, cache restoration,
  stale picks, live context, native STEP readback, asynchronous edits, exploded
  sections and regeneration. Existing geometry, jobs, cache, drawing, protocol
  and viewer regressions pass; the WebGL suite now consumes the assembly mesh.
- **219 schema checks across 18 tools** and **345 official MCP SDK 2.3.0 checks**
  passed (polling affects counts), including an asynchronous exploded assembly drawing. Discovery schemas
  cover assembly intent, edits, inventory, ownership and live summary responses.
- Generated and visually inspected the example PDF using Poppler. Its assembled
  dimensions are 60 mm wide and 24 mm high; exploded height is 48 mm. Independent
  pypdf/ezdxf validation passed **22 checks** across the single-page PDF, four DXFs,
  artifact sizes and retained recipe. All DXFs use mm and audit with zero errors
  or fixes; exploded labels appear only on the corresponding views.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  verified native relocation with empty PATH and SDK environment removed. Its
  new assembly smoke requires the part inventory and labeled exploded SVG, in
  addition to the existing create/edit/reopen/export/drawing/cache/MCP checks.
  The packaged documentation includes `ASSEMBLIES.md` and the editable examples.
- Build, CTest, schema/SDK, native artifact and independent-reader evidence is
  retained under `build/assembly-demo/`. `git diff --check` passed.

Scope: same-document, one-level assemblies and deterministic rigid datum mates.
Nested assemblies, linked document parts, closed-loop/general mate solving,
kinematics, collision/fit validation and per-part hide/isolate
controls remain future work. Solid operations consume source parts, not assembly
outputs. STEP export is placed compound geometry rather than a promise of XCAF
product hierarchy or editable mates. This increment has local macOS evidence;
earlier multi-platform CI and human viewer acceptance do not certify these new
sources. No global installation, remote push or release was performed.

Next: pursue nested/linked assemblies or additional mate types only
with explicit workflows; separately investigate cold HLR performance for complex
threaded solids. The assembly contract is in `ASSEMBLIES.md`.

## Latest increment — geometry and projection caching

Implemented automatic disposable caching through the shared native worker path:

- `.cache` holds exact OCCT B-rep snapshots for every feature, provenance and the
  output summary, plus complete ordered drawing view sets. Restoring geometry
  validates every shape and topology count inside the bounded worker. Triangles
  are regenerated; selection/evaluation IDs remain fresh. No public MCP fields,
  tools, document format changes or cache API were added.
- SHA-256 keys cover the complete model intent (including embedded STEP content),
  native source/header fingerprints, toolchain/configuration, selected OCCT SDK
  binaries, kernel and cache-format versions. Source fingerprints include current
  geometry/projection tolerances. Projection keys add the ordered views, hidden
  lines, section planes and hatch extraction. Titles, sheet layout, dimensions,
  tolerances, formats and view names rerender using the same projections.
- Entries have payload checksums; reads and writes are bounded. Exact B-rep text
  is limited to 32 MiB, encoded entries to 64 MiB and the workspace cache to 128
  entries / 256 MiB. A native lock protects oldest-publication-first eviction and
  atomic writes. Missing, corrupt, oversized, symlinked, unavailable or busy
  caches fall back to rebuilding/skipped publication. Interrupted cache writes
  are cleaned on the next successful publication. Cache loss cannot lose intent.
- Workers only read shared cache and stage new entries. Coordinators check
  cancellation before publishing results from successful workers. Revision locks,
  mutation validation, HEAD publication, resource limits and artifact identities
  are preserved. Cache hits do not share OCCT objects between processes/threads.

Executed locally on macOS arm64 / OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **20/20 passed in
  25.15 seconds**. The new cache suite includes **44,578 checks**: exact feature
  geometry/provenance round trips for threaded, sketched, lofted and imported
  models; equivalent per-face mesh area/oriented volume and selection edges;
  cold/warm drawing artifacts; changed annotations/names/layout/formats; model,
  hidden-line and section invalidation; checksums, damaged B-reps, deletion,
  >1 MiB entries, quotas, concurrent publication and symlink fallback. The job
  suite's **46 checks** include no cache publication from cancelled, crashed,
  timed-out or memory-limited workers. Rendering failures after staging also
  publish no cache. Existing drawing/viewer/transaction regressions pass.
- **189 schema checks across 18 tools**, **296 MCP SDK 2.3.0 interoperability
  checks** (counts vary with polling), and `git diff --check` passed.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  native relocation with empty PATH and removed SDK environment. It now also
  requires geometry/projection cache entries and checks repeated angular
  PDF/SVG/DXF artifacts have identical SHA-256 hashes.
- Reproducible developer benchmark:
  `python3 tests/cache_benchmark.py build-app-protocol/agent-3d-cad build/cache-demo`.
  The clean run uses the unmodified full M20 threaded knob, with two hatched
  section views, width/height dimensions and PDF/SVG/DXF outputs. Durable job
  timestamps include coordinator/worker/cache/artifact publication:

  | Operation | Seconds |
  | --- | ---: |
  | Cold model creation and geometry cache | 1.373 |
  | Cold section drawing (both caches removed) | 5.682 |
  | Repeated drawing, median of three | 0.126 |
  | Changed A3 sheet/title/general tolerances | 0.127 |
  | Geometry cached, projections removed | 4.544 |
  | Changed thread length, rebuilt new revision | 1.814 |

  Repeated section generation is **45.1x faster** in this local run. All warm
  PDF/SVG/DXF bytes match cold artifacts. Geometry-only regenerated dimensions
  agree within 1e-8; styled dimensions match the 38 mm height and exact model
  width. Editing thread length changes volume while historical source remains
  unchanged. Report, manifests and artifacts:
  `build/cache-demo/cache-benchmark-2bbbfeg7/`. Suite, SDK, schema and bundle logs
  are in `build/cache-demo/`. The benchmark has optional `--views standard`.

Limits and remaining work: this caches whole models and complete ordered view
sets, not incremental features or independent views. Meshes and rendered files
are not cached. Entry count/size eviction is oldest-first, not LRU. A full
four-view hidden-line drawing of the M20 knob still hit a **240-second cold job
limit**; earlier synchronous attempts hit 30 seconds. The failed job/state is
retained under `build/cache-demo/cache-benchmark-_g4wzpwl/`; its subsequent
orthographic/top diagnostic probes were cancelled. Caching does not fix that
first-generation HLR bottleneck and no warm full-view speed claim is made.
These changes have local macOS evidence, not a new five-platform CI claim. No
commit, global installation, remote push or release was performed.

Next: editable assemblies with multiple parts, placements, mates and exploded
views; separately, investigate first-time exact HLR performance on complex
threaded solids. Those capabilities remain planned.

## Latest increment — angular dimensions and explicit tolerances

Implemented the user's next drawing increment through the shared native service,
CLI/MCP schemas, and isolated drawing jobs:

- `kind: angular` measures two directed projected straight-line references.
  Each reference's `from` and `to` points must resolve to the same supporting
  line; nearby distinct matches fail. The actual line directions and intersection
  supply the angle and vertex, including virtual intersections. `sweep` selects
  minor or reflex angles; optional model-mm `arc_radius` sets annotation size.
  Parallel/collinear lines and unreadably small arcs fail explicitly. Arcs and
  virtual vertices participate in sheet fitting without changing width/height
  measurements. PDF/SVG render tangential arrows and extension/leader lines;
  DXF preserves analytic annotation ARC entities at 1:1 mm.
- Per-dimension `manufacturing_tolerance` supports symmetric allowances, signed
  lower/upper deviations (including unilateral/same-sign values), and absolute
  limits. Optional `general_tolerances.linear`/`.angular` supply symmetric mm/deg
  defaults, overridden by individual dimensions. General notes are printed in
  both sheets and standalone DXFs. No tolerance is inferred, and the old circle
  `tolerance` remains a matching rule only.
- Tolerance scalars preserve parameters and bounded expressions with mm/deg
  contexts. Explicit allowances support six decimal places; finer input fails
  rather than rounding away intent. Toleranced nominals display six decimals,
  trimming zeros; deviations apply to that displayed nominal and absolute limits
  must contain it. Results distinguish unrounded `value_mm`/`value_deg` from
  `display_value_mm`/`display_value_deg`, report the exact printed label, effective
  allowance/source, and acceptance bounds. Projection accuracy is unchanged.
  Longer tolerance labels move outside short dimension spans to avoid crossing
  extension lines; unfittable annotations fail.
- Added editable `examples/angular-plate.create.json` and its drawing request
  with explicit demonstration allowances. The user's existing sprocket drawing
  was not assigned guessed manufacturing tolerances. Updated discovery contracts,
  protocol/drawing docs, README, packaged native-cad guidance and roadmap.

Executed locally on macOS arm64 with exact OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` followed by
  `ctest --test-dir build-app-protocol --output-on-failure`: **19/19 passed in
  19.37 seconds**, including **1,876 drawing checks**. New checks cover measured
  acute/obtuse/reflex and section angles; analytic DXF arc radius/sweep; ambiguous,
  missing and parallel references; signed/micron tolerances, explicit limits,
  general overrides and DXF preservation; wrong units and invalid ranges;
  failed-publication rollback; parameterized regeneration after reopening; and
  asynchronous angular drawing jobs. `git diff --check` passed.
- Final independent contracts: **189 schema checks across 18 tools** and **286
  official MCP SDK 2.3.0 interoperability checks**. Counts can vary with job
  polling. These include successful native angular/tolerance requests and degree
  output schemas that cannot be confused with linear `value_mm` results.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  relocation with empty PATH and SDK environment removed. The smoke now creates
  the angular plate and checks its 45-degree result, 44.75-degree lower limit,
  exact printed tolerance label and 7.98 mm bore lower limit.
- Generated 45-degree and 315-degree example sheets and visually reviewed their
  Poppler-rendered PDFs. **40 independent pypdf/ezdxf checks** verified two
  single-page PDFs, four clean DXF audits with zero errors/fixes, mm units,
  explicit/general tolerance text, and both annotation arcs' center, radius and
  sweep at 1:1 mm. Editable source read back unchanged after export. PDFs, PNGs,
  manifests, independent-reader results and all logs are retained under
  `build/drawing-angular-demo/`.

Limitations: angular dimensions reference straight lines in the selected 2D
projection, not arbitrary curved-edge tangents or 3D angles inferred from an
isometric view. Labels use portable ASCII (`deg`, `+/-`, and `.. LIMITS`); this is
not a GD&T or standards-certification implementation. These changes have local
macOS native/bundle evidence; the older five-platform CI results do not validate
this increment. No global installation, remote push or release was performed.

Next: measured geometry/projection caching, particularly repeated drawings of
threaded parts; then editable assemblies with placement, mates and exploded
drawings. These remain planned capabilities.

## Latest increment — aligned drawing layouts and section hatching

The user selected hatching and standard layouts as the next increment. The
existing macOS Codex select/edit/refresh acceptance below already closes the
viewer loop; this work does not claim a new host/platform acceptance run.

Implemented through the shared native `cad_drawing` service and bounded workers:

- `layout: third_angle | first_angle | grid`. Standard layouts align front/top
  world X and front/right world Z at a common fitted scale, even with unequal
  annotation margins. View ordering in a recipe does not change the arrangement.
  A front view is required; duplicate orthographic/isometric orientations fail.
  Isometric and up to two additional section cells fit beside the orthographic
  group; absent orthographic views leave reserved cells. The title block states
  the convention. Defaults without custom views use third angle; existing custom
  view recipes without `layout` retain grid ordering. `view_layouts` reports
  sheet cells and origins with documented coordinate mapping.
- Section views default to material hatching, with `hatch: false` for outlines.
  OCCT intersects each solid with the section plane, retaining faces and inner
  boundaries. The renderer clips 45-degree lines at 2.5 mm sheet spacing using
  analytic lines/circles/arcs and the existing bounded polyline approximation.
  Face intervals are united, preserving cavities, disconnected material and
  nested islands without parity cancellation between overlapping solids.
  An added overlap regression exposed empty material output from intersecting
  a whole interfering pattern compound; per-solid intersection fixes that case.
  A tangent profile without material area fails explicitly unless hatch is off.
- PDF/SVG use thin hatch strokes; 1:1 DXFs retain clipped line entities on a
  separate `HATCH` layer. Hatch geometry never participates in dimension matching.
  All material boundaries share the existing projection budgets. Additional
  limits bound scan lines, intersection work and output segments. Failed layout
  fitting or hatching publishes no drawing and never changes model HEAD.
- Updated discovery schemas, protocol/drawing docs, packaged native-cad guidance,
  and examples. `examples/plate-section.drawing.json` uses a parameterized section
  offset; the keyed sprocket recipe now requests third angle explicitly.

Executed on macOS arm64, exact OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **19/19 passed in
  18.45 seconds**. The final additional tangent-section regression passed the
  drawing suite in **1.08 seconds**, now **1,825 checks**. Coverage includes
  layout placement/alignment, negative coordinates and unequal annotations;
  bore/keyway voids, islands, disconnected/overlapping material, circular seams,
  elliptical polyline sections, multiple scales, explicit hatch disablement,
  tangent profiles, work limits, failed scale rollback, existing regeneration
  and asynchronous job behavior. `git diff --check` passed.
- **179 schema checks across 18 tools** and **283 official MCP SDK 2.3.0 checks**
  passed. Native schemas accept the new contracts and reject invalid layout/
  hatch shapes. Logs: `build/drawing-layout-demo/{schema,mcp}.log`.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  verified relocation with empty PATH and SDK environment removed. Its smoke
  now additionally generates the parameterized plate section, checks third-angle
  metadata, and verifies actual hatch strokes in the exported SVG. No global
  installation or host registration was changed.
- Recreated the keyed 13-tooth sprocket in an isolated workspace, applied its
  saved revision-2 fillet, and generated both first-/third-angle A3 sheets plus
  the parameterized plate section. The sprocket source read back unchanged.
  Poppler rendered both final sprocket PDFs for visual inspection. Independent
  pypdf checks verified three single-page PDFs, revision/convention labels and
  all eight sprocket dimensions. All **14 DXFs** passed ezdxf audit with zero
  errors/fixes and mm units. They contain **98 hatch segments**; 101 samples per
  sprocket hatch segment stayed in the hub material outside the bore/keyway.
  Evidence, PDFs, PNGs, manifests and CTest logs: `build/drawing-layout-demo/`.

Limitations: section views remain plane profiles, not cutaway projections with
cutting-plane arrows; hatching has one fixed angle/spacing and does not encode
material conventions. DXF hatch lines are not associative HATCH objects. These
changes have local macOS build/bundle evidence; the earlier five-platform CI
results below do not validate this increment. No remote push or release was made.

Next: angular dimensions and explicit manufacturing tolerances; then measured
geometry/projection caching (especially threaded parts); then editable assemblies
with placement, mates and exploded drawings. Those capabilities remain planned.

## Latest increment — GitHub CI and independent Arch Linux validation

The user created and checked in the public repository
`https://github.com/cfaulkingham/agent-3d-cad` and authorized an independent Arch
Linux test host. The initial source commit is
`04e73a43b2199027c37e114907c253e96f2e560e` (`init`). Its initial five-platform
push run is `https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37536892312`.
Both macOS and both Ubuntu lanes completed successfully. The first Windows
lane built its SDK and service but failed two native suites. The corrected run
passed all native and MCP checks, then exposed a bundle dependency-filter issue;
the final packaging follow-up passed all five native lanes, as recorded below.
This supersedes
earlier statements that there was no remote repository or runner execution.

The successful `macos-15` arm64 runner passed **17/17 CTests** in **49.03
seconds**, **171 schema checks**, **298 official MCP SDK checks**, relocation
with empty PATH, and native packaging. Its downloaded archive independently
verified **244 files**, has SHA-256
`051776b1adb379ac424c218f2b4156a7008eea834edb3354cbfed2f847c0acb8`, and records
**minimum macOS 15.0** rather than the local SDK's 27.0 baseline. Its provenance
SHA-256 is `2dee873fa3099d5ef4c02b79da801346be885569beca0c7b03a579aaea7ef621`.
That exact archive was also installed into an isolated local directory and
passed the full runtime workflow on this Mac with SDK environment removed and
developer tools hidden from PATH. No global service registration changed.
Logs, archive verification and installation evidence are in `build/platform-ci/`.

The `macos-15-intel` x64 runner passed **17/17 CTests** in **89.03 seconds**,
**173 schema checks**, **343 official MCP SDK checks**, empty-PATH relocation
and packaging. Its independently verified archive contains **244 files**, has
SHA-256 `8b052a4f0effeaf813c234a21ed94697b43d2de13b9b9b7dcb0fbd8e55f5cc51`,
and records **minimum macOS 15.0**. Provenance SHA-256 is
`a73532891a21b93f7ccd5871e5faa9ba133e61b3befaf08f9c48d391f70a605b`.
This is native Intel runner evidence, without installing Rosetta on this Mac.

Both Ubuntu 24.04 CI lanes passed all native, schema, official MCP SDK,
relocation, packaging and **fresh runtime-only container** checks. The container
asserted the absence of developer tools while generating PDF/SVG/four DXFs and
preserving the model record. Recorded CI test results:

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 17/17 | 49.03 | 171 | 298 |
| macOS 15 / x64 | 17/17 | 89.03 | 173 | 343 |
| Ubuntu 24.04 / arm64 | 17/17 | 27.63 | 169 | 243 |
| Ubuntu 24.04 / x64 | 17/17 | 30.66 | 169 | 243 |

The first Windows run passed **15/17 suites**, including persistence, locks,
jobs, topology, drawings and the native MCP/viewer loop. It failed `modeling`
because OCCT's STL filename overload opens a narrow standard stream and creates
the wrong name for `prism-é.stl`, and failed the exact embedded-source check in
`app_protocol`. It is not reported as a successful Windows acceptance run.
The portability patch on `codex/native-platform-validation`:

- Uses OCCT 8.0.1's existing STL stream overload with `std::ofstream(path)`;
  native wide paths work on Windows, and flush/close failures remain explicit.
- Builds the app byte initializer from normalized UTF-8 data, forces the
  generated review HTML to LF, and preserves its exact final newline. The
  generated HTML and native resource retain app hash
  `affa14fe5b7576007408288b25074192df2b8ba79a56b8b4b9fb3867c2a9a3a7`.
- Keeps both failing assertions and adds binary STL read-back plus an isolated
  LF/CRLF checkout regression. The latter checks physical bytes and exact output,
  avoiding CMake text reads that can hide newline differences. Source-integrity
  failures now report actual/expected lengths and the first differing position.

Before pushing the patch, macOS passed all **18/18 suites** in **18.60 seconds**;
the final embedding checks passed **4/4 relevant suites** in **1.18 seconds**.
The final Arch patch passed **18/18 suites** in **17.97 seconds**. Both final
relocated bundles passed the native workflow and embedded-resource hash check
with empty PATH. A temporary embedding implementation added a final newline;
the exact Arch assertion detected it and the final patch corrected it rather
than changing expectations. `build/arch-validation/windows-fixes*` and its
`logs/windows-fixes*` retain the executed evidence. The corrected Windows run
subsequently verified both fixes, as recorded below.

The corrected source commit `555737b23c4e66dbbf21436b532f5999c16c0460` completed at
`https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37544102649`.
Its four macOS/Ubuntu lanes have completed successfully, including the added
physical-byte embedding regression:

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 18/18 | 40.39 | 175 | 328 |
| macOS 15 / x64 | 18/18 | 149.49 | 171 | 333 |
| Ubuntu 24.04 / arm64 | 18/18 | 26.71 | 169 | 248 |
| Ubuntu 24.04 / x64 | 18/18 | 19.78 | 171 | 248 |

All four corrected archives passed independent file-set, link and SHA-256
verification, with 244 manifested files per macOS archive and 256 per Ubuntu
archive. Every bundle passed verified relocation and the empty-PATH native
workflow; both Ubuntu lanes also passed the fresh runtime-only container.
`build/platform-ci/fixed-evidence.json`, `fixed-*.log` and
`fixed-*-verification.json` retain the consolidated evidence. Corrected archive
SHA-256 values:

- macOS arm64: `8c12a815f8248698a303011890bd80735eb31fb3ea1156f25d9efcdc3c14f12e`.
- macOS x64: `d1b46c072c451e03261ac01c7c9fb21ca49a6102200b1128545ec9feb86e0205`.
- Ubuntu arm64: `e5a2b495b740020d29ef65d0a652950b85fe0f4e096d6a21c564afdc660c5050`.
- Ubuntu x64: `ca1bca3308d2d0ca1c93074ea9f454b024f9f28373cc108a4ca782a3dc0a42b9`.

The corrected Ubuntu x64 archive was separately hash-checked, installed and
relocated on Arch. Its full PATH-isolated native runtime workflow and **171
schema checks across 18 tools** passed. Logs and the installation record are
in `build/arch-validation/logs/ubuntu-fixed/`. This tests the patched CI binary,
in addition to the independent patched Arch compiler build.

The corrected Windows x64 run passed **18/18 CTests in 57.03 seconds**, **167
schema checks** and **243 official MCP SDK checks**. This verifies Unicode STL
export/read-back and exact viewer embedding on native Windows, alongside real
geometry, persistence, workers and drawings. It then failed bundle relocation:
`wtdccm.dll` was unresolved. CMake's policy warnings show paths such as
`C:\Windows\system32/advapi32.dll`; the old forward-slash-only filter missed these
system paths and traversed the OS dependency graph. This is a packaging failure,
not successful bundle acceptance; the failed run and full log are retained.

The packaging follow-up matches both separator styles at every System32 boundary
and selects normalized paths when CMake supports
[CMP0207](https://cmake.org/cmake/help/latest/policy/CMP0207.html). It retains hard
failure for unresolved application libraries and conflicting dependencies. The
new `runtime_filters` CTest checks **16 cases**, including the actual mixed paths,
normalization/case variants, SDK/MSVC DLLs, unresolved names and lookalike
folders. Local macOS passed **19/19 suites in 18.57 seconds** and the relocated
bundle workflow with empty PATH. Evidence is in
`build/platform-ci/packaging-fix-local-{build,ctest,bundle}.log`.

CI now uses separate cache restore/save actions, saving a completed SDK before
service tests or packaging. Previous Windows failures discarded each successful
42-minute SDK build because the old cache action saved only on job success. SDK
keys now hash the exact recipe plus an explicit compiler/deployment/configuration
ABI tag; service or packaging edits do not invalidate them. The four existing
verified macOS/Ubuntu caches migrate only under the unchanged recipe hash and
ABI tag, using their exact original key; there is no broad restore fallback.
The workflow passed actionlint 1.7.12. The final native Windows relocation and
archive run subsequently passed, as recorded below.
The final portability/packaging source commit
`2e96291e8114bdbe362eb935f37fe228ac3f0f2a` passed **all five native CI lanes**:
`https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37548998613`.
All lanes passed native geometry/persistence/workers, schema conformance, the
official MCP SDK, verified relocation with empty PATH, and native packaging.
Both Linux lanes additionally passed the fresh runtime-only container.

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 19/19 | 38.71 | 171 | 293 |
| macOS 15 / x64 | 19/19 | 95.14 | 171 | 333 |
| Ubuntu 24.04 / arm64 | 19/19 | 26.26 | 171 | 248 |
| Ubuntu 24.04 / x64 | 19/19 | 30.48 | 171 | 248 |
| Windows 2025 / x64 | 19/19 | 47.25 | 167 | 248 |

All five downloaded archives independently passed complete file-set, per-file
SHA-256 and link verification. macOS archives contain 244 manifested files,
Ubuntu archives 256, and the Windows ZIP **270**. Windows records MSVC
19.51.36260.0, exact OCCT 8.0.1 and the same canonical viewer app hash as all four
other platforms. Its ZIP also passed case-insensitive filename uniqueness and
checks that kernel32/user32/advapi32/wtdccm OS DLLs were not copied. It contains
the required OCCT/FreeType DLLs and the MSVC/UCRT redistributable runtime.
Final archive SHA-256 values:

- macOS 15 / arm64: `1ec9ee4815e7fbf39ac4749ac02bef0441df4c710a7c4677ffaf6856ed22f64b`.
- macOS 15 / x64: `79b748997eeb9a80937da5aad56c836d6920913b4812c5fa646a8a6fcbed04dc`.
- Ubuntu 24.04 / arm64: `880c8eae74424d7e65ec59ff850e8d745a609d9e895d59a482efdb2557363148`.
- Ubuntu 24.04 / x64: `030fd69ea991b04ee08dc686fc437744ec32902d93432342c79590f1882ed48e`.
- Windows 2025 / x64: `b102604ad9042ea5dd1076b03becbc03239934a28818426ac16cb99dfc34702e`.

Windows bundle relocation generated PDF/SVG/four DXFs and exercised the native
CLI/MCP/worker workflow with empty PATH and SDK variables removed. This is
native runner relocation evidence, not a fresh Windows desktop or GPU-host
claim. Actual GPU selection/edit/refresh remains demonstrated on the Codex Mac.
The Windows SDK build took **41 minutes 49 seconds** and was successfully cached
before service testing; the service/tests/contracts/package portion took about
four minutes. The new cache saved under the Windows recipe's CRLF hash
`41d6d2e21ae2be0cfd597d63d52d59286c7a291edccf3aaa07941070083bb496`.
The four previously verified SDKs also migrated to the new keys without rebuilds.
`build/platform-ci/packaging-evidence.json`, `packaging-*.log` and
`packaging-*-verification.json` retain the complete final evidence. Initial and
intermediate Windows failures remain recorded above and were not weakened into
passing assertions. Native platform validation is complete; next authorized
product work is better drawing layouts, hatching, angular dimensions and
explicit tolerances, followed by caching/performance and assemblies.


Both downloaded Linux archives independently verified all **256 manifested
files**, including link destinations and the exact file set. The x64 archive
SHA-256 is `904176122410cfdbf6713fd716d9b50205af81204315edbf3fab6ad8572db2fa`
and the arm64 archive SHA-256 is
`a41a04bb1c1c5ac5dda64b9fe970f0d4c6163184dc40368e3b46559c9ee8da64`.
That exact **Ubuntu x64 archive was transferred to Arch**, rechecked against its
archive hash, verified/relocated with the installer, and passed the full runtime
workflow and **171 schema checks across 18 tools** there. This demonstrates the
Ubuntu-built native bundle on a newer Arch distribution, in addition to the
independent Arch compiler build below. SDK environment and developer PATH were
removed during that runtime check; the Arch host itself still has developer
tools installed. Logs and installation records are in
`build/arch-validation/logs/ubuntu-*`.

That exact commit was archived and transferred to a fresh, isolated directory
on the user-supplied Omarchy 4.0.4 / Arch x86_64 host. Its compiler was GCC
16.2.1 and its glibc was 2.44. All three upstream archives were verified against
the recorded SHA-256 pins. OCCT 8.0.1 and FreeType 2.14.3 were built from those
archives, without a system OCCT or sibling checkout. CMake 3.31.10, patchelf
0.19.1 and the independent Python MCP client were installed only in a test-local
virtual environment; system packages and user service configuration were not
changed.

Executed Arch evidence, retained locally in `build/arch-validation/`:

- The complete native build and **17/17 CTests** passed in **18.01 seconds**.
- The verified, relocated shared-library bundle passed model creation, editing,
  reopen/rollback, STEP/STL/drawings/MCP and embedded app-resource checks with
  empty PATH and SDK loader/resource overrides removed.
- The independently installed bundle passed **173 schema checks across 18
  tools** and **248 official MCP SDK 2.3.0 interoperability checks** (counts
  vary with job polling).
- The runtime workflow passed with only required OS utilities on PATH and SDK
  environment removed, including native PDF/SVG/four-DXF generation and
  byte-identical model readback after drawing. This host has developer tools
  installed: this is a PATH-isolation test, not a fresh tool-free OS/container.
  The reused smoke script's container wording does not change that limitation.
- A native Arch preview archive was produced, SHA-256
  `004bcd91a1ce4f17fdef5f4c9e9ee65cf8c45dd4c20e7cb3e77777c15b823a63`.
  Its **256 manifested files** were independently verified after retrieval,
  with no extra entries or escaping links. Provenance SHA-256 is
  `5cf896613d18d25357ce8f62d6f068d44754b0920c79ea943480da16ca1f7f17`.
  It inherits the Arch glibc baseline and is not evidence of compatibility with
  older distributions. The Ubuntu CI bundle remains the intended portable
  Linux baseline.
- The original knob and duplex sprocket examples were replayed in a separate
  Arch workspace, with **23 model/geometry/export/drawing/source-preservation
  checks**. The sprocket retained 409 faces, 1,210 edges and volume
  64,181.2664969257 mm³, and generated PDF/SVG/five DXFs. Both saved records
  remained unchanged after STEP/STL export and drawing generation.

The extra knob comparison exposed the limitation of the existing non-adaptive
volume quadrature: the identical example gives 32,476.00416788722 mm³ on macOS
and 32,476.001346121142 mm³ on Arch, a 0.00282 mm³ difference. An initial extra
0.001 mm³ comparison failed and is retained in `representative.log`; it is not
reported as passing. Independent native readers then loaded both platforms'
STEP files on both platforms with healing disabled: all four retained one valid
solid and passed **15 helix/flank/root/crest/lead probes each**. Adaptive volume
integration at requested relative errors 1e-7/1e-9/1e-11 gave final values within
**0.000000433 mm³** of each other (reported relative error about 1.44e-10).
The continuation compares that independent geometry at the original 0.001 mm³
threshold, rather than broadening it or changing product tests. The default
summary should not imply precision beyond its integration method. Numerical
read-backs, probe source, original failure and final replay logs are retained.

The first temporary SSH driver stopped after the successful native suites and
relocation because its schema-test filename was mistyped. The actual unchanged
`tests/schema_conformance.py` and remaining checks were then executed directly
and passed; no product test was weakened. Original and corrected driver logs
are retained. The final corrected Windows x64 runner results are recorded above.

## Latest increment — actual viewer acceptance and current Linux validation

The user authorized working through the remaining items starting with the live
viewer loop. The **installed** service created separate `viewer_loop_plate`
revision 1 and opened `viewer_loop_acceptance` once in the actual Codex MCP App.
The human expanded the app and picked its 60 mm top/front edge. Native context
resolved `edge-10` in evaluation `eval_5c13132c3b3f0d477d7c3b1c7e0cc344`
to one geometric selector, centered at [30, 0, 12] mm. Quick Edit published that
reference and the request “Round this selected edge to 1 mm.” The host acknowledged
the handoff and placed it in the chat composer. The human confirmed sending it.
The exact request then arrived as an MCP App message after the acceptance turn,
independently confirming receipt. Its revision-1 context was explicitly checked
against saved revision 2, which already contained the requested fillet; no second
edit/revision was created. Do not claim the host automatically posts messages.

The agent read the document and context, explicitly resolved the selection, and
committed only that edge's 1 mm fillet as revision 2. The same rendered app
followed automatically, displayed the new feature and rounded edge, and cleared
the old pick. Native camera yaw/pitch/zoom/pan matched within 1e-12 (only yaw's
floating-point normalization changed). The solid stayed valid with seven faces,
15 edges and volume 28,787.12388980385 mm³. A stale revision-1 pick failed with
`stale_selection`; an intentionally impossible radius failed on `rounded`
without changing revision 2 or its displayed evaluation. The sprocket and knob
were not edited. Actual DOM, GPU screenshots and native calls are in this turn;
`build/viewer-acceptance/host-evidence.json` and `plate-r2.jpg` retain local evidence.

Two concrete fixes followed the acceptance work:

- The UI now says **Send to chat** and explains the possible composer Send step.
  An acknowledgment no longer claims “Request sent.” The guide, protocol and
  packaged native-cad skill describe this host behavior.
- A repeat Linux run exposed a transient `workspace_busy` during post-edit
  polling. The actual state controller now treats read-only polling lock/queue
  contention and superseded transfer references as loading, disables old picks,
  preserves the last rendered solid/camera, and retries a fully validated transfer
  on the next poll. Other failures remain explicit. This never retries mutations,
  context writes or message delivery. Deterministic tests cover each transfer
  stage, revision changes, disabled sends during recovery and persistent errors.

Executed evidence:

- macOS arm64: five relevant CTest suites passed in **2.94 seconds**, including
  **63 bridge/state checks** (up from 40). Ten additional consecutive actual
  native MCP/controller loops passed **13 checks each**. Logs:
  `build-app-protocol/viewer-acceptance-tests.log` and
  `build/viewer-acceptance/stress.log`.
- Current Linux arm64 source in Docker's native aarch64 engine: **17/17 CTests**
  passed in **15.26 seconds**, **171 schema checks across 18 tools**, and
  **253 official MCP SDK 2.3.0 interoperability checks**. Relocation with empty
  PATH passed. The fresh Ubuntu runtime-only stage, with no Python/Node/Rust/
  compiler/CMake, passed the model/view/jobs/export/MCP workflow plus actual
  native PDF/SVG/four-DXF generation and byte-identical model readback afterward.
  The drawing runtime smoke was added in this increment. Full final log:
  `build-linux-container/final-runtime.log`. An earlier run failed the polling
  race above; its test was not weakened or retried to manufacture a passing claim.
- Both preview archives were independently checked against every manifested
  file, including in-tree symlink destinations and rejection of extra entries:
  **244 macOS files**, **256 Linux files**. Both embed app SHA-256
  `affa14fe5b7576007408288b25074192df2b8ba79a56b8b4b9fb3867c2a9a3a7`.
  `build/viewer-acceptance/archive-verification.json` records full archive paths,
  sizes and hashes. The macOS bundle is installed as a fresh version; its
  executable passed **169 schema checks** and **278 official MCP SDK checks**
  (counts vary with polling). `install-result.json`, `registration.json` and
  `installed-{schema,sdk}.log` in that evidence directory record the installation
  and preserved configuration/skill backups. Only the CAD command path and its
  skill changed; unrelated configuration bytes and saved models are intact.

This does not establish offline artifact/browser acceptance or other-host
behavior. Native Windows x64, Linux x64 and macOS x64 still need execution.
At the end of that earlier increment, the five-platform CI definition had no
remote repository or runner execution. The user subsequently created and
checked in the GitHub repository; see the newer platform-validation record above.
Local packaging/installation records for this increment live in
`build/viewer-acceptance/`; refresh the running MCP connection before expecting
new app text/race handling in an already mounted viewer.

## Latest increment — native engineering drawings

The user authorized implementing 2D drawings from saved models. `cad_drawing`
is now the eighteenth CLI/MCP tool and is admitted by `cad_job`. It generates
PDF/SVG sheets and separate 1:1 mm DXFs for one to six front/top/right/isometric
or planar section views. It preserves original parameterized drawing recipes
for explicit regeneration against a later committed revision. Drawing exports
never change model HEAD. Details and examples are in `DRAWINGS.md`.

Implementation:

- `BuiltModel::drawing` in `kernel.cpp` keeps OCCT hidden-line removal and plane
  intersections inside the kernel boundary. Analytic lines/circles/arcs remain
  analytic; other curves are approximated within 0.02 mm model-space deflection.
  Coincident projected entities are deduplicated, edge-on circular splines
  collapse to lines only when their poles prove collinearity, and covered hidden
  line spans do not redraw visible boundaries as dashed geometry.
- `drawing.cpp` validates closed recipes, resolves existing bounded scalar
  references, measures view extents and geometry-attached linear/radial
  dimensions, rejects missing/ambiguous references, lays out labeled A4/A3
  sheets, and writes native PDF/SVG/DXF. No converter or language runtime is
  added. Printable labels round to three decimals; returned values retain their
  measured precision. DXF uses valid R2000 handles, owners and table records.
- Projection and rendering both execute under worker time/memory limits. The
  coordinator writes a fresh generation directory and publishes its manifest
  last under the document lock after a cancellation check. Failure cannot
  replace existing drawings. Sidecars retain original/resolved intent and the
  source model SHA-256. A crash may leave a directory without a final manifest;
  it is an unpublished export, not a committed model revision.
- Bundle documentation, schemas, agent skill, examples and empty-PATH relocation
  checks include the new workflow. Ordinary model/STEP/STL contracts remain intact.

Final macOS arm64 evidence in `build/drawing-demo/`:

- `cmake --build build-app-protocol --parallel 4` succeeded. All **17/17 CTests**
  passed in **18.01 seconds**, including **434 drawing checks**. Tests cover
  exact view extents, hidden bore edges, arcs and section offsets; attached,
  missing and ambiguous dimensions; native PDF cross-reference offsets; DXF
  coordinates/units/handles/ownership; historical recipe regeneration; preserved
  files/HEAD after failure; and asynchronous drawing jobs. `git diff --check`
  passed. Existing modeling, storage, jobs, MCP and viewer suites remain green.
- The final installed binary passed **171 JSON Schema checks across 18 tools**
  and **293 official MCP SDK 2.3.0 interoperability checks** (polling counts can
  vary); logs are `schema-installed.log` and `sdk-installed.log`. The actual
  installed service also completed **11 real MCP
  calls** in `installed-mcp.json` to draw the user's saved sprocket and confirm
  its source record was unchanged.
- All five representative DXFs passed **ezdxf 1.4.3 with zero errors and zero
  repairs** (`dxf-audit-final.json`). Poppler rendered the A3 PDF; visual review
  found readable, unclipped dimensions and labels. Independent strict pypdf
  parsing confirmed model/revision and dimension text. All seven files produced
  by the installed MCP service match these reviewed files byte-for-byte.
- `bundle-check` generated drawings after relocation with empty PATH and no SDK
  loader/resource overrides. The final archive has **244 verified files** and
  matches the installed provenance. It is 23,893,213 bytes, SHA-256
  `c0ca67aa5fe3472e239ecf752accc0c3b9ceba4696f06c79d5cc7251cf871c97`.
  Path: `build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`.
  Provenance SHA-256:
  `af58c23d07b881a17260db5c8b4b8971ce8b73c2bd94f49a6acdbc8e0b606134`.

The new version is installed under `~/Applications/Agent3DCAD/`; exact executable
and backups are recorded in `build/drawing-demo/install-result.json` and
`registration.json`. The existing Codex registration now points to it and the
global native-cad skill is updated. The CLI registration command unexpectedly
removed another MCP server's `args` field; a byte-preserving correction restored
the original configuration with only the authorized CAD executable path changed.
All unrelated settings were verified preserved. Previous installations, skill
and config backups remain available. A running host connection may need an MCP
refresh to discover `cad_drawing`; no Codex UI automation was attempted.

The user's drawing is in
`~/Documents/Agent3DCAD/exports/duplex_35_sprocket_20t-r1-drawing-eval_0f63d8497144773773715bc2f23ddd5e/`.
Its sheet measures bore **19.05 mm**, hub length **34.925 mm**, and row spacing
**10.1346 mm** (printed 10.135), with a real plane section through the first row.
The 3D source is still revision 1. The PDF was queued for the Codex file preview.

Limits: labeled view cells do not claim a standard first/third-angle arrangement;
sections are plane profiles, not cutaway projections. Drawings do not assign
GD&T, manufacturing tolerances, material or thread conventions. ASCII annotation
text, explicit regeneration, bounded one-sheet layouts and the documented
projection/export caps apply. Three-view sprocket HLR took 1.62 seconds in a
native probe; the helical knob exceeded a 30-second probe timeout. Complex
curved drawings may require `cad_job` with a longer budget, and can still time
out explicitly. No new Linux or Windows execution evidence is claimed. Existing
platform, host-interaction, signing and license gates remain open. Nothing was
uploaded or published. Next drawing extensions should follow actual user needs:
standard sheet arrangements, cutaway hatching, richer annotations or multi-sheet
layout, rather than promising manufacturing certification.

## Direct MCP demonstration — duplex #35 sprocket

The native MCP tools are now available directly in this chat. The user's next
request, “Build a double-strand #35 sprocket,” created saved document
`duplex_35_sprocket_20t`, revision 1, using the **installed** service's `cad_job`
→ `cad_create`, then `cad_open`, `cad_export` (STEP/STL) and `cad_read` tools.
No service implementation or installed bundle changes were needed. Its 65
ordinary native features form exact circular seating/working/topping surfaces,
tangent flanks, 20 repeated tooth spaces, two aligned rows, a hub and bore.

Assumptions: 20 teeth, 3/4-inch plain bore, type-B hub, 9.525 mm pitch,
5.08 mm bushing diameter, 4.1148 mm tooth-row width, 10.1346 mm transverse pitch,
49.2125 mm hub diameter and 34.925 mm overall length. Sources, tooth-form equations,
edit limits and complete structured intent are in
`examples/duplex-35-sprocket.prompt.md` and the paired `.create.json`.
The tooth count is encoded in instance layout/constants rather than an independent
parameter. No material, load rating or manufactured shaft fit is certified.

The native job committed in 1.464 seconds: one valid solid, 409 faces, 1,210 edges,
volume 64,181.266497 mm³. Independent native STEP read-back with optional healing
disabled retained one valid solid and volume, passed **812** bore/bushing/row/phase
point probes, and found **40/80/80** exact seat/working/topping cylindrical faces
at the expected radii. The STL's 5,264 triangles have two incident triangles per
each of 7,896 edges at 0.00001 mm vertex quantization. One-off verification source
and log are in `build/sprocket35/verify.cpp` and `validation.log`.

`cad_open` opened view `sprocket35_review`; subsequent `cad_context` returned the
current evaluation, non-stale revision 1 and host-published camera state. This is
new evidence of the actual host/App connection. It does not establish visual
GPU correctness or the complete selection-to-Quick-Edit acceptance loop.

## Local setup and threaded-knob acceptance — 2026-10-06

The user requested installation on this Mac and a real MCP demonstration:
“make a knob with a male 20mm coarse threaded end.” This increment adds the
missing `external_thread` primitive, installs the native bundle and packaged
`native-cad` skill, and registers the enabled `agent-3d-cad` STDIO server in the
user's Codex config. Existing config was backed up and all unrelated settings
were verified unchanged. The runtime workspace is `~/Documents/Agent3DCAD`.

`external_thread` builds an exact continuous cylindrical helix with nominal
60-degree flanks, P/8 crest, flat root, optional left/right handedness, and a
45-degree lead at +Z. Its bounded domain is pitch ≥0.1 mm, diameter ≤200 mm,
diameter/pitch 3–100, and 1–16 turns. It has no certified fit class or process
clearance. The writer now sets the pinned OCCT ToSTEP defaults explicitly
(`SplitCommonVertex` and `DirectFaces`), avoiding shared-actor flag leakage from
other writers without enabling import healing.

`examples/m20-knob.create.json` and its paired prompt specify M20×2.5 right-hand,
20 mm exposed stud, 45 mm nominal grip diameter, 18 mm grip height, eight finger
scallops, 1.2 mm top/bottom grip rounds, and 38 mm overall height. The stud
intersects the grip by 1 mm before fusion. Through the **installed native MCP
server**, the official SDK completed **18 tool calls**: create/read, STEP/STL
export, open/live mesh transfer, and a fresh-process reopen. Saved document
`m20_coarse_knob`, revision 1, has one valid solid, 53 faces, 148 edges and
volume 32,476.0042 mm³. STEP is 895,518 bytes; STL is 257,484 bytes. The STL's
5,148 triangles have two incident triangles at each of 7,722 shared edges using
0.00001 mm vertex quantization. Its STEP exceeds the current 512 KiB embedded
import cap; native exchange tests validate the complete example's round trip.

Final source verification: **16/16 CTests passed in 17.31 seconds**, with **1,353
C++ assertions plus CLI**, **17 installer checks**, 15 offline-renderer, 40 live
UI/state, 13 real-MCP live-loop and 42 WebGL checks. Schema conformance passed
**158 checks across 17 tools**; official MCP SDK 2.3.0 passed **249 checks**.
The modeling suite's 245 checks include exported RH/LH helical phase, actual
flank/crest/root probes, expression rebuild, full-knob STEP one-solid/volume,
and dimensional boundaries. M20×2.5×20 builds in approximately 0.63 seconds.
The verified relocation workflow runs with empty PATH. `git diff --check` passed.

The installer (`packaging/install-local.cmake`) checks every manifested file and
link before/after copying into a fresh versioned directory, preserving prior
versions. The final archive contains exactly 239 manifested files plus
provenance. A macOS CPack post-build step uses GNU tar serialization to avoid
unmanifested AppleDouble entries without changing source extended attributes.
Final preview: `build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`,
23,755,678 bytes, macOS 27.0+ arm64.
SHA-256: `49e790d5f176de7adad65cc6fb99dd6011302d7f2f22c368347b593d2573b9aa`.
Installed/archive provenance SHA-256:
`5b6c9b7e38c55e59ba3b4834b40373d1e2c9def09aa37418fb4f90d0a0d3c077`.
The installed directory is recorded in `build/knob-setup/install-result.json`;
setup details and complete MCP evidence are in `build/knob-setup/SETUP.md`,
`installed-mcp.log` and `installed-report/mcp-transcript.json`.

At the end of knob setup, this chat's tool catalog had not refreshed (the next
sprocket turn above confirms direct availability). Computer Use explicitly
prohibits controlling Codex itself; that boundary was not bypassed. A manual
MCP refresh was the remaining host step at that time.
The saved `m20_knob_review` view and mesh are ready, but real host/GPU and
selection/Quick Edit interaction remain unverified. Windows and other existing
release gates remain open. No remote repository or release was created.

## Latest increment — live create–view–select–edit loop

After comparing this project with `../text-to-cad`, the user authorized moving
to the next experience step. M4 adds a focused native-backed viewer rather than
the sibling's complete Python service/UI contract. No sibling code or runtime
was copied. Existing M0–M3 release gates remain open; this increment does not
claim full text-to-cad feature or host parity.

Implemented:

- `src/live.cpp`: five shared CLI/MCP tools (`cad_open`, `cad_show`, `cad_list`,
  `cad_context`, app-only `cad_viewer`) and durable workspace-scoped view state.
- `web/`: bundled WebGL2/WebGL1 viewport, model library, source feature tree,
  native measurements, selection, standard views, pan/orbit/zoom and Quick Edit.
- `src/mcp.cpp`, `cmake/EmbedViewerApp.cmake`: self-contained MCP App resource
  `ui://agent-3d-cad/viewer.html` with no network origins. CMake embeds exact UTF-8
  asset bytes in the executable. End users still need no Node or other tooling.
- Sync uses isolated mesh jobs, checks view generation plus HEAD before
  publication, freezes evaluation data, and transfers at most 128 KiB per chunk.
  Unchanged HEAD does not rebuild. Context validates pick resolution and rechecks
  identity under locks; stale context remains explicitly marked for inspection.
- The actual app bridge/controller follows edits automatically, preserves camera,
  clears outdated picks, restores matching saved context on reopen, and hides an
  unrelated old model while a document switch loads. Mesh identity is rechecked
  after transfer. Optional host context acknowledgments cannot block HEAD polling.
- Quick Edit publishes context then sends an explicit user message if supported.
  Optional PNGs require image-message support. Copy request has a selectable-text
  fallback. A message with uncertain delivery is never automatically resent.
- `skills/native-cad/SKILL.md` is packaged with the product; it teaches authoring,
  selection resolution, revision checks and reusing the same viewer. Validation
  with the skill-creator validator passed (temporary developer PyYAML 6.0.3).

Latest macOS arm64 shared-OCCT build: `build-app-protocol`. All **15 CTest suites
passed in 9.73 seconds**: **1,286 native checks plus CLI**, **15 offline renderer
checks**, **38 live bridge/state checks**, **13 real MCP live-loop checks**, and
**42 WebGL math/lifecycle/native-payload checks**. Independent schema validation
passed **158 checks across 17 tools** and official MCP SDK 2.3.0 passed **249
checks**, including resource discovery/read and UI metadata. Polling-dependent
counts may vary. Logs: `build-app-protocol/final-ctest.log`, `final-schema.log`,
`final-sdk.log`, and `Testing/Temporary/LastTest.log`.
The default shared build, `build-package`, was then rebuilt with the final assets
and passed the same 15 suites in **9.44 seconds**. `git diff --check` passed.
The final host-sizing correction adds debounced, deduplicated MCP App size
notifications and handles hosts declining fullscreen. Its focused bridge/state
suite passes **40 checks** (superseding 38 above), and the real MCP loop still
passes **13 checks**. This is covered by protocol tests, not real inline layout
evidence from a browser. The final archive information below names this version.

Final M4 macOS preview:
`build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`
(23,749,281 bytes, **macOS 27.0+ arm64**).
SHA-256: `f77e5454544089886359d407f146a17aaf435bfdbfe28fb18d1c83720a0d0d4a`.
Embedded app SHA-256:
`4a5c0c8e3779ffb1d34008657b0614e6ba0811735e9ebe87a17bd52f091a943d`.
The final resource/bridge/live-loop suites passed again (93/40/13 checks), and
relocation with empty PATH passed, including serving the exact embedded app.
All 224 provenance entries were checked against files inside the final archive.
The bundle includes 17-tool schemas, the guide, agent skill and live-plate example.
`build-app-protocol/RESULTS.md`, `final-app-tests.log`, `final-bundle.log` and
`final-package.log` retain evidence. The archive is local and unpublished.
The new live layer has not been run on Linux: Docker was stopped at the optional
follow-up probe and was not restarted. Earlier Linux evidence below covers M0–M3.

The real MCP loop starts the native executable and executes the actual app bridge
and state controller through a test host transport. It computes an unambiguous
visible edge from native tessellation, resolves a unique geometric selector,
commits a selective fillet, automatically receives revision 2, preserves camera,
clears the old pick, and proves invalid fillet rollback. This is **not a real
browser/GPU or Codex-host interaction test**. GPU lifecycle uses mocks; ray math
and native payload compatibility are real. Existing local-file browser policy
was not bypassed. No global host configuration was installed during that earlier M4 increment;
the local setup increment above subsequently registered the native server.

The M4 acceptance step is to connect this native service to a real MCP Apps host,
open `cad_open`, pick/send a Quick Edit, and observe an agent edit in the same
rendered view. See `docs/LIVE_VIEWER.md` and `examples/live-plate.create.json`.
Windows/x64 execution, signing/license decisions, automatic evaluation garbage
collection, assemblies, hide/isolate/clipping, recents/thumbnails and broader
fabrication formats are not established by this increment.

The older M0–M3 evidence and archives below are retained as historical evidence;
they do not contain the M4 viewer unless a newer artifact is explicitly named.

## Scope and architecture

The user requested completion of the spec, explicitly including M3 modeling and
Windows, and authorized parallel sub-agents. Four workstreams implemented
kernel/model, jobs/storage, viewer/service integration, and packaging/CI.

- C++20 service with exact checksum-pinned OpenCascade 8.0.1.
- Closed structured intent, persistent feature IDs, ordered replay, finite values.
- Shared `Service` for CLI and MCP; seventeen tools with input/output JSON Schemas.
- Kernel work is serial inside isolated native workers; coordinator publishes.
- Per-document writer lock checks revision both before work and before publication.
- Immutable revisions include optional durable request receipts; HEAD is atomic.
- No Python/Rust/Node/compiler required in the end-user modeling path.
- No HTTP endpoint or public release. The user-created GitHub repository is
  recorded above. This Mac now has a
  local global MCP registration for the native preview.

## Implemented behavior

| Area | Files | Behavior |
|---|---|---|
| Model contract | `src/model.cpp` | Closed schemas, unit-checked bounded arithmetic, dependencies, edit batches |
| Geometry | `src/kernel.cpp` | Primitives, exact helical external threads, booleans, selective fillets, numeric sketches/workplanes, extrusion/revolve/loft/sweep, rigid transforms, holes, patterns, reusable instances |
| STEP import | Model/kernel/service | Embedded content ≤512 KiB and verified SHA-256, unit conversion to mm, rebuild independent of original file |
| Topology | Kernel/service | Evaluation-scoped face/edge measurements, unique selector suggestions, explicit cardinality errors and bounded OCCT history evidence |
| Mesh/viewer | Kernel, `src/viewer.cpp` | Same-evaluation B-rep face/triangle mapping and edge polylines, offline HTML with picks and revision loading |
| Live viewer | `src/live.cpp`, `web/`, MCP resource | Library, feature tree, WebGL picking, automatic HEAD refresh, validated context and host Quick Edit |
| Editing utilities | Service | Candidate preview, historical comparison, restore-as-new-revision, stale/draft selection rejection |
| Persistence | `src/storage.cpp` | POSIX and Windows file/lock implementations, per-document conflict control, durable deduplication receipts |
| Workers/jobs | `src/jobs.cpp` | Native spawning, queue admission, cancellation, wall/memory budgets, recoverable durable jobs, bounded result files |
| Distribution | `cmake/`, `packaging/` | Relative library paths, native closure, OCCT resources, notices, schema publication, provenance, archives and relocation checks |
| CI | `.github/workflows/ci.yml` | Native macOS arm64/x64, Linux x64/arm64, Windows x64 lanes plus Linux runtime-only container gate |

The original acceptance requirements remain in `ROADMAP.md`. Implementation
status does not waive their unexecuted platform and human-interaction checks.

Historical M0–M4 requirement audit (newer acceptance evidence is recorded above):

| Gate | Demonstrated | Outstanding |
|---|---|---|
| M0 editable backend | Real OCCT geometry, transactions, CLI and MCP suites on macOS arm64 and Linux arm64 | Other platform runs tracked below |
| M1 selection/viewer | Persisted evaluated picks, selective edits, ambiguous replay rejection, draft previews, offline artifacts and pure renderer tests | Human/browser pick, reference handoff and updated-view interaction |
| M2 jobs/distribution | Worker failure/cancel/deadline/memory handling, durable retries, old-revision reads, independent MCP SDK, relocated Mac/Linux bundles and fresh Linux runtime-only workflow | Remaining platform bundles |
| M3 modeling/portability | All scoped features, immutable imports, section/copy lineage, two representative parts edited/reopened with analytic checks | Native Windows persistence/geometry and remaining architecture runs |
| M4 integrated experience | Native live state, actual bridge/controller over real MCP, selection-to-fillet refresh, camera retention, stale/failed edit handling, self-contained resource | Real MCP Apps host and GPU interaction |

## Validation evidence

Local host: macOS arm64, AppleClang 21.0.0, exact OCCT 8.0.1, JSON 3.12.0,
FreeType 2.14.3. The convenience static-SDK build passed all nine CTest suites.
The complete pinned dependency recipe was also built and installed from source,
including **shared** OCCT and standalone shared FreeType with optional codecs
turned off. A service linked to that freshly built SDK passed all nine suites
in the final run (7.66 seconds): **1,118 explicit native checks plus the CLI
process integration suite**.

Native suites:

- `model`: closed fields, dimensions, graph/parameter/edit validation.
- `geometry`: analytic volumes, booleans, rounded four-hole plate, STEP read-back.
- `transactions`: failed-build rollback, historical reopen, stale revisions,
  orphan candidates, cross-process writer exclusion and concurrent reads.
- `protocol`: MCP initialization, discovery, structured errors, notifications,
  malformed frame recovery and JSON-only stdout.
- `cli_smoke`: complete plate create/edit/reopen/export across native processes.
- `topology`: 783 checks including selected-edge analytic fillets, unique/missing/
  ambiguous rules, unchanged-reference evolution, exact mesh mappings, limits,
  valid OCCT history targets, and per-instance pattern lineage checked against
  translated geometry and matching mesh IDs.
- `modeling`: 178 checks including arbitrary planes, clockwise/self-intersecting
  profiles, signed extrusion, partial/full revolve, loft, sweep, transform,
  repeated solids, holes, expression unit/node/depth bounds, STEP hashes,
  source-unit conversion, Unicode STEP/STL filenames and rejection of STEP with
  a valid solid plus loose surfaces. Every solid must have closed shells and
  positive finite volume; optional STEP healing is disabled. Ruled and smooth
  loft histories identify generated side faces without mutating source sections.
- `jobs`: 42 checks including real worker cancellation/kill/deadline/memory
  failure with HEAD preservation, live MCP ping/read/cancel during a build,
  four worker slots/eight active-job admission, path symlink rejection, and
  recovery before/after commit without duplicate publication.
- `viewer`: 46 checks for stored picks after restart, wrong/stale/draft identity rejection,
  preview without mutation, HTML/data identity, safe embedded JSON, comparisons,
  request deduplication and schema discovery; resolved picks drive a real selected
  fillet and updated view; ambiguous replay preserves HEAD; Unicode workspace
  paths and evaluation metadata exceeding 1 MiB round-trip correctly. Summary
  queries include the measured feature ID for both output and intermediate shapes.

Independent `tests/schema_conformance.py` used jsonschema 4.25.1 (developer-only)
to validate all twelve tool input/output schemas and real CLI/MCP responses:
**120 checks passed** in the final run (polling count can vary). It authors, edits,
reopens and exports the bracket and nozzle examples, checking changed geometry
against analytic volumes and preserving historical revisions. It also rebuilds
an import after deleting its original STEP and restores history. Only documented
transient `workspace_busy` responses are retried with a bounded deadline.
Python is not used by native CTest or included in the product bundle.

`tests/mcp_sdk_smoke.py` passed **211 interoperability checks** using the official
MCP Python SDK 2.3.0 against the real native stdio subprocess. The independent
client negotiated protocol 2025-11-25 in auto and legacy modes, discovered and
validated all twelve tools, edited/exported models, inspected structured errors,
pinged/read during active native work, cancelled it, and reopened durable history,
request receipts and job results. This establishes SDK interoperability, not a
GUI-host attachment; no global MCP registration was installed. Dependencies are
pinned in `tests/mcp_sdk_requirements.txt` and are development-only.

`node tests/viewer_renderer_tests.js` passed **15 pure renderer checks**: nearest
depth selection, hidden edges, coincident-geometry ambiguity, degenerate triangles,
topology mappings, payload bounds and viewport clipping. Node is a developer-only
test dependency. The viewer uses a depth buffer and bounded raster work; these
tests do not claim real browser-interaction evidence.

A relocated shared-library bundle passed create/edit/reopen/query/STEP/STL/MCP
with an empty PATH and SDK/loader variables removed. Every bundled Mach-O
executable/library was audited for non-system absolute load paths. This is
relocation evidence on the development Mac, not a fresh-machine claim.

Linux arm64 validation ran natively under Docker Desktop's Linux aarch64 VM in
Ubuntu 24.04 with GCC 13.3.0, using source-built pinned OCCT 8.0.1 and FreeType
2.14.3. The final run passed **all nine CTest suites (1,118 checks plus CLI) in
4.97 seconds**, **118
schema checks**, **181 official SDK checks**, and **15 renderer checks**. Polling
counts vary; the earlier successful Linux run had 120/186 schema/SDK checks.
The relocated bundle passed with an empty PATH. A separate fresh Ubuntu 24.04
runtime image passed create/edit/reopen, failed-edit rollback, saved views, durable
jobs/retry, STEP/STL export and MCP in 0.7 seconds. The runtime script asserts
that Python, Rust, Node, CMake and compilers are absent. `ldd` confirmed every
non-glibc dependency came from the bundle, including the pinned FreeType.

Linux validation exposed and fixed three distribution issues: Ninja needed
FreeType declared as an OCCT ExternalProject dependency; OCCT's Linux font code
needed Fontconfig headers; installed SDK libraries needed `$ORIGIN` RPATH for
transitive dependency lookup. The bundle includes Fontconfig/Expat and their exact
distribution notices/version record. Its runtime scan retains the configured
FreeType instead of traversing an unused system duplicate. GCC notices follow
the actual bundled runtime package version, which can differ from the compiler.
These fixes passed the full source build and final runtime gate without OCCT
source patches or loader-environment workarounds.

The browser tool refused `file://` navigation to the viewer under its URL policy.
No browser workaround was attempted. HTML generation and saved pick behavior are
tested; interactive rendering/picking remains unverified by a real browser here.

## Reproduce

Convenience local build (ignored preset/SDK, not a repository requirement):

```sh
cmake --preset local
cmake --build --preset local
ctest --preset local
```

Fresh dependency and portable bundle commands are in `README.md` and
`docs/DISTRIBUTION.md`. Current fully built shared dependency workspace:
`build-deps-packaging`; service build: `build-package`.

```sh
cmake --build build-package --parallel 4
ctest --test-dir build-package --output-on-failure
cmake --build build-package --target bundle-check
cpack --config build-package/CPackConfig.cmake -C Release -B build-package/packages
```

Optional independent contract/client checks (install
`tests/mcp_sdk_requirements.txt` into a development-only virtual environment):

```sh
python3 tests/schema_conformance.py build-package/agent-3d-cad
python3 tests/mcp_sdk_smoke.py build-package/agent-3d-cad
```

Local preview artifact:
`build-package/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`.
SHA-256: `88f38e1405a26a650c79db2117f1bfabfd0b8b2f858b88fece5f59e4715a4b27`.
It inherits this SDK's **macOS 27.0 minimum**; see packaged provenance. Do not
claim it supports older macOS versions. CI builds use their own explicit baseline.

Linux preview artifact (tested Ubuntu 24.04 aarch64, glibc 2.39):
`build-linux-validation/agent-3d-cad-0.1.0-Linux-aarch64.tar.gz`.
SHA-256: `4fac3f62efc789c5d4bf6ac8aca50695b7fce7321446e34996522ed46a4ad4df`.
`build-linux-validation/RESULTS.md`, `validation.log`, `LastTest.log`,
`final-loader-audit.log` and `provenance.json` retain the exact commands, results,
runtime lookup evidence and packaged hashes. No archive has been published.

## Remaining acceptance gates and limits

Native platform validation passed all five CI lanes, including Windows x64
relocation and preview packaging. See the latest increment for exact results and
archive hashes. Remaining independent gates:

1. In a normal browser, open a generated HTML artifact, pick a face/edge, copy or
   save its reference, resolve it through the service, make a selective edit,
   and load the new `.view.json`. The UI includes these controls; real interaction
   still needs evidence because of the browser tool's local-file policy.
   A simple local demo is ready in `build/manual-review`, document `pick_demo`,
   revision 1. Its HTML is
   `build/manual-review/exports/pick_demo-eval_55b5a63bd8cd6fba5d77016be1dea535.html`.
   The pending user request is to select an edge and paste its copied reference;
   then resolve it, add a selective fillet, and verify the updated view manually.
2. Before a public release, the owner must choose the original-code license and
   arrange signing/notarization and applicable source/relinking distribution.
   The preview archive records the pending license decision; it is not a release.

Other deliberate limits:

- Numeric profiles are not a sketch constraint solver. No assemblies/joints,
  full drafting/GD&T, manufacturing certification, general scripts or remote multi-tenancy.
- Geometric selectors encode intent; indices and OCCT history are not universal
  persistent topology names. Face picks are inspectable; editing currently uses
  edge selectors. A changed cardinality fails explicitly.
- Every revision/query rebuilds intent; there is no persistent B-rep cache.
  Stored evaluations and viewer files may be removed only if their picks are no
  longer needed. No automatic evaluation/artifact garbage collector is present.
- Viewer/evaluation artifacts permit 64 MiB; model/transport input stays 1 MiB.
  Viewer rendering is bounded and can require narrowing to a feature for a very
  complex model. Occluded geometry is not selected through a visible face.
- macOS memory enforcement samples physical footprint every 10 ms and can
  overshoot between samples. Linux address-space limits and Windows Job Object
  limits have different memory accounting. All platforms have worker deadlines.
- Synchronous geometry calls wait for workers; use cad_job for a responsive MCP
  connection during work. Jobs report coarse phases, not estimated percentages.
- Workspace files are trusted local storage, not hostile-user or NFS sandboxing.
  Abrupt exits can leave ignored temporary worker/staging files. Request receipts
  reconcile commits; no filesystem durability rollback is promised.

Next authorized product work is drawing layouts/hatching/angular dimensions/
explicit tolerances, then performance/caching and assemblies. Native platform
validation has passed. The live Codex viewer loop has real rendering/selection/
refresh evidence on macOS; other GPU hosts and offline artifact interactions
retain their own independent gates.

## Direct MCP model — 13-tooth duplex #35, keyed 5/8-inch bore

On 2026-10-06, the installed native service created
`duplex_35_sprocket_13t_keyed`, revision 1, for the user's explicit 13-tooth,
5/8-inch bore and keyway request. Its 53 supported features create two aligned
13-tooth rows with exact seat/working/topping arcs, a type-B hub and a through
3/16-inch keyway. Complete intent, sources, dimensions and edit limits are in
`examples/duplex-35-sprocket-13t-keyed.create.json` and its `.prompt.md`.

Native validation reports one valid solid, 273 faces, 802 edges and volume
17,888.61949253724 mm3. Independent OCCT 8.0.1 STEP read-back with healing disabled
retained one valid solid and volume within 0.01 mm3. All 568 clearance/material
probes passed, including both tooth rows, bushing seating, phase, inter-row
clearance, bore and keyway walls. Exact cylindrical seat/working/topping face
counts are 26/52/52. STL validation found 3,512 triangles and 5,268 edges, each
with two incident triangles at 0.00001 mm quantization. Export did not change
editable intent. Verification source and evidence: `build/sprocket35-13t/`.
The actual Codex MCP App was opened once as `sprocket35_13t_keyed_01a113a0`;
DOM and GPU screenshot inspection confirmed revision 1 and the two rows/keyway.

The first create job was rejected for duplicate `tooth_row_cut_13` identity;
no document was published. Corrected job `duplex35_13t_keyed_20261006_v2`
succeeded. STEP/STL are saved in the native workspace's exports directory.
No service implementation, installed bundle or pre-existing model changed.
Hub dimensions are documented assumptions; manufacturing fit/material/load rating
remain unspecified. Existing platform-validation next tasks above are unchanged.

### Follow-up — selected hub edge rounded to 1 mm

The user's MCP App Quick Edit selected revision-1 `edge-787`, the circular
outside edge of the projecting hub at Z=-17.5006 mm, and requested a 1 mm round.
`cad_read`, `cad_context` and `cad_resolve_selection` confirmed current revision 1
and one unique circular edge. `cad_apply(expected_revision=1)` added parameter
`hub_edge_radius=1`, the `rounded_hub_edge` fillet and output change, committing
revision 2. The edit recipe is
`examples/duplex-35-sprocket-13t-keyed.edit.json`.

Revision 2 is one valid solid, 274 faces, 804 edges, volume
17,869.923224229355 mm3. Topology confirms the new toroidal face spans exactly
Z=-17.5006 to -16.5006 mm. The existing live viewer followed revision 2, cleared
the old selection and displayed the rounded outer hub edge; its DOM/screenshot
were inspected. Revision-qualified STEP/STL exports succeeded as `-r2.step` and
`-r2.stl`. No additional manufacturing fit or load validation is claimed.

### Drawing export — 13-tooth keyed sprocket revision 2

The user requested drawings of the current sprocket. Installed `cad_job` /
`cad_drawing` job `sprocket35_13t_keyed_drawing_r2_v1` generated an A3 landscape
sheet at 2:1 with top/front/right/isometric views and a true hub section at
Z=-5 mm. PDF/SVG and five 1:1 mm DXFs are in the native workspace export folder
`duplex_35_sprocket_13t_keyed-r2-drawing-eval_93a432aeb7e1e6b8ad2bd3e4d5d36d85`.
The reusable request is `examples/duplex-35-sprocket-13t-keyed.drawing.json`.

All eight requested dimensions resolved to actual geometry: OD 44.45, overall
length 31.75, row spacing 10.1346, row width 4.1148, hub diameter 28.178125,
bore 15.875, keyway width 4.7625 and opposite bore wall-to-keyway-roof 17.8816 mm.
Poppler rendered the single-page PDF for visual inspection; pypdf confirmed all
eight printed dimension labels and revision 2. All five DXFs passed independent
ezdxf audit with zero errors/fixes and millimeter units. An initial validation
process loaded a Python 3.14 NumPy into Python 3.12; reusing the bundled compatible
NumPy corrected the verifier environment without changing artifacts. Native
source read-back remained byte-equivalent JSON to its pre-export response.
Review evidence and the complete 132,638-byte ZIP are under
`build/sprocket35-13t/`; no model revision or service implementation changed.
