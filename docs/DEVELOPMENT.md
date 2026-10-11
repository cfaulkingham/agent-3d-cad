# Development and native workflows

Run these commands from the repository root. For installation and everyday use,
see the [README](../README.md) and [client setup](GETTING_STARTED.md).

Use `cad_fabrication_review` with explicit machine/process limits for measured
source-part checks and saved-pose clearance/interference. The new
`nested-assembly.review.json` example uses illustrative fixture limits.
[FABRICATION_REVIEW.md](FABRICATION_REVIEW.md) defines coverage and unknowns;
`cad_manufacture.options.fabrication_review` includes its hashed report.

Use `cad_gcode_review` for an existing checksummed plain G-code artifact with
explicit machine/material/initial state. Native static inspection preserves
original bytes, failed/unknown findings and a portable hash ledger. It supports
`cad_job` cancellation/replay and does not require a slicer or language runtime.
[GCODE_REVIEW.md](GCODE_REVIEW.md) defines its subset, assumptions and bounds;
the native `gcode` suite tests analytic paths and package/job behavior.

Use `cad_slice` to plan and run one committed source solid through an explicitly
installed OrcaSlicer 2.4.2. Supply raw executable/profile hashes, self-contained
compatible Marlin FFF profiles, High Temp Plate and explicit review assumptions;
then run the returned plan by its raw hash. [SLICING.md](SLICING.md) describes
fixed argv, exact-source numerical verification, effective settings, bounded
processes and portable publication. It never contacts a printer. The native
`slicer` suite uses an actual native process-control fixture, not a simulated
slicing-accuracy claim. To include its plan/run contracts in independent schema
and SDK tests, pass `build/cad_slicer_fixture` (Windows: `.exe`) as the optional
second argument after the service executable. Actual installed Orca integration
has separate evidence in HANDOFF; core CAD requires neither Orca nor Python.

For sourced STEP geometry, `cad_import` accepts `expected_sha256` and optional
`purchase`, binding supplier/part/source identity to unchanged raw file bytes.
Unchanged imported sources and rigid copies retain that identity in BOMs and
manufacturing packages, including an original `source.step`. See
[PURCHASED_PARTS.md](PURCHASED_PARTS.md); the washer example needs a downloaded
local STEP path, while native regression tests create isolated local fixtures.

## Build and test

Development needs CMake 3.24+ and a C++20 compiler. Build checksum-pinned native
dependencies (OCCT 8.0.1 and FreeType 2.14.3):

