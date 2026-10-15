# Geometry edit action matrix

The developer matrix sends real MCP calls to the native Service. It discovers
faces and edges on five independently described parts: a box, bored plate,
extruded L bracket, vertically rounded box, and circular ruled loft. Analytic
volumes validate every starting model. Tests use isolated temporary workspaces;
no installed plugin, source document or sibling repository is required.

## Coverage and identities

Every eligible edge gets a selective fillet and chamfer. Every planar face gets
an inward shell, a through hole and a boundary projection followed by extrusion.
Every body gets a transform, mirror and separated two-copy pattern. Degenerate
edges and nonplanar face actions are outside the matrix's declared applicability.
Missing unique selectors are recorded as unselectable cases, not omitted.

Case identities use quantized geometry descriptors from the initial source model,
not evaluation-local face numbers. The complete inventory and canonical action
specifications are compared to the baseline even during a sampled run. A changed
corpus, action definition or missing case requires explicit full-run review.
These case keys are test identities only, not claims of persistent CAD naming.

One uniformly scaling parameter drives the source and the action dimensions.
The matrix parameterizes geometric selectors once before the action. Every
successful edit walks scale through 1.1, 0.9 and 1.0 using those same saved
selectors, checking volume, area, centroid, bounds and solid count against
independent scaling laws. Transform/mirror/pattern actions also check volume and
placement; projections check source face area times extrusion height; subtractive
and edge edits must have the expected material effect. Rejected actions and
parameter edits must preserve HEAD. Historical source and an explicit rejected
negative-scale transaction are checked after successful actions.

Drill positions come from a fixed grid in the selected planar face's exact bounds.
The same qualified mesh/topology evaluation checks interior occupancy and drill
clearance, including holes and L-shaped boundaries. Changing triangle diagonals
or vertex order cannot change the grid coordinates. Geometry creation and
measurements remain native exact B-rep operations. This initial matrix does not
cover arbitrary independent dimension changes, all feature types, all topology
changes or manufacturability.

## Run and review

```sh
ctest --test-dir build -R '^action_matrix_' --output-on-failure
python3 tests/action_matrix.py build/agent-3d-cad --full --results build/action-matrix-full.json
```

The ordinary CTest covers each action family on each part plus a stable one-in-eight
sample. The complete matrix is available locally and through the native CI
workflow's `full_action_matrix` manual input. `--filter TEXT` selects case keys for
diagnosis; it cannot update the baseline. `--jobs 1..4` bounds independent case
concurrency (default 2); each case owns its MCP session and workspace. Native
geometry remains serial inside isolated worker processes. Each edit uses a
10-second / 2-GiB durable job; workers enforce time and memory limits. Process or
protocol failures are distinct from geometry refusals and cannot be blessed.

`--results` writes executable/corpus hashes, full case inventory, action-definition
hashes, actual edit requests, outcomes, errors and timings. Captured action and dimension-edit
failure packages are copied beside that report, making them replayable after
scratch cleanup. For a supported feature-qualified error, use `cad_failure`
minimization as described in [FAILURE_REPLAY.md](FAILURE_REPLAY.md). Search limits
or timeouts are never treated as successful geometry.

The committed `tests/action_matrix/baseline.json` is a regression baseline:
previously passing actions and dimension walks must keep passing. Native geometry
refusals and explicit timeouts remain visible failures in the report; an initial
baseline may record them without claiming those operations work. A new timeout
cannot replace a known refusal. Geometry invariant failures, harness failures and
unstructured process crashes always fail the run, including baseline updates.

After inspecting the complete results and explaining every intended change:

```sh
python3 tests/action_matrix.py build/agent-3d-cad --full --results build/action-matrix-full.json --bless "Explain the reviewed baseline change"
```

Blessing requires an unfiltered full run and a nonempty reason. Review the baseline
diff before committing; do not use it to hide unexpected regressions. The harness
unit tests explicitly check lost coverage, lost selectors, edit regressions,
changed action definitions, timeouts and invariant failures. Actual executed
counts and platform limits belong in HANDOFF.md.
