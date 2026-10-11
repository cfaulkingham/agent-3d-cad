# Build123d comparison gap closure

Authorized 2026-10-10: tackle every deficiency identified in the comparison of
agent-3d-cad `f90033f` and the local build123d `d3b12235`, using parallel agents.
This is the completion ledger, not a declaration that planned APIs exist.

## Required behavior and evidence

| Workstream | Required scope | Status / evidence |
|---|---|---|
| Solid operations | Uniform/nonuniform scaling, existing-face draft with neutral plane, asymmetric/angle chamfers with reference side, twist extrusion, holed lofts and vertex endpoints | Integrated `ad59f5b`; strict analytic/STEP, edit/rollback, cache and component tests. [Bounded contract](PARITY_SOLIDS.md); native/actual-call acceptance at `d359331` passed |
| Surface robustness | Both signs/joins of the existing freeform-panel thickening case; explicit surface/shell STEP round trips | Integrated `d154229`; four thickening cases, nonmaterial STEP imports and explicit solid materialization tested. [Contract](PARITY_SURFACES.md); native/actual-call acceptance at `d359331` passed |
| Surface construction | Curve-network/Gordon surfaces, boundary fills with tangent/curvature continuity, arbitrary trim contours/holes, curved curve/sketch projection | Integrated `d154229`; exact compatible polynomial networks, checked C0/G1/G2 fills, UV contour holes and qualified directional projection. [Bounds](PARITY_SURFACES.md); native/actual-call acceptance at `d359331` passed |
| Sheet metal | Chained flanges, bends through blanks, hems, jogs, miters, generated bend/corner relief, cuts through bends and matching developed geometry; investigate Bezier verification abort | Integrated `6ce74b9`; named forming tree, exact developed geometry and narrow exact Bezier-support remedy. [Contract](PARITY_SHEET_METAL.md); native/actual-call acceptance at `d359331` passed |
| Parametric authoring | Bounded trig/square-root/conditional expressions and parameter-driven pattern counts; explicit units, dependency tracking and failure rollback | Implemented `fcc4f1b`; focused and final native/actual-call acceptance at `d359331` passed |
| Curves and queries | Dedicated helices; constrained tangent lines/arcs; hull, trace, full-round; curve sampling/tangents/trimming and richer geometric selection | Integrated `39a5022` and `83a0b63`; rational curves, exact native queries and analytic hull/trace/full-round tests. [Bounds](PARITY_CURVES.md); native/actual-call acceptance at `d359331` passed |
| Captured authoring | DXF blocks/text/hatches; editable text-on-path with captured font/source provenance | Integrated `6bd87ef` and `b5ef0d5`; captured fonts, bounded block/hatch/text expansion and editable planar glyph placement. [Contract](PARITY_AUTHORING.md); native/actual-call acceptance at `d359331` passed |
| Mesh reconstruction | Native analytic plane/cylinder/sphere recognition with residuals and leftovers, usable as guided editable reconstruction rather than false recovered history | Integrated `aec96c0`; complete triangle residuals/membership, bounded fitting and native-built editable proposals. [Bounds](MESH_RECONSTRUCTION.md); native/actual-call acceptance at `d359331` passed |
| Embedded file delivery | Bounded source-qualified export resources and supported host save/download action for STEP/STL/drawings; retain safe fallback when capability absent | Implemented resource capture/read and viewer request; final native/UI and SDK acceptance passes; actual target-host save actions pending |
| Product acceptance | Current cross-platform validation; actual ChatGPT/Claude install/create/select/edit/restart/reopen/export/update/remove/reinstall journeys; signing and distribution/CPU-selection gates | Package header/provenance checks, Rosetta-aware native installer selection, complete bundled guides and isolated lifecycle implemented `9753e62`; final package inventory, 121 MCP / 387 isolated lifecycle checks and guide links pass; actual foreign-platform/host/signing/directory gates remain open. [Local evidence boundary](LOCAL_PACKAGE_ACCEPTANCE.md) |

All geometry remains native C++20 / pinned OCCT 8.0.1 behind `BuiltModel`.
No source-model code execution, sibling runtime dependency, silent intent change,
validation weakening, or unqualified persistent topology indices are allowed.
New operations must participate in document/schema validation, dependencies,
caches, portable components, jobs and transactional rollback. Independent geometry
checks must use tolerances and exported STEP readback where applicable.

