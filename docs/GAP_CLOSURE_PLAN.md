# Build123d comparison gap closure

Authorized 2026-10-10: tackle every deficiency identified in the comparison of
agent-3d-cad `f90033f` and the local build123d `d3b12235`, using parallel agents.
This is the completion ledger, not a declaration that planned APIs exist.

## Required behavior and evidence

| Workstream | Required scope | Status / evidence |
|---|---|---|
| Solid operations | Uniform/nonuniform scaling, existing-face draft with neutral plane, asymmetric/angle chamfers with reference side, twist extrusion, holed lofts and vertex endpoints | In progress: isolated `parity-solids` worktree |
| Surface robustness | Both signs/joins of the existing freeform-panel thickening case; explicit surface/shell STEP round trips | In progress: isolated `parity-surfaces` worktree |
| Surface construction | Curve-network/Gordon surfaces, boundary fills with tangent/curvature continuity, arbitrary trim contours/holes, curved curve/sketch projection | In progress: isolated `parity-surfaces` worktree |
| Sheet metal | Chained flanges, bends through blanks, hems, jogs, miters, generated bend/corner relief, cuts through bends and matching developed geometry; investigate Bezier verification abort | In progress: isolated `parity-sheet-metal` worktree |
| Parametric authoring | Bounded trig/square-root/conditional expressions and parameter-driven pattern counts; explicit units, dependency tracking and failure rollback | Implemented `fcc4f1b`; 4 focused CTests plus independent schema checks passed; integrated acceptance pending |
| Curves and queries | Dedicated helices; constrained tangent lines/arcs; hull, trace, full-round; curve sampling/tangents/trimming and richer geometric selection | Pending |
| Captured authoring | DXF blocks/text/hatches; editable text-on-path with captured font/source provenance | Pending |
| Mesh reconstruction | Native analytic plane/cylinder/sphere recognition with residuals and leftovers, usable as guided editable reconstruction rather than false recovered history | Pending |
| Embedded file delivery | Bounded source-qualified export resources and supported host save/download action for STEP/STL/drawings; retain safe fallback when capability absent | Implemented resource capture/read and viewer request; focused native/UI and SDK checks pass; actual target-host save actions pending |
| Product acceptance | Current cross-platform validation; actual ChatGPT/Claude install/create/select/edit/restart/reopen/export/update/remove/reinstall journeys; signing and distribution/CPU-selection gates | Pending; external signing/directory access may be required; implementation tests cannot establish these gates |

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
documented real-host checks. Discovery currently occupies 474,263 of 476,160 bytes;
schema growth must be handled without weakening contracts or relaxing that test
merely to claim success.

The initial comparison rebuilt the native service and passed 8/8 modeling suites
in 32.71 seconds and 14 selected sibling tests. Native probes confirmed missing
scale/draft, asymmetric chamfers, square-root expressions, holed lofts and surface
STEP import. Native freeform thickening failed for both signs/joins; build123d
thickened its exported surface and native STEP import validated both resulting
solids. These are baseline observations, not completion evidence.

The goal remains active until every row has scope-matched evidence. Partial
increments, known rejected geometries, unavailable host capabilities and missing
publisher credentials stay explicit unfinished work.
