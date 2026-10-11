# Solid operation parity increment

Implemented native C++20 operations use pinned OCCT 8.0.1. They retain editable
feature IDs, parameter expressions, component snapshots, geometric selectors,
exact feature caches and transaction rollback. No sibling runtime or Python code
is part of the implementation. These are solid-modeling capabilities, not a
claim of complete build123d API parity or host/release readiness.

## Scaling

`scale` requires `input`, `origin` and `factors`. `factors` is one dimensionless
scalar for uniform scale or three dimensionless scalars for scaling the world
X/Y/Z axes independently. Coordinates obey `origin + factors * (point-origin)`.
Factors accept parameters/expressions and must lie in `[0.000001,1000000]`.
Use `mirror` for reflection; zero or negative scale is rejected.

```json
{"id":"sized","type":"scale","input":"imported","origin":[0,0,0],"factors":[2,1,0.5]}
```

Inputs are earlier solids, including opaque imported STEP and compound solids;
assemblies remain compositions of source parts. Scaling changes geometry and
therefore does not inherit unchanged purchased-part identity. Uniform scaling
retains analytic representations where OCCT permits; nonuniform scaling uses
exact affine transformations of native B-reps and can produce rational surfaces.
No tessellation substitutes for the solid.

## Drafting existing faces

`draft` requires solid `input`, geometric `faces`, signed `angle_deg`, pull
`direction`, and `neutral_plane` (a complete workplane). The pull direction must
cross the neutral plane. Angle magnitude is at least 0.00001 and below 89 degrees.
Positive angles remove material on the pull-direction side of the neutral plane;
negative angles add material. The face's intersection with the neutral plane is
held fixed. Draft applies to existing planar, cylindrical and conical faces;
OCCT propagates it through tangent-connected faces. Unsupported surface kinds,
selection ambiguity and transformations requiring invalid/topology-changing
geometry fail with the feature identity and leave the revision unchanged.

## Chamfer side and angle

The existing symmetric `chamfer` contract remains unchanged. Add either
`distance2` or `angle_deg`, together with `reference_face`, for a nonsymmetric
chamfer. The fields are mutually exclusive. `reference_face` is a geometric
face selector with `expected_count:1`; every selected edge must be incident to
that face. `distance` is measured on the reference face, and `distance2` on the
other adjacent face. Angle is measured from the reference face and must lie
strictly between 0 and 90 degrees. For a perpendicular edge, the other distance
is `distance * tan(angle_deg)`. Reference-side identity never comes from a
transient face index. A different side must be authored explicitly.

## Twist extrusion

`twist_extrude` requires sketch `input`, signed `distance` and `angle_deg`.
Optional world-space `center` defaults to the sketch workplane origin and must
lie on its plane. Travel is along the sketch normal times distance; rotation is
right-handed about that same normal, independently of the sign of travel.
Magnitude of travel is at least 0.00001 mm; rotation is bounded to 16 turns.
Zero angle is an ordinary exact prism. The native auxiliary-helical-spine law
rotates the complete profile during travel, including explicit holes. Multiple
sketch regions are supported. Native sweep approximation/tolerances apply; failed
or self-invalid solids are rejected without removing boundaries or features.

## Lofts with holes and vertex endpoints

`loft.sections` retains references to sketch features. Each sketch must contain
exactly one material region; sections must have equal numbers of interior
boundaries. The outer boundaries are lofted, corresponding hole boundaries are
lofted separately and their exact solids are subtracted. A hole leaving the outer
loft or touching/overlapping another hole between sections is rejected. Independent
exact separation and Boolean overlap checks guard hole tracks; native
self-interference checks also guard each boundary and the final solid.

Optional `start_vertex` and `end_vertex` are world-space scalar triplets. They
extend the sequence before/after the sketches. One sketch is permitted when at
least one vertex is supplied; otherwise at least two sketches are required.
At most 32 sketch sections are permitted; `ruled` retains its existing meaning.
A pyramid/cone can thus be authored with one section and one vertex. Every
boundary uses the same endpoint vertices, so pinched hollow results must still
pass native solid validity checks; vertices cannot occur between sketch sections.

One hole has unambiguous correspondence. Two or more holes require `hole_order`,
an array with one row per sketch section. Each row lists every hole in the
intended common track order. A landmark is `{point:[x,y,z],tolerance:mm}` and
must uniquely match the centroid of the planar region enclosed by one unused
hole. This is geometric design intent, not an enumeration index or inferred
nearest-neighbor match. Parameterize landmarks alongside section geometry.
Missing, duplicate or ambiguous matches fail explicitly.

```json
{"id":"tube","type":"loft","sections":["bottom","top"],"ruled":true,
 "hole_order":[
   [{"point":[-3,0,0],"tolerance":0.000001},{"point":[3,0,0],"tolerance":0.000001}],
   [{"point":[-3,0,10],"tolerance":0.000001},{"point":[3,0,10],"tolerance":0.000001}]]}
```

## Numerical evidence and validation