```sh
cmake -S cmake/dependencies -B build/dependencies \
  -DAGENTCAD_DEPS_PREFIX="$PWD/.deps/occt" -DAGENTCAD_JOBS=4
cmake --build build/dependencies --parallel 4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOpenCASCADE_DIR="$PWD/.deps/occt/lib/cmake/opencascade" \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/occt"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

An existing exact exception-enabled OCCT SDK can be selected with OpenCASCADE_DIR.
Use its prefix for CMAKE_PREFIX_PATH as well. When switching SDKs in an existing
build directory, clear cached FreeType locations with `cmake -U 'FREETYPE_*'`
and the same configure arguments, or use a fresh build directory; an older SDK
library directory on the loader search path can override the selected OCCT.
Use the supplied dependency recipe's HLR midpoint, root streaming and grid reuse
patch for the cold-drawing optimization and current performance geometry regressions; an unmodified SDK
retains the previous behavior. The patch and modified source accompany bundles.
CMake fetches checksum-pinned nlohmann JSON 3.12.0 when not installed. Offline
build overrides and bundle instructions are in [DISTRIBUTION.md](DISTRIBUTION.md).
Local convenience presets and SDKs are ignored and are not repository requirements.

Use `build/` for the current native build. If an existing patched SDK is already
installed at a different prefix, select both its OCCT config and library prefix:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOpenCASCADE_DIR="$PWD/.deps/hlr-streaming-sdk/lib/cmake/opencascade" \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/hlr-streaming-sdk" \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$PWD/.deps/json-3.12.0"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

This example reuses local dependency installs; a fresh checkout uses the recipe
above. Keep packages and dependency-build output under `build/packages/` and
`build/dependencies/`. Store CAD workspaces under `.local/workspaces/` and
evidence worth keeping under `.local/evidence/`, outside disposable build trees.
See the [repository guide](README.md) for cleanup and the archived local evidence
mapping. User projects still use their independently configured Documents folder.

## Try a complete workflow

With the MCP server connected to an MCP Apps host, create a model and call
`cad_open` with its `document_id` and a workspace-unique `view_id`. Keep that view
open while using `cad_apply`; it updates automatically. `cad_context` reads the
user's current selection and camera, and `cad_show` switches the document in the
same view. The bundled [native-cad skill](../skills/native-cad/SKILL.md) guides this
agent workflow. The CLI/offline workflow below remains available.

The live viewer also persists uncapped clipping and exploded inspection through
the same native context actions. Native live tests cover validation, source
immutability, revisions and restart; renderer/controller tests cover clipped
occlusion, shared vertex ownership, graphics recovery and delayed context writes.
See [PRESENTATION.md](PRESENTATION.md) and HANDOFF for actual browser evidence.

Use `cad_measure` with a current committed topology/mesh evaluation for exact
face/edge/leaf-pair distances and bounded assembly clearance queries. The live
Exact measurement panel submits native jobs and retains source-pose results
while clipping, hiding or exploding parts. [MEASUREMENTS.md](MEASUREMENTS.md)
defines qualification, analytic angles, witnesses and material-interference
semantics. The native `measurement` suite covers analytic geometry, cold-cache
reference recovery, deadlines/cancellation, revision retirement and job replay.

Use a fresh workspace:

```sh
build/agent-3d-cad --version
build/agent-3d-cad call cad_create --workspace .local/workspaces/plate --input examples/plate.create.json
build/agent-3d-cad call cad_apply --workspace .local/workspaces/plate --input examples/plate.edit.json
build/agent-3d-cad call cad_view --workspace .local/workspaces/plate --input examples/plate.query.json
build/agent-3d-cad call cad_query --workspace .local/workspaces/plate --input examples/plate.query.json
build/agent-3d-cad call cad_export --workspace .local/workspaces/plate --input examples/plate.export.json
```

Open the returned HTML path in a browser. Orbit/zoom, pick a face or edge, and
copy/save the reference for `cad_resolve_selection`. A `.view.json` can be loaded
into the same viewer to review an updated revision. Both artifacts work offline.
A pick belongs to its document, revision and evaluation; persistent edits use
geometric selectors with explicit expected cardinality. Failed or ambiguous edits
preserve HEAD. Use `cad_preview` to inspect edits without committing.

The plate example changes thickness and hole spacing. Additional representative
models are `examples/bracket.create.json` (numeric profile, extrusion, holes,
parameter expression) and `examples/nozzle.create.json` (loft and bore).
`examples/m20-knob.create.json` builds a fluted knob and a nominal right-handed
M20×2.5 stud with the `external_thread` feature's exact helical geometry. Its
paired `.prompt.md` records the design assumptions and thread-fit limits.

CLI `--input -` reads JSON from stdin. Errors are JSON on stderr with exit code 1.
`tools` publishes full input/output schemas. The [protocol](PROTOCOL.md)
describes feature and selection contracts, limits, errors and persistence.

An `assembly` feature gives reusable source solids distinct part IDs and world
placements. Rigid mates align explicit parent/child datum frames with editable
offsets and angles; changing source parameters rebuilds every affected instance.
Assembly geometry stays separate even where parts touch or overlap. Use
`set_part_placement`, `set_mate`, and `remove_mate` for atomic arrangement edits,
and view-specific part offsets for exploded drawings. See
[ASSEMBLIES.md](ASSEMBLIES.md) for a complete editable example, transform
conventions, inspection fields and bounded nested composition. The
`nested-assembly.create.json` example reuses an articulated subassembly twice.
Source-keyed BOM metadata supplies optional item/part numbers, descriptions and
materials; quantities count instances. `cad_bom` exports revision-qualified
JSON/CSV. Drawing `bom: true` adds the table and optional visible-surface balloons.
See `examples/assembly-bom.drawing.json` for an assembled/exploded sheet.

For moving assemblies, `examples/articulated-arm.create.json` exercises revolute,
slider and cylindrical mates, coupled coordinates and named poses. The embedded
Motion panel previews, resets and saves poses. `examples/articulated-arm.robot.json`
exports URDF with paired SRDF and source meshes; its physical limits are illustrative
test values. [Robot export](ROBOT_EXPORT.md) documents SI coordinate conversion,
SDF inertial requirements and consumer-validation limits.

`examples/section-inspection.create.json` reuses two bored housings on separate
bases. Open its live view, enable a Z clipping plane through the housings, and
calculate the exact section. A plane strictly between Z=0 and Z=20 has two
annular regions with total area `216π mm²`; the bases remain outside that plane.
The native report retains each bore. Section surfaces are read-only review
geometry; original faces remain selectable through the openings. See
[SECTIONS.md](SECTIONS.md). The native `section`, `live`, `app_protocol`,
`live_ui` and `webgl_renderer` suites cover the service and view coordination.

Repeated queries, exports and drawings automatically reuse exact geometry and
projected drawing views in a bounded workspace cache. Parameter edits rebuild
only affected feature dependencies. Drawing layout, dimensions, tolerances and
formats can change without repeating projection; unrelated branches do not
invalidate the output's projections. Native build changes invalidate derived
entries; removing the cache loses no editable work.
Complex first-time threaded drawings may still need `cad_job` with a longer
timeout. Developer benchmark: `python3 tests/cache_benchmark.py
build/agent-3d-cad build/cache-benchmark` (isolated workspaces; two hatched sections
of the full M20 knob, cold/warm/style-change and geometry-only timings). Optional
`--views standard` exercises the much slower four-view hidden-line drawing.
Use `--cold-only` to measure the first drawing alone, and `--timeout-ms` to set
its bounded job budget. Failed jobs retain timings and errors in the report.

The native dependency benchmark builds a panel with 128 holes and two editable
adapter occurrences, then compares three adapter-width edits against independent
cold workspaces. It verifies matching geometry and records per-feature reuse,
timings, native/kernel identity and the executable hash:

```sh
build/cad_dependency_cache_benchmark build/dependency-cache-benchmark
```

This is a developer executable, excluded from runtime bundles. Timing is evidence,
not a flaky test threshold. See [DEPENDENCY_CACHE.md](DEPENDENCY_CACHE.md).

## Jobs and retries

Use `cad_job` for asynchronous geometry so MCP remains responsive during builds:

```json
{"action":"submit","request_id":"plate_view_2","tool":"cad_view",
 "arguments":{"document_id":"plate","revision":2},
 "budget":{"timeout_ms":30000,"memory_mb":2048}}
