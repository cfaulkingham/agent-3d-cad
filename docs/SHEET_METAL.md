# Native sheet-metal intent and developed blanks

The original sheet-metal phase adds exact constant-thickness base plates,
cylindrical bends and straight edge flanges, plus an editable developed blank
derived from their preserved bend intent. It uses the existing native service,
isolated OCCT 8.0.1 workers, transactions, caches, selection rules and exports.
The expanded tree, fold, hem, relief, miter and cut contracts are described in
[PARITY_SHEET_METAL.md](PARITY_SHEET_METAL.md).

`sheet_metal` consumes one connected planar sketch region. Holes in that region
are retained in the base. It requires `input`, positive `thickness`, explicit
`k_factor` in [0,1] and `flanges` (0–32). Material is placed opposite the profile's
oriented normal: an XY profile at Z=0 has material from Z=−thickness to Z=0.
The K-factor is the neutral-axis fraction measured from the physical inside
radius through the thickness. Both thickness and K-factor are editable scalars.

A direct base-edge flange requires an `id`, `edge`, positive `inside_radius`, signed
`angle_deg` and positive `length`. Its edge uses the existing geometric edge
selector with `feature_id` equal to the source sketch, `curve_kind: "line"` and
`expected_count: 1`. It must resolve one straight outer boundary edge; a hole
edge, missing/ambiguous edge, or repeated source edge fails explicitly.
`length` is the straight material after the curved bend, measured from its
tangent line. Optional nonnegative `start_gap` and `end_gap` shorten the flange
along the oriented outer boundary edge. The remaining width must be positive.
Gaps are design inputs; they do not automatically create bend reliefs.

For example, this base has one 90° wall with a 3 mm inside radius and a 10 mm
straight leg, in 2 mm sheet:

```json
{"schema_version":1,"units":"mm","parameters":{"wall":10,"k":0.5},"features":[
  {"id":"profile","type":"sketch",
   "workplane":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},
   "profile":{"type":"rectangle","width":20,"height":30}},
  {"id":"formed","type":"sheet_metal","input":"profile","thickness":2,
   "k_factor":{"parameter":"k"},"flanges":[
    {"id":"wall","edge":{"type":"geometric","feature_id":"profile",
      "curve_kind":"line","expected_count":1,
      "center":{"point":[10,30,0],"tolerance":0.000001}},
     "inside_radius":3,"angle_deg":90,"length":{"parameter":"wall"}}]},
  {"id":"flat","type":"sheet_unfold","input":"formed"}
],"output":"flat"}
```

Positive angles fold toward the profile normal; negative angles fold away.
Ordinary bends have magnitude at least 0.01° and below 179.99°; explicit open
hems use exactly 180° as described in the expanded contract. A bend is an exact annular
sector between cylinders of radii `inside_radius` and
`inside_radius + thickness`, extruded along the selected edge. The straight
leg is tangent to that curved sector. Base, bend and leg are fused into one
valid exact solid; the service does not represent a bend by a box or a mesh.
Actual material collisions, disconnected joins and self-interference fail the
candidate instead of trimming or adjusting the user's dimensions.
Native verification must also complete successfully. OCCT 8.0.1 can abort its
self-interference check on a swept Bezier wall. The sheet implementation now
converts that exact polynomial/rational extrusion to a B-spline surface using
the same poles, weights, extrusion endpoints and UV boundaries, with exact
knot insertion for stable surface integration.
It retains analytic bend cylinders and unchanged tolerances, checks geometric
invariants, and requires the complete native checker to pass. See the regression
and independent cubic area check in `tests/sheet_metal_tests.cpp`; an aborted
checker is never treated as a passing result.

`sheet_unfold` requires `input` naming an unchanged earlier `sheet_metal`
feature. It recreates the planar base with each flange extended by its straight
length plus the bend allowance:

`allowance = |angle in radians| × (inside_radius + k_factor × thickness)`.

The resulting blank is an actual exact solid of the same nominal thickness,
with original base holes retained. Overlapping developed regions fail. It can
be saved as output, assembled and exported independently as STEP/STL/3MF; the
normal native top-view drawing workflow produces its exact outline and holes
as DXF when the blank lies in XY. Drawing frames use fixed world directions;
an arbitrarily oriented blank must be aligned with the chosen orthographic
frame to retain cutting dimensions. There is no automatic flatten-to-XY drawing
alignment. Imported or arbitrarily modified solids cannot be unfolded by this
feature. It derives the blank from captured bend intent, rather than guessing
radii, neutral axes or a fabrication history from geometry.

Source-qualified `summary.sheet_metal` reports the formed/flat mode, thickness,
K-factor, bend widths/angles/radii/straight lengths, bend allowance and bend
deduction. It also provides world-coordinate bend-start, center and end lines
on the developed profile. Bend deduction uses
`2 × (radius + thickness) × tan(|angle|/2) − allowance`; a 180° hem reports
`null` because its tangent deduction is undefined. Line metadata is retained
for authoring/inspection; drawing exports do not automatically add those lines.

The model keeps constant geometric thickness. Its physical annular-sector
volume uses the geometric middle radius `radius + thickness/2`; developed
length uses the caller's K-factor. Consequently formed and blank modeled
volumes coincide for K=0.5, but differ for other K values. Reports provide both
volumes and explicitly decline a volume-preservation assumption. A supplied
K-factor is an engineering input, without a material/process certification or
an inferred density, elasticity or plastic forming simulation.

The original direct-bend contract is extended by
[PARITY_SHEET_METAL.md](PARITY_SHEET_METAL.md), which specifies named chained
flanges, folds through existing rectangular blank regions, jogs, open hems,
generated endpoint/corner relief, leg miters and cuts crossing cylindrical bends.
Virtual-sharp dimension modes and automatic unfolding of arbitrary surfaces
remain outside the native contract.

Named flange IDs and geometric rules remain editable. Parameters invalidate the
appropriate exact feature caches. Revision-pinned components rewrite nested
flange edge feature IDs and carry a complete self-contained folded/flat intent
closure. Cold reopening needs no original source workspace or scripting runtime.
All saved feature IDs, revision guards, failed-build rollback and native job
semantics apply unchanged. Topology history is available native evaluation
evidence; unfold construction does not fabricate stable formed-to-flat face IDs.

Development verification is `ctest --test-dir build -R sheet_metal
--output-on-failure`. The dedicated suite covers analytic curved-bend volumes
and developed lengths, independent STEP cylinder-radius/thickness inspection,
signed/non-square bends, arbitrary oriented workplanes, base holes, K-factor
distinctions, circular-arc bases and verified exact Bezier normalization,
reference/collision failures, exact caches, parameter edits, cold
reopen, rollback, independent exports, portable components and durable jobs.
Platform/host release gates remain recorded separately in `HANDOFF.md`.
