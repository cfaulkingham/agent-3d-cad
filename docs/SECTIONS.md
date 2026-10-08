# Exact native section geometry

`cad_measure` accepts `query.action:"section"` for a current committed
document/revision/evaluation/feature. The native service intersects exact source
B-reps with an explicit plane and returns result-local curves, point contacts,
material cap regions, exact areas/boundary lengths and derived cap triangles.
The native contract and live viewer implementation are available locally. The
Exact section panel calculates filled surfaces and dimensions for the current
clipping plane and exploded placement. See HANDOFF for executed evidence and
remaining host validation.

```json
{"action":"section","plane":{"normal":[0,0,1],"offset_mm":5},
 "explode":{"distance_mm":8,"directions":[
   {"part_id":"left/pin","direction":[0,0,1]}]},
 "part_ids":["left/pin"]}
```

Only `action` and `plane` are required. Fields are closed. Normal/direction
components lie in [-1,1] and squared length must be within 1e-6 of one. The
offset is in [-1e12,1e12] mm; exploded distance is in [0,1e6] mm. Omitted explosion
is zero with no overrides. Up to 1,024 unique direction overrides and 1–1,024
unique subset paths may name current assembly leaves. A non-assembly source
rejects leaf scopes, overrides and positive explosion. Invalid, stale, draft,
source/build-mismatched and missing-leaf queries fail explicitly.

The displayed-world plane equation is `normal · point = offset_mm`. Each source
leaf remains in its committed pose. If its displayed displacement is `d`, its
source plane offset is `offset_mm - normal · d`. The default displacement uses
the same bounds-center direction, sorted occurrence order and coincident-center
fallback as live explosion. Explicit directions are normalized. Visibility does
not remove leaves from a section query; `part_ids` supplies deliberate subset
coverage. This computation never edits placements, source geometry or HEAD.

The standard measurement result header pins document, revision, evaluation,
feature, kernel, native build and actual source-model SHA-256. Its `report` has:

- `schema_version:1`, `units:"mm"`, `action:"section"`,
  `method:"native_BRep_planar_section"`,
  `coordinate_space:"committed_source_pose"` and
  `plane_coordinate_space:"displayed_world_mm"`.
- `coverage`: `feature_solids`, `all_assembly_leaves` or `explicit_leaf_subset`.
  `plane` and resolved `explode` record the exact inputs.
- `sections`: one scope per leaf, or one null-owned feature scope. Each records
  `source_plane_offset_mm`, `displacement_mm`, area, boundary length, region/curve
  counts, `contact_points` and `status` (`empty`, `tangent` or `area`).
- `regions`: validated native material faces, with `cap-N` IDs, owning `part_id`
  (or null), source `solid_index`, exact `area_mm2`, `perimeter_mm`, `center_mm`
  and `wire_count`. Interior wires preserve actual holes.
- `curves`: native intersection edges with `section-N` IDs, owner/solid index,
  analytic `curve_kind`, exact `length_mm`, center/bounds, `degenerate`, sampled
  `points` and supported line direction or circle radius/axis.
- `mesh`: source-coordinate `positions`, indexed `triangles` and
  `triangle_regions` mapping each triangle to a derived cap. Linear deflection
  is 0.1 mm. This mesh presents native material faces; it is not used to invent
  exact areas or bridge holes.
- Aggregate `area_mm2`, `boundary_length_mm` and status. Area is explicitly
  `sum_of_solid_sections`: repeated/interfering solids retain separate sections,
  and this total does not claim union area. Aggregate boundary length sums native
  intersection-edge lengths; region perimeters describe material-face wires.
- `selection_lifetime:"section_result"`, native topology tolerance 1e-7 mm and
  explicit point-to-plane validation tolerance `point_tolerance_mm:1e-6`.

An outside plane is a successful complete empty result. Point/curve tangencies
retain contact geometry with no invented material cap. Every returned cap mesh
point, curve sample, contact point and cap centroid is finite, within coordinate
bounds and on its owning source plane within the stated point tolerance. Curves
are evaluated natively; sampled presentation alone is not an analytic curve.

Aggregate limits are 1,024 source solids, 10,000 derived regions/curves/contact
points, 200,000 cap vertices, 200,000 cap triangles, 200,000 curve samples and an
8 MiB serialized report. Excess work fails; entities are never silently omitted.
Use `cad_job` for bounded asynchronous execution, cancellation and deadlines.
Historical successful jobs remain qualified historical data after source edits
or restart. New queries require the current source/build evaluation.

## Native live-view coordination

`cad_viewer`'s `section` action accepts `view_id`, `evaluation_id` and optional
`query`. An object starts a durable native section job; omission polls; null
clears its reference and requests cancellation. This slot is independent of
`measure`, which accepts only pair/clearance queries. Starting returns the saved
admission snapshot without a potentially contending second job read.

A live section query must match the current clipping plane and exploded
placement. Direction-array order does not affect matching; overrides at zero
distance produce no displacement. Missing clipping or a different plane/placement
fails as `stale_selection`. Ready sync and `cad_context.section` contain the
current evaluation, job ID and query. Source/build refresh, retargeting, disabling
clipping or changing its plane/explosion retire the section. Kept-side reversal,
camera changes and visibility retain the same qualified geometric result.
Clearing and plane retirement cancel outside document/view writer locks. A
completed job that wins the cancellation race remains historical; no retired
job can restore its view reference or publish a partial cap report.

Section IDs identify derived review geometry only. They are never original
`face-N`/`edge-N` references or stable design selectors. To edit the model,
resolve an original current face/edge selection through `cad_resolve_selection`.
The exact drawing-section pipeline remains separately available in DRAWINGS.md.

## Reviewing a section in the viewer

Enable **Clip with a plane** in Visual inspection, choose the plane and offset,
then select **Calculate exact section**. A successful result fills the cut's
material regions with a distinct section color and draws their native boundary
curves. Native cap triangles preserve holes; the displayed area and boundary
length come from the exact report, not triangle measurements. The panel lists
filled regions, holes and the report's actual feature or leaf coverage. A plane
outside the source shows an empty result; point and curve tangencies report the
contact without inventing a filled surface.

The calculation is explicit and asynchronous. Queued/running work disables a
second calculation, while **Clear result** can cancel it. Moving the plane,
changing explosion, disabling clipping or loading a different source retires
the report and its surfaces immediately; calculate again for the new view.
Reversing the kept side and changing visibility retain the same native result.
Hidden leaves remove their visible surfaces without changing query coverage or
the reported totals. All-assembly reports include all leaves; explicit subsets
remain labeled as subsets. Interfering solids contribute separate section areas.

Section surfaces occlude original faces and edges behind them. Picking one
clears an original selection and explains that it is review geometry. Holes
remain open for picking visible original geometry through the cut. No `cap-N`
or `section-N` ID becomes an editable source reference. Captures include the
rendered section, and agent-request snapshots include its qualified job/query
metadata alongside presentation and source identity. Reopening restores a
current section; delayed results cannot restore a retired plane or evaluation.
