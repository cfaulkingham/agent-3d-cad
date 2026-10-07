![agent-3d-cad — Native, editable CAD for AI agents. Create, inspect, refine, export.](docs/assets/readme-banner.png)

# agent-3d-cad

A native editable CAD service for agents, written in C++20 on OpenCascade 8.0.1.
Create saved designs, make atomic parameter/feature edits, inspect selectable
geometry, preview changes, and export independent STEP/STL files and native
PDF/SVG/DXF drawings through MCP or
the CLI. Models contain structured intent; they never execute scripts or compile
code. End-user bundles need no Python, Rust, Node, CMake or compiler.

**Status: native preview, including M4 live viewing, M5 drawings and M6 assemblies; platform/release evidence is
tracked in [HANDOFF.md](docs/HANDOFF.md).** The service includes selective fillets,
workplanes and numeric profiles, extrusion/revolution/loft/sweep, transforms,
patterns, reusable instances, holes, bounded expressions, immutable STEP imports,
an offline viewer, durable bounded jobs, and editable assemblies with placements,
rigid datum mates, BOM exports and exploded drawings with part balloons. A general sketch or mate constraint
solver, kinematics, manufacturing certification and remote hosting are outside
this scope.

The service includes a bundled **live MCP App viewer**: a model
library, feature tree, WebGL rendering, face/edge selection, per-part hide/isolate,
camera retention,
and Quick Edit requests with exact revision context. An open view follows saved
edits automatically. The select–edit–refresh workflow has been observed in the
actual Codex host on macOS arm64. Quick Edit may prepare a chat composer draft
that the user must send. See [LIVE_VIEWER.md](docs/LIVE_VIEWER.md).

## Build and test

Development needs CMake 3.24+ and a C++20 compiler. Build checksum-pinned native
dependencies (OCCT 8.0.1 and FreeType 2.14.3):

```sh
cmake -S cmake/dependencies -B build-deps \
  -DAGENTCAD_DEPS_PREFIX="$PWD/.deps/occt" -DAGENTCAD_JOBS=4
cmake --build build-deps --parallel 4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DOpenCASCADE_DIR="$PWD/.deps/occt/lib/cmake/opencascade"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

An existing exact exception-enabled OCCT SDK can be selected with OpenCASCADE_DIR.
Use the supplied dependency recipe's HLR midpoint, root streaming and grid reuse
patch for the cold-drawing optimization and current performance geometry regressions; an unmodified SDK
retains the previous behavior. The patch and modified source accompany bundles.
CMake fetches checksum-pinned nlohmann JSON 3.12.0 when not installed. Offline
build overrides and bundle instructions are in [DISTRIBUTION.md](docs/DISTRIBUTION.md).
Local convenience presets and SDKs are ignored and are not repository requirements.

## Try a complete workflow

With the MCP server connected to an MCP Apps host, create a model and call
`cad_open` with its `document_id` and a workspace-unique `view_id`. Keep that view
open while using `cad_apply`; it updates automatically. `cad_context` reads the
user's current selection and camera, and `cad_show` switches the document in the
same view. The bundled [native-cad skill](skills/native-cad/SKILL.md) guides this
agent workflow. The CLI/offline workflow below remains available.

Use a fresh workspace:

```sh
build/agent-3d-cad --version
build/agent-3d-cad call cad_create --workspace ./workspace --input examples/plate.create.json
build/agent-3d-cad call cad_apply --workspace ./workspace --input examples/plate.edit.json
build/agent-3d-cad call cad_view --workspace ./workspace --input examples/plate.query.json
build/agent-3d-cad call cad_query --workspace ./workspace --input examples/plate.query.json
build/agent-3d-cad call cad_export --workspace ./workspace --input examples/plate.export.json
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
`tools` publishes full input/output schemas. The [protocol](docs/PROTOCOL.md)
describes feature and selection contracts, limits, errors and persistence.

