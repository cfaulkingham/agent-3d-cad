# Implementation handoff

Updated: 2026-10-06. **M0–M3, M4 live viewing, and M5 drawings are native previews.**
Actual Codex-host rendering and select–edit–refresh are demonstrated on macOS
arm64. Full cross-platform release is not yet demonstrated.

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
The existing five-platform CI definition is ready but has no remote repository
or runner execution. No source was uploaded and no repository/release created.
Read-only GitHub checks confirmed the signed-in account `cfaulkingham`; the
proposed `cfaulkingham/agent-3d-cad` repository still does not resolve.
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
- No HTTP endpoint, remote repository or public release. This Mac now has a
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

Final requirement audit:

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

1. Execute native Windows, Linux x64 and macOS x64 CI. Definitions exist,
   but this project has no remote repository or configured runner execution.
   GitHub CLI authentication was verified outside the filesystem/network sandbox;
   the active account is `cfaulkingham`, and `cfaulkingham/agent-3d-cad` does not
   currently resolve. A pending user approval asks to create that **private**
   repository, upload this reviewed implementation, and run the existing CI
   matrix, or to use another destination the user specifies. No repository has
   been created, no source uploaded, and no global authentication state changed.
   Windows locking/spawning/publication code has been implemented and reviewed;
   it has not been compiled/executed on a native Windows host here. A read-only
   `arch -x86_64 /usr/bin/uname -m` probe failed with `Bad CPU type in executable`,
   so this Mac also cannot validate an Intel binary through existing Rosetta.
   No OS component was installed to change that result.
2. In a normal browser, open a generated HTML artifact, pick a face/edge, copy or
   save its reference, resolve it through the service, make a selective edit,
   and load the new `.view.json`. The UI includes these controls; real interaction
   still needs evidence because of the browser tool's local-file policy.
   A simple local demo is ready in `build/manual-review`, document `pick_demo`,
   revision 1. Its HTML is
   `build/manual-review/exports/pick_demo-eval_55b5a63bd8cd6fba5d77016be1dea535.html`.
   The pending user request is to select an edge and paste its copied reference;
   then resolve it, add a selective fillet, and verify the updated view manually.
3. Before a public release, the owner must choose the original-code license and
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

Next work is native Windows x64 and remaining architecture execution using the
existing CI matrix. The live Codex viewer loop now has real rendering/selection/
refresh evidence; other-host and offline artifact interactions retain their own
gates. Resolve concrete failures before expanding modeling or viewer scope.
