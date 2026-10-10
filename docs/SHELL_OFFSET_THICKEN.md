# Native shell, offset and thickening

These editable features use the pinned OCCT 8.0.1 kernel in the normal isolated
geometry worker. They retain source features, parameter expressions, revision
history, disposable caches and independent STEP/STL/3MF exports. Failed geometry
and references fail the candidate transaction before HEAD publication.

| Feature | Contract | Result |
|---|---|---|
| `shell` | `input`, signed `thickness`, `faces`; optional `join` | Hollow one solid, removing the selected opening faces. `faces: []` creates a sealed cavity. |
| `offset` | `input`, signed `distance`; optional `join` | Parallel solid boundary: positive expands, negative contracts. Multiple input solids offset separately. |
| `thicken` | `input`, signed `thickness`; optional `faces`, `join` | Solid between an open surface patch and its parallel surface, including its boundary walls. Omit `faces` for a sketch profile. Supply explicit `faces` to thicken surfaces of an existing solid. |

`join` is `arc` (default, round convex transitions) or `intersection` (extend
parallel surfaces to their intersection). Dimensions have magnitude at least
0.00001 mm; zero is an error. The fixed geometric coincidence tolerance is
0.0000001 mm. Negative shell thickness builds inward from the retained source
boundary; positive builds outward. A negative thickening follows the opposite
side of its oriented source faces or sketch workplane.

For example, this 20 × 30 × 10 mm box has a 2 mm inward wall with an open top:

```json
{"schema_version":1,"units":"mm","parameters":{"wall":-2},"features":[
  {"id":"body","type":"box","size":[20,30,10]},
  {"id":"housing","type":"shell","input":"body","thickness":{"parameter":"wall"},
   "faces":{"type":"geometric","feature_id":"body","surface_kind":"plane",
            "expected_count":1,"normal":{"vector":[0,0,1],"tolerance":0.000001}},
   "join":"intersection"}
],"output":"housing"}
```

Its exact material volume is `6000 − 16 × 26 × 8 = 2672 mm³`. The original
box remains an editable upstream feature. Replacing `faces` with `[]` leaves a
sealed cavity and volume `6000 − 16 × 26 × 6 = 3504 mm³`.

Face rules are persistent geometric design references. Required fields are
`type: "geometric"`, `feature_id` equal to the operation's input,
`surface_kind` and explicit `expected_count` (1–10000). Optional predicates are:

- `normal: {vector, tolerance}`: oriented planar-face normal; the tolerance is
  radians. The vector is normalized and its sign matters. A +Z rule does not
  select the opposite −Z planar face. Nonplanar normal rules are rejected.
- `center: {point, tolerance}`: surface centroid and distance tolerance in mm.
- `area: {value, tolerance}`: surface area and tolerance in mm². Explicit
  arithmetic expressions use the `mm2` unit.

`faces` accepts one rule or an array of up to 64 rules. Every rule must match its
own cardinality. Missing, ambiguous and insufficient matches return
`selection_missing`, `selection_ambiguous` and `selection_count_mismatch` with
source-qualified candidates. Overlapping rules selecting a face twice fail
explicitly. The empty array is supported only for a sealed `shell`.

`cad_query` topology includes a suggested face `selector` only when its bounded
centroid/area/normal rule is unique in that source feature. Current evaluated
face picks resolve through `cad_resolve_selection` to the same rule. Face IDs
remain local to the qualified evaluation. Assembly picks identify upstream
source parts and do not suggest rules on the placed aggregate.

Surface thickening supports exact planar and curved source patches; a cylinder's
lateral face, for example, thickens into a tube with its two end walls. Explicit
selected faces must form one connected, manifold open patch. Complete closed
boundaries and disconnected selections fail, directing closed-body work to
shelling. Joining adjacent selected planar faces supports a bent wall without
introducing sheet-metal unfolding or bend allowances.
Sketch profiles preserve all boundary holes. When a derived planar sketch has
multiple disjoint regions, thickening retains one independent solid per region.

Shelling requires one source solid and must retain a face. Separate parts should
be shelled upstream of patterns or assemblies. Offsets retain each input solid;
they do not fuse them. Every offset must preserve one valid solid per source
and change its volume in the requested direction. Shelling, offsetting and
thickening validate closed positive-volume B-reps and reject self-interfering
results. Exact material containment also verifies that signed offsets and shells
stay on the requested side of their source. A mathematically valid offset body
that inverts after excessive contraction is rejected. Collapsed geometry,
invalid cavities and kernel construction failures
retain the committed revision. There is no silent healing, join substitution,
feature removal or mesh fallback. As with OCCT's offset algorithms, sufficiently
small features, unsuitable surface continuity and excessive distances may fail.

Native face/edge generation and modification history is evaluation evidence;
it is preserved through exact caches and is never advertised as stable naming.
Revision-pinned components rewrite face-rule feature IDs with their captured
dependency closures. Geometry-changing shell/offset/thicken outputs do not
inherit an unchanged imported purchased-part identity.

Development validation uses `ctest --test-dir build -R shell_offset
--output-on-failure`. The dedicated suite covers analytic signed thicknesses,
joins, source preservation, face-reference failures, collapse rejection,
connected curved/planar patch thickening, cold rebuild, selected-face resolution,
rollback, independent exports, self-contained components and durable jobs.
Platform/host release acceptance remains recorded separately in `HANDOFF.md`.