An `assembly` feature gives reusable source solids distinct part IDs and world
placements. Rigid mates align explicit parent/child datum frames with editable
offsets and angles; changing source parameters rebuilds every affected instance.
Assembly geometry stays separate even where parts touch or overlap. Use
`set_part_placement`, `set_mate`, and `remove_mate` for atomic arrangement edits,
and view-specific part offsets for exploded drawings. See
[ASSEMBLIES.md](docs/ASSEMBLIES.md) for a complete editable example, transform
conventions, inspection fields and the supported one-level assembly scope.
Source-keyed BOM metadata supplies optional item/part numbers, descriptions and
materials; quantities count instances. `cad_bom` exports revision-qualified
JSON/CSV. Drawing `bom: true` adds the table and optional visible-surface balloons.
See `examples/assembly-bom.drawing.json` for an assembled/exploded sheet.

Repeated queries, exports and drawings automatically reuse exact geometry and
projected drawing views in a bounded workspace cache. Drawing layout, dimensions,
tolerances and formats can change without repeating projection. Model edits or
native build changes invalidate the cache; removing it loses no editable work.
Complex first-time threaded drawings may still need `cad_job` with a longer
timeout. Developer benchmark: `python3 tests/cache_benchmark.py
build/agent-3d-cad build/cache-benchmark` (isolated workspaces; two hatched sections
of the full M20 knob, cold/warm/style-change and geometry-only timings). Optional
`--views standard` exercises the much slower four-view hidden-line drawing.
Use `--cold-only` to measure the first drawing alone, and `--timeout-ms` to set
its bounded job budget. Failed jobs retain timings and errors in the report.

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
Nineteen application tools expose create/read/apply/restore/import, query/export/BOM/drawing,
view/preview/selection resolution, comparison, jobs, model discovery and live
view context. `cad_open` attaches the bundled MCP App resource; `cad_viewer` is an
app-only transport tool. This is a local stdio service with no HTTP endpoint.
No global host configuration is installed for you.

`cad_drawing` creates revision-qualified vector drawings in isolated native
workers, with aligned first-/third-angle layouts, hatched planar sections,
hidden-line views, geometry-checked linear/angular dimensions and explicit tolerances,
PDF/SVG sheets, and one 1:1 mm DXF per view. It saves a reusable recipe; replay it
against a later committed revision to regenerate. See [DRAWINGS.md](docs/DRAWINGS.md)
and `examples/plate-section.drawing.json`. The paired `angular-plate` examples
demonstrate angular dimensions and tolerance styles. Drawing generation requires
no extra runtime.

For a verified versioned local installation and explicit Codex registration, see
the [installation instructions](docs/DISTRIBUTION.md#install-use-and-uninstall).

The optional developer interoperability check uses the pinned official MCP Python
SDK in `tests/mcp_sdk_requirements.txt`. Install those requirements in a virtual
environment, then run `python tests/mcp_sdk_smoke.py /absolute/path/to/agent-3d-cad`.
It exercises discovery, schema validation, edits, exports, structured errors,
responsive jobs and restart through an independent client. Python is not part of
the native distribution.

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
| `src/kernel.cpp` | OCCT shapes, history, selectors, mesh mapping, exchange |
| `src/storage.cpp` | Native locking, atomic revisions, durable request receipts |
| `src/jobs.cpp` | Process budgets, admission, cancellation and recovery |
| `src/service.cpp` | Shared CLI/MCP use cases and tool contracts |
| `src/viewer.cpp` | Standalone offline browser viewer |
| `src/drawing.cpp` | Drawing recipes, measured annotations, native PDF/SVG/DXF output |
| `src/mcp.cpp`, `src/main.cpp` | Stdio protocol and CLI adapters |
| `tests/` | Real geometry, transactions, topology, jobs, viewer and process tests |
| `cmake/`, `packaging/` | Reproducible dependencies and native bundles |

See [SPEC.md](docs/SPEC.md), [ROADMAP.md](docs/ROADMAP.md), and
[DEPENDENCIES.md](docs/DEPENDENCIES.md) for architecture, acceptance gates and terms.

## License

The original code is released under the [MIT License](LICENSE). Native bundles
also redistribute third-party components (Open CASCADE Technology, nlohmann JSON,
FreeType) under their own licenses; see [NOTICE](NOTICE) and
[packaging/THIRD_PARTY.md](packaging/THIRD_PARTY.md).
