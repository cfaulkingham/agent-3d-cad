# Exact sketch operations and solid transforms

Native feature documents can reuse, transform and derive exact closed planar
regions. Every operation remains a feature with earlier source IDs, evaluated
parameters and revision-qualified provenance. Geometry stays in the native OCCT
8.0.1 kernel. No model-supplied code or installed language runtime is used.

## Feature contracts

| Type | Required fields | Optional fields |
|---|---|---|
| `sketch_fuse`, `sketch_cut`, `sketch_intersection` | `id`, `type`, `left`, `right` | Coplanar sketch inputs; exact material union, subtraction or intersection |
| `sketch_offset` | `id`, `type`, `input`, `distance` | `join`: `arc` (default) or `intersection` |
| `sketch_fillet` | `id`, `type`, `input`, `radius`, `vertices` | Circular 2D corner fillets |
| `sketch_chamfer` | `id`, `type`, `input`, `distance`, `vertices` | Equal-distance 2D corner chamfers |
| `sketch_transform`, `sketch_instance` | `id`, `type`, `input` | `rotation`, `translation`; rotation precedes translation |
| `sketch_mirror` | `id`, `type`, `input`, `plane` | Exact reflection in an authored workplane |
| `sketch_face` | `id`, `type`, `input`, `faces` | Reuse selected coplanar solid faces as a sketch |
| `sketch_projection` | `id`, `type`, `input`, `faces`, `workplane` | Orthogonal projection of selected planar face boundaries |
| `mirror` | `id`, `type`, `input`, `plane` | Exact solid reflection |
| `split` | `id`, `type`, `input`, `plane`, `keep` | `keep`: `both`, `top` or `bottom` |
| `intersection` | `id`, `type`, `left`, `right` | Exact common material of earlier solid features |

`plane` and `workplane` use the existing `{origin,normal,x_direction}` contract.
All coordinates are model millimeters; direction vectors are dimensionless.
Scalar fields accept numeric values or existing bounded parameter expressions.
Sketch offsets and extrusion distances are signed; magnitudes must be at least
0.00001 mm. Fillet radius and chamfer distance must be positive and at least that
minimum. These new sketch feature families are intermediates: a model output,
assembly part, solid mirror or boolean operand must still be solid geometry.

## Material regions, holes and planes

A sketch may contain 1–128 exact planar face regions. A hole remains an interior
wire of its containing face. Boolean operations preserve exact circular,
Bezier and spline curves; boolean output is not sampled into a polygon. Disjoint
union regions remain separate and `extrude`/`revolve` process all regions. A
single-region boolean result is normalized to a face for existing loft consumers.
Loft still requires one boundary per section and rejects holes or multiple
regions explicitly.

Booleans require coplanarity within 1e-7 mm and parallel or antiparallel normals
within 1e-9 of unit dot product. Opposite authoring normals are accepted. The left
sketch determines the output workplane and extrusion direction. New faces and
compound regions must have valid B-reps, positive finite area and no loose
vertices, edges or wires. Pairwise positive-area overlap is rejected. Empty
subtraction/intersection and contact-only intersection fail; an empty sketch is
never committed. Valid disjoint material is not silently discarded.

Positive offset expands outer boundaries and contracts holes; negative offset
contracts outer boundaries and expands holes. Arc joins create exact circular
corner arcs, while intersection joins extend adjacent curves to meet. Complete circular boundaries use an exact analytic radius change and reject
nonpositive resulting radii before entering native offset algorithms. Other exact
closed offset loops are reconstructed by containment into material regions and
holes. The nesting step rejects crossing, coincident or touching boundaries and
checks validity and self-interference. Results that collapse or cannot be
represented as valid material regions fail with the feature ID. No wire healing
or geometry substitution occurs.

Transforms rotate in world coordinates about the authored axis and origin,
then translate. Mirroring transforms the source workplane normal as a vector,
so mirroring an XY sketch in YZ keeps its +Z extrusion direction. The reflection
preserves exact curves and holes. Sketch instances are saved dependent copies,
not assembly occurrences.

## Selecting corners and faces

`vertices` is `"all"` or a geometric point selector:

```json
{"type":"geometric","feature_id":"profile","point":[0,0,0],
 "tolerance":0.000001,"expected_count":1}
```

The point is in world coordinates, tolerance is positive in millimeters and the
feature ID must equal the operation input. Eligible corners have two distinct
incident edges. A closed circle seam is not a corner. A missing, ambiguous or
incorrect-count selection fails deterministically with actual and expected
counts. Corner indices are never saved as persistent references. Unsupported
corners, excessive radius/distance and geometric failures preserve HEAD.

`faces` uses the shared feature-qualified `face_selection`: one geometric face
selector or 1–64 selectors, each with its own required `expected_count`.
Selectors specify `surface_kind`, optionally oriented normal, center and area.
Normal predicates have angular tolerance in radians; center tolerance is in
millimeters and area tolerance is in square millimeters. Duplicate selections
across selectors fail. See [SHELL_OFFSET_THICKEN.md](SHELL_OFFSET_THICKEN.md).

`sketch_face` requires coplanar selected planar faces and derives its workplane
from the first selected face's oriented normal. It retains original exact face
boundaries, including holes. Multiple selected regions must not overlap. The
selector replays against the current source geometry after parameter edits;
evaluated face IDs remain local to that evaluation.

`sketch_projection` projects each selected planar face boundary along the target
workplane normal using native curve projection. Tilted circular faces yield
exact conics; Bezier/B-spline boundaries remain exact curves. Interior loops and
disjoint regions are rebuilt without discarding holes. Selected source faces
may occupy different planes; every projected region must be valid and not overlap
another projected region. Nonplanar faces, edge-on faces, degenerate boundaries,
empty projections, touching/crossing loops and overlapping projected material
fail explicitly. This is face-boundary projection, not arbitrary open-edge
projection or a silhouette of a curved solid.

## Solid mirror, split and intersection

Solid mirror applies an exact native reflection and preserves positive-volume
closed solids. It does not inherit an unchanged purchased-part identity because
reflection changes the source geometry's handedness.

Split uses the authored infinite plane and native halfspaces. `top` means the
side where `plane.normal dot (point - plane.origin) > 0`; `bottom` is the negative
side. `both` returns separate solids with the cut faces preserved. Both sides
must contain positive-volume closed material even when only one side is kept.
A plane that misses or only touches the source fails explicitly. Reversing the
normal swaps `top` and `bottom`. No finite clipping box truncates large models.

Solid intersection requires a valid positive-volume exact result. A disjoint or
contact-only common result fails. Inputs remain independent saved features.

## Persistence and verification

All new features use existing `input`, `left` and `right` dependency contracts,
so parameter edits invalidate affected geometry and projection cache entries.
Cache restoration validates every planar region and reconstructs each derived
workplane from the saved feature and source planes. Cold rebuilds, snapshots,
reopened documents and STEP exports all preserve editable source and geometry.
Provenance records source dependencies and evaluation-scoped native operation
history; histories are evidence about a particular build, not stable topology
naming.

The `sketch_operations` CTest uses analytic area/volume and bound expectations,
independent STEP readback and B-rep validation. It covers coplanarity/orientation,
exact curves, interior holes, multiple disjoint regions, offsets, selected/all
corners, ambiguity/missing failures, reflected extrusion normal, oriented
halfspace splits, solid intersection, face reuse/projection, snapshot restoration,
parameter-cache invalidation and service preview/edit failure rollback/reopen.
