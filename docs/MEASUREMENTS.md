# Measurements

The viewer's ruler measures two user-defined points directly on the model,
without opening a panel. Enable Measure in the bottom toolbar, click point A,
then click point B. Markers, their connecting line and the straight-line 3D
distance in mm appear on the canvas. Click A or B to replace that endpoint;
click another visible location to start a new pair. Escape or the ruler button
exits the mode and hides its overlay. Re-enabling restores the current points.

Picks within six CSS pixels of a visible displayed edge snap to the closest
point on its polyline, including its endpoints. Edge outlines need not be
turned on. Occluded and clipped edges cannot attract the pick; hidden parts
are excluded and exploded edges follow their displayed placement. Other
picks use the frontmost displayed surface, including displayed section caps.
Accuracy is limited by the displayed edge polylines and surface tessellation;
this is not an exact native B-rep measurement or a manufacturing tolerance.
Coincident points measure zero. Point clicks do not also trigger double-click
camera framing.

Orbit and zoom preserve the positions and move the overlay with the camera.
Model/evaluation, artifact hash, visibility or presentation changes retire the
points. PNG capture includes the ruler while its mode is active. Measurements
are local viewer state, available for native models and read-only artifacts.
They are not saved in model history, exposed as native selection references,
restored across reopening, or submitted as native jobs. Draft motion poses must
be saved or reset first.

## Exact source measurements

`cad_measure` measures the current committed source pose using native OpenCascade
B-reps. It does not use mesh distances, clipping planes or exploded offsets.
First obtain an evaluation with `cad_query(kind:"topology" or "mesh")` or the
ready live view. Supply its document, revision, evaluation and feature identity:

```json
{"document_id":"spacer_assembly","revision":1,
 "evaluation_id":"eval_FROM_CURRENT_QUERY","feature_id":"assembly",
 "query":{"action":"pair","targets":[
   {"kind":"part","part_id":"spacer_a"},
   {"kind":"part","part_id":"spacer_b"}],"minimum_clearance_mm":1}}
```

Replace the example evaluation ID with the actual returned token. A pair requires
two distinct targets. A target is a leaf occurrence path, an evaluated face ID
or an evaluated edge ID: `{kind:"part",part_id}`, `{kind:"face",entity_id}` or
`{kind:"edge",entity_id}`. Hierarchy groups are not leaf targets. All targets
belong to the shared feature/evaluation; cross-document pairs are unsupported.
`minimum_clearance_mm` is optional, finite and in [0,1e6]. It is the caller's
requirement; the service invents no manufacturing tolerance.

`query:{action:"clearance"}` measures every unordered pair of assembly leaves.
It requires 2–23 leaves, at most 253 pairs. Above that bound the operation fails
explicitly. Supply optional distinct `part_ids` (2–23) for an explicit subset.
Reports distinguish `all_assembly_leaves`, `explicit_leaf_subset` and
`explicit_pair`; a subset never claims whole-assembly coverage.

Each result pins document/revision/evaluation/feature, actual source SHA-256,
native build and OCCT 8.0.1. Its report gives minimum distance in mm and up to 16
closest-point witness pairs per pair, with raw native solution count and truncation.
The first 16 candidates are checked for finite coordinates, agreement with the
reported distance, and independent exact point-to-target distance within 1e-7 mm.
Inconsistent candidates are omitted and counted in `rejected_witness_count`;
`witnesses_truncated` also records those omissions. No qualified witness fails
the operation explicitly. Native geometry is never changed to produce evidence.
Witnesses are source coordinates; the solver's representative/ordering is not
canonical. Planar faces and straight edges also report an acute, unoriented angle
in degrees. Line/plane angles measure the line relative to the plane. Curved
entities do not receive a guessed angle.

Part pairs additionally return common solid material volume in mm³ and an
interference flag. Only common solids contribute volume; face/edge contact does
not imply material overlap. A contained part can have zero solid distance and
positive common volume. Face/edge targets return null material-interference fields.
Positive volume above 1e-9 mm³ fails a part-pair check even at zero requested
clearance. Distance checks use 1e-7 mm slack. A supplied threshold yields `pass`
or `fail`; absent a threshold the status is `measured` unless part interference is
detected. These are geometric findings at the saved pose, not production
certification, swept clearance or dynamics simulation.

Missing evaluations, changed HEAD, different source/build and draft picks fail.
Save or reset a motion preview before measuring. Faces/edges are recovered
uniquely from their recorded native geometry descriptors (type, bounds, center,
area/length, analytic axes and owner where available), ignoring enumeration ID
and selector fields. Numeric descriptor matching uses
`1e-7 + 1e-9 * max(abs(values))`. No match or multiple matches fail explicitly;
this is geometric recovery within a current evaluation, not stable face/edge
naming. Degenerate edges fail. Source is rechecked under the document writer
lock after native computation, before a result returns.

Direct CLI and MCP calls share the service. Durable `cad_job` submission supports
bounded native processes, cancellation/deadlines and qualified historical replay.
Failed work does not change source/history or publish a partial report. Refresh
old evaluation tokens after a native build changes; older metadata lacks the new
private source/build qualification.

The inspection tools' Face and part clearance panel takes two parts or current face/edge picks,
measures a pair or checks an assembly, and displays source distances, witnesses,
angles and overlap. Jobs run asynchronously. Clear result cancels pending work
and clears the view's measurement reference. App-only `cad_viewer` action
`measure` accepts optional `query`: an object starts a job, omission polls, and
null clears. It requires view/evaluation identity. Sync and `cad_context` include
the current job ID and qualified query; agents retrieve its report with `cad_job`.
Quick Edit includes this reference. Reopening restores it; new evaluations,
retargets and stale references cannot reuse it. Visibility and presentation
changes retain source measurements. HANDOFF records executed evidence.