```

Poll with `{"action":"get","job_id":"plate_view_2"}`; cancel with action
`cancel`. Jobs survive client exits. Kernel work runs in isolated native workers;
publication rechecks the revision in the coordinator. Mutation request IDs and
durable receipts reconcile a lost reply without committing twice. The protocol
specifies cancellation races, recovery and platform memory-limit behavior.

## MCP

Configure a local stdio server with absolute executable and workspace paths:

```json
{
  "mcpServers": {
    "agent-3d-cad": {
      "command": "/absolute/path/to/bundle/bin/agent-3d-cad",
      "args": ["serve", "--workspace", "/absolute/path/to/cad-workspace"]
    }
  }
}
```

Baseline MCP `2025-11-25`: initialize/initialized, ping, tools/list and tools/call.
Twenty-nine application tools expose create/read/apply/restore/import,
query/measurement/export/BOM/drawing/manufacturing/process review, G-code review,
slicing/printer plans, robot handoff, external artifact review, view/preview/
selection resolution, comparison, jobs, model discovery and live view context.
`cad_open` attaches the bundled MCP App resource; `cad_viewer` is an
app-only transport tool. This is a local stdio service with no HTTP endpoint.
No global host configuration is installed for you.

`cad_drawing` creates revision-qualified vector drawings in isolated native
workers, with aligned first-/third-angle layouts, hatched planar sections,
hidden-line views, geometry-checked linear/angular dimensions and explicit tolerances,
PDF/SVG sheets, and one 1:1 mm DXF per view. It saves a reusable recipe; replay it
against a later committed revision to regenerate. See [DRAWINGS.md](DRAWINGS.md)
and `examples/plate-section.drawing.json`. The paired `angular-plate` examples
demonstrate angular dimensions and tolerance styles. Drawing generation requires
no extra runtime.

For a verified versioned local installation and explicit Codex registration, see
the [installation instructions](DISTRIBUTION.md#install-use-and-uninstall).

The optional developer interoperability check uses the pinned official MCP Python
SDK in `tests/mcp_sdk_requirements.txt`. Install those requirements in a virtual
environment, then run `python tests/mcp_sdk_smoke.py /absolute/path/to/agent-3d-cad`.
It exercises discovery, schema validation, edits, exports, structured errors,
responsive jobs and restart through an independent client. Each independent SDK
lifecycle (auto negotiation and reopened legacy session) has a 90-second deadline,
including process launch and teardown; the two together have a 180-second aggregate
deadline. Tool calls retain their 15-second limit and the native worker budget is
unchanged. Stage diagnostics distinguish elapsed time, Python CPU, schema checking
and completed assertions. Run `python tests/mcp_sdk_deadline_tests.py` to check
lifecycle cancellation and error attribution. Python is not part of the native
distribution.

For a final discovery inlining change, retain the native tool catalog immediately
before that pass and compare it with the final catalog using
`python tests/discovery_compaction_check.py before-tools.json after-tools.json --source .`
in the same Python environment. This independently expands removed definitions,
checks every schema and reference, and compiles the exact source pass privately
to test literal, recursive, scoped and pointer-reference safeguards. Both catalogs
must have the same tool contracts and alias names. JSON headers are found in the
local pinned dependency or CMake FetchContent directories; use
`--json-include /path/to/include` for another nlohmann JSON 3.12.0 installation.

For union/schema normalization changes, run
`python tests/discovery_hoist_check.py --source .` in that environment. It compiles
exact production statements privately and compares independent Draft 2020-12
acceptance, including closed field sets, cardinality, mandatory discriminators,
reference/literal/annotation safeguards and schema-keyword property names.
For the final local-anchor pass, run
`python tests/discovery_anchor_check.py --source . --catalog before-tools.json`.
It compiles the exact production pass and verifies deterministic, reversible
reference shortening against independent Draft 2020-12 acceptance, including
recursive graphs, pointer suffixes, reference siblings, scopes and literal data.
The optional catalog is the native catalog immediately before anchoring; use
that pre-anchor catalog as the final input to the inlining check above.
`python tests/modeling_gap_schema_tests.py /absolute/path/to/agent-3d-cad` validates
actual combined modeling calls, saved records, sheet reports, surface outputs and
live face-selection context.
`python tests/authoring_schema_tests.py /absolute/path/to/agent-3d-cad` exercises
captured sources and jobs. These remain
developer tests; the native product has no Python dependency.

External-artifact acceptance uses `tests/artifact_mcp_sdk_smoke.py` with the same
official SDK environment. After the `artifact_mcp_flow` CTest produces
`build/artifact-flow-evidence.json`, run
`python tests/artifact_schema_tests.py build/artifact-flow-evidence.json` for
independent Draft 2020-12 validation of actual artifact calls and results.

## Bundle

```sh
cmake --install build --prefix "$PWD/build/bundle"
```

The install tree includes native libraries, OCCT resources, dependency provenance,
and third-party notices with relative library paths. The distribution guide
covers relocation tests, clean runtime containers, installation and removal.
CI definitions cover macOS arm64/x64, Linux x64/arm64 and Windows x64; a configured
CI lane is not evidence that it has run. Release signing and
notarization remain owner decisions.

## Source map

| Location | Responsibility |
|---|---|
| `src/model.cpp` | Closed schemas, units/expressions, dependencies and semantic edits |
| `src/component.cpp` | Pinned source snapshots, dependency materialization, bindings and local-edit status |
| `src/kernel.cpp` | OCCT shapes, history, selectors, mesh mapping, exchange |
| `src/storage.cpp` | Native locking, atomic revisions, durable request receipts |
| `src/jobs.cpp` | Process budgets, admission, cancellation and recovery |
| `src/service.cpp` | Shared CLI/MCP use cases and tool contracts |
| `src/viewer.cpp` | Standalone offline browser viewer |
| `src/drawing.cpp` | Drawing recipes, measured annotations, native PDF/SVG/DXF output |
| `src/mcp.cpp`, `src/main.cpp` | Stdio protocol and CLI adapters |
| `tests/` | Real geometry, transactions, topology, jobs, viewer and process tests |
| `cmake/`, `packaging/` | Reproducible dependencies and native bundles |

See [SPEC.md](SPEC.md), [ROADMAP.md](ROADMAP.md), and
[DEPENDENCIES.md](DEPENDENCIES.md) for architecture, acceptance gates and terms.

## STEP and print workflow regression

`ctest --test-dir build -R print_workflow --output-on-failure` drives the real
native CLI from an isolated temporary workspace. Independent ZIP/XML, triangle
adjacency, signed-volume, bed-margin and spacing checks cover 3MF, layout replay,
subset import and periodic-face rendering/STL export. Python is a development
test driver only; the installed exporter and inspector are C++.
