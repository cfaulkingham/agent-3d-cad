# Native sheet-metal intent and developed blanks

This separate sheet-metal phase adds exact constant-thickness base plates,
cylindrical bends and straight edge flanges, plus an editable developed blank
derived from their preserved bend intent. It uses the existing native service,
isolated OCCT 8.0.1 workers, transactions, caches, selection rules and exports.

`sheet_metal` consumes one connected planar sketch region. Holes in that region
are retained in the base. It requires `input`, positive `thickness`, explicit
`k_factor` in [0,1] and `flanges` (0–32). Material is placed opposite the profile's
oriented normal: an XY profile at Z=0 has material from Z=−thickness to Z=0.
The K-factor is the neutral-axis fraction measured from the physical inside
radius through the thickness. Both thickness and K-factor are editable scalars.

Each flange requires an `id`, `edge`, positive `inside_radius`, signed
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
Their magnitude is at least 0.01° and below 179.99°. A bend is an exact annular
sector between cylinders of radii `inside_radius` and
`inside_radius + thickness`, extruded along the selected edge. The straight
leg is tangent to that curved sector. Base, bend and leg are fused into one
valid exact solid; the service does not represent a bend by a box or a mesh.
Actual material collisions, disconnected joins and self-interference fail the
candidate instead of trimming or adjusting the user's dimensions.
Native verification must also complete successfully. In OCCT 8.0.1 a tested
Bezier-cap base with a straight-edge flange causes the result self-interference
checker to abort. That candidate fails explicitly with bounded `native_faults`
diagnostics; the service does not publish unverified geometry or convert the
authored curve to bypass this limitation. Polygonal, holed and circular-arc base
boundaries are covered by passing geometry regressions.

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
`2 × (radius + thickness) × tan(|angle|/2) − allowance`. Line metadata is retained
for authoring/inspection; drawing exports do not automatically add those lines.

The model keeps constant geometric thickness. Its physical annular-sector
volume uses the geometric middle radius `radius + thickness/2`; developed
length uses the caller's K-factor. Consequently formed and blank modeled
volumes coincide for K=0.5, but differ for other K values. Reports provide both
volumes and explicitly decline a volume-preservation assumption. A supplied
K-factor is an engineering input, without a material/process certification or
an inferred density, elasticity or plastic forming simulation.

This phase supports flanges directly from the base region, with their bend
starting at the selected edge. It does not yet support chained flange trees,
fold lines through an existing blank, jogs, hems, corner/bend relief generation,
miters, cuts through curved bends, virtual-sharp dimension modes, or automatic
unfolding of arbitrary surfaces. Those remain larger sheet-metal operations.
Suitable endpoint relief and manufacturing allowances must be modeled explicitly
in the input profile or addressed in a later sheet-metal phase.

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
distinctions, circular-arc bases and explicit Bezier-check aborts,
reference/collision failures, exact caches, parameter edits, cold
reopen, rollback, independent exports, portable components and durable jobs.
Platform/host release gates remain recorded separately in `HANDOFF.md`.