An affine-scaled analytic cylinder exposed inaccurate Gauss-only volume
integration in OCCT despite its small reported error estimate. For radii 10/15 mm
and height 5 mm, analytic volume is `750π = 2356.1944901923448 mm³`.
At requested relative epsilon 1e-9, Gauss-only reported `2356.1941555768203`,
while Gauss-Kronrod reported `2356.1944898849015`. The integrated summary uses
Gauss-Kronrod per rational support face, fixed-degree Gauss for planar faces
bounded only by lines, and adaptive Gauss for other polynomial/analytic faces.
Exact knot spans and an exterior common reference per closed solid keep rational first-moment
integration within the unchanged worker budget. Strict analytic tests remain
unchanged. Exact geometry, not mesh integration, determines the reported mass.

OCCT's normalized property accumulator loses nonzero first moments when an
intermediate signed mass becomes zero. The summary therefore accumulates signed
mass and first moments with compensated sums, dividing only after the complete
positive material mass is validated. A 10 by 20 by 30 mm box independently checks
the analytic centroid `[5,10,15]` through native, snapshot, worker, STEP import
and source-deleted readback paths; the prior result was `[1.25,7.5,15]`.

An individual curved face can also have zero signed mass and nonzero first
moments, which OCCT's normalized per-face result cannot expose. The complete
solid is retried at up to 16 nearby exterior common references when that occurs.
A planar face through the reference is accepted only when its point flux is
identically zero. Unresolved conditioning returns explicit `kernel_failure`.
The cubic roof `z=(2x/3-1)^3` over `x=0..3`, `y=0..1`, with bottom `z=-1`, has
analytic mass 3 and first moments `27/5`, `3/2`, `-9/7`. Its regression deliberately
produces a zero-mass top-face contribution and verifies recovery, including an
independent STEP reader. No centroid is guessed and no tolerance is increased.

Disconnected solid occurrences integrate near their own bounds before their
positive masses and first moments are combined with compensated sums. One
assembly-wide reference caused 7.28e-5 mm centroid drift for two 1 mm cubes at
X=0 and X=1e6 mm; local integration yields the analytic X centroid 500000.5 mm.
Overlapping/coincident instances retain their separate material contributions.
Independent regressions cover distant and unequal-size solids, reversed instance
order, rotated nested assemblies, snapshots, workers and cold STEP reconstruction.

The numerical increment `2304c65`, integrated as `79e448c`, passed its final
seven focused suites in **47.46 s**, including **459 parity-solid checks**, artifact native/MCP
centroids, text placement, surface thickening, transformed rational assemblies
and performance checks. The curved-pipe build and summary took **0.388 s** and
its ordinary create call passed the unchanged 30-second worker limit.

The local-solid increment `6e835de`, integrated as `1f766bb`, passed **12/12
focused suites in 67.18 s**, including **579 parity-solid checks**, STEP import,
all four assembly suites, surfaces, text, artifacts and performance. Its pipe
build/summary took **0.337 s**, and the separated-cube centroid is exact at
floating precision. Complete integrated acceptance is recorded in HANDOFF.

`parity_solids` tests analytic volumes and bounds, signed draft/twist geometry,
explicit chamfer side, annular and pointed lofts, explicit multiple-hole
correspondence, native history, STEP readback, cached/cold reconstruction,
parameter edits, failed-edit rollback, source revision preservation and portable
components (including remapped reference faces). The independent developer script
`tests/parity_solids_schema_tests.py` checks live Draft 2020-12 contracts and actual
native calls. Actual executed test evidence is recorded in the integration handoff.

## Historical incremental evidence — 2026-10-10

macOS arm64, pinned OCCT 8.0.1 / FreeType 2.14.3, Release build:

- Native build completed. `parity_solids` passed **359 checks in 18.74 s**.
- The final selected CTest run passed **18 of 19 suites in 160.36 s**: model,
  geometry, transactions, protocol, topology, modeling, modeling_curves,
  richer_modeling, parity_solids, sketch_operations, shell_offset, surface,
  cache, dependency_cache, purchased_part, step_import, component and
  performance_geometry. `performance_geometry` passed in 15.73 s.
- `app_protocol` failed its unchanged discovery-size assertion:
  **488,609 bytes**, exceeding the **476,160-byte** catalog budget. Integration
  must compact discovery losslessly; this increment does not waive that gate.
- Independent Draft 2020-12 validation passed **95 checks / 9 native models**
  using `tests/parity_solids_schema_tests.py`. `git diff --check` passed.
- Full rational-surface centroid integration is measurably more expensive:
  `modeling_curves` took 29.21 s and `richer_modeling` 15.07 s. Accuracy is
  retained, including analytic translated elliptical-cylinder centroids.
  Exact spline-span integration subsequently improved `parity_solids` from
  18.74 s to 15.52 s with unchanged tolerances. The follow-up regression run
  passed 7/7 suites in 81.66 s: geometry, modeling_curves, richer_modeling,
  parity_solids, shell_offset, surface and performance_geometry. Curves and
  richer modeling remained 29.26 s / 14.79 s; performance_geometry was 15.49 s.
  In an independent translated elliptical-cylinder probe, span integration
  reduced summary time from 226 ms to 20 ms and mass error from 3.1e-7 mm³
  to approximately 5e-12 mm³, preserving the centroid to floating precision.

No Windows/Linux, package installation, host GUI, signing or release gates are
established by this local increment.