## Integration and completion audit

Each workstream requires implementation, documented contracts, meaningful analytic
and failure tests, and an integrated build. Cross-cutting validation includes the
complete native CTest set, independent actual-call JSON Schema validation, MCP SDK
interoperability, bounded discovery, relocated native/plugin packaging and the
documented real-host checks. Compact mutation receipts and strict structural
schema factoring are implemented `8d9c289`; full source remains available through
revision-pinned `cad_read`, and immutable durable results are projected without
rewriting their bytes. The user explicitly chose this preview API contract;
[COMMIT_RECEIPTS.md](COMMIT_RECEIPTS.md) records migration and hash semantics.
All integrated features and exact section endpoints occupy
454,784 of the unchanged 476,160-byte budget. Independent lossless factoring
proofs and actual-call Draft 2020-12 validation retain closed schemas and rejection
checks. Final source/guide checkpoint `d359331` passed **72/72 CTests in 300.77 s**,
**1,421 official MCP SDK checks**, and **12/12 actual-call schema/artifact scripts**.
The modeling matrix passed **1,227 checks / 72 models / all 61 feature kinds**;
full conformance including the native slicer fixture passed **893 checks**.
Assertions, tolerances, catalog and worker budgets remain unchanged. Executed
source, binary/archive hashes and evidence locations are recorded in HANDOFF.

The continuation audit at ledger `465e937` additionally passed **110 final-package
resource-delivery checks across 10 files**, including independent byte decoding,
restart/edit persistence and original-export deletion. Actual host save remains
unverified. GitHub confirms the tested source is unpublished and has zero workflow
runs; branch publication for CI awaits approval. Docker's configured socket is
absent and its restart question remains pending. The local signing query reports
zero valid identities. Installed ChatGPT app metadata combines the ChatGPT name
with `com.openai.codex`; the intended acceptance target is being clarified.
These are external prerequisites, not missing native acceptance evidence.

Numerical integration required independent corrections. `450656f` selects
rational quadrature per support face; `6d82d1f` corrects exact planar polygon
moments, and `fc25da3` conditions rational first moments away from symmetry
cancellation. The curved-pipe example fell from the unchanged 30-second worker
timeout to approximately 0.37 seconds with independent area/volume/centroid/STEP
and transformed mixed-body checks. The artifact flow exposed an imported
rectangular STEP centroid error despite correct volume and bounds. `79e448c`
preserves signed first moments through intermediate mass cancellation and uses
bounded common-reference retries for individual curved-face cancellation;
strict box/cubic-roof native, worker, snapshot, STEP and artifact oracles pass.
A subsequent independent audit found 7.28e-5 mm centroid drift for two 1 mm
solids separated by 1e6 mm. `1f766bb` integrates per solid before compensated
positive-mass combination and reports the analytic 500000.5 mm centroid exactly.
Its independent native/worker/snapshot/cold STEP cases include unequal sizes,
reversed order, rotated nested and coincident instances; the focused increment
passed 12/12 suites in 67.18 seconds, including 579 solid checks. Final integrated
acceptance at source/guide checkpoint `d359331` passed as recorded above.

The initial comparison rebuilt the native service and passed 8/8 modeling suites
in 32.71 seconds and 14 selected sibling tests. Native probes confirmed missing
scale/draft, asymmetric chamfers, square-root expressions, holed lofts and surface
STEP import. Native freeform thickening failed for both signs/joins; build123d
thickened its exported surface and native STEP import validated both resulting
solids. These are baseline observations, not completion evidence.

Remaining comparative feature limits are documented in [MODELING_GAP.md](MODELING_GAP.md):
no general sketch constraint solver, polynomial/nonperiodic Gordon networks,
line/circle hulls and straight-edge full-rounds, restricted DXF/text layouts,
rectangular bend cuts and no arbitrary imported-sheet unfolding. Implemented
native contracts are accepted within these explicit bounds; broader API parity
is not claimed.

The goal remains active until every required row has scope-matched evidence. Partial
increments, known rejected geometries, unavailable host capabilities and missing
publisher credentials stay explicit unfinished work.
