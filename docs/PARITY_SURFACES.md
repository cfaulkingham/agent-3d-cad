# Surface construction, projection and exact curve outputs

This increment adds native surface workflows and corrects thickening of trimmed
polynomial patches. All geometry stays in the serial OCCT worker. Every feature
has the usual stable feature ID, parameter dependencies and atomic failure behavior.
This document describes supported bounds, not universal surface-kernel robustness.

## Geometry kinds and STEP

`curve` features use `{id,type:"curve",path:{type:"wire",segments:[...]}}`.
Segments use the existing world-coordinate line, three-point arc, Bezier and
interpolating-spline syntax. Open and closed connected wires are valid outputs.
They report zero solid count, volume and area; their center is weighted by exact
curve length. Topology, edge-polyline viewing, snapshots and exact STEP export
work. Curve outputs cannot be solid operands, assembly/component roots, print
meshes or manufacturing inputs. `curve_project` is also a curve feature.

`cad_import` has optional `geometry:"solid"|"surface"`, defaulting to `solid`.
Surface imports create an `import_step_surface` feature containing original STEP
bytes and SHA-256. The entire file must consist of valid finite faces/shells;
solid files and mixed material/nonmaterial files fail instead of silently
extracting boundaries. Surface imports do not allow `solid_indices` or purchasing
metadata. Closed shells stay nonmaterial until explicit `surface_solid`.
A single imported closed shell is accepted by that operation. Import never
recovers source feature history. Both STEP feature types exclude captured bytes
from metadata budgets and remain subject to worker memory/time bounds.

## Exact contour trims

`surface_trim` keeps the existing `u_range`/`v_range` form and also accepts
`boundary:[2D segments]` plus optional `holes:[[2D segments],...]`. The two forms
are mutually exclusive. Coordinates are dimensionless UV parameters of the
source surface, including dimensionless expressions. Boundaries must close;
loops must be simple, holes contained and mutually disjoint. Winding does not
change explicitly authored hole intent. Up to 16 holes are supported.

Each contour is an exact curve on the source surface. Trim validation includes
containment against the existing face, so a second trim cannot fill a previous
hole or restore removed material by merely staying inside its UV bounding box.
There is no planar approximation of curved trim boundaries.

## Constrained filling

`surface_fill` requires `boundaries` (2–32 ordered constraints) and numeric
`tolerance` (1e-7 to 1e-3 mm). Optional `points` supplies up to 64 interior world
points. `angular_tolerance` and `curvature_tolerance` have the same numeric bounds
in radians and inverse millimeters, defaulting to 1e-4.

A boundary is either `{curve:<3D segment>,continuity:"C0"}` or
`{input:<feature>,edge:<geometric selector>,face:<geometric face selector>,
continuity:"C0"|"G1"|"G2"}`. `face` is mandatory for G1/G2. Both selectors must
expect exactly one entity. The edge must belong to the selected support face.
Optional `reverse:true` reverses that boundary's authored orientation. Boundaries
must connect in order and close explicitly; no missing edge is synthesized.
C0 passes through the edge, G1 additionally matches the support tangent plane,
and G2 additionally matches curvature. Unsupported/ambiguous references fail.

When every constraint selects the complete single boundary wire of the same
explicit support face, an exact private copy is tried first. Each boundary edge
must occur exactly once, with consistent authored winding. All interior points
must lie on that trimmed face within the positional tolerance, and the same
independent validity and continuity checks apply. Reversed winding reverses the
result face and its signed thickening direction. Partial boundaries, holes,
repeated seam edges, mixed supports and off-support points use the general
solver; an interior point requesting a bulge is preserved. At exactly coincident
neighboring G1/G2 endpoints, support tangent planes farther apart than twice the
angular tolerance are rejected as geometrically incompatible before solving.

The general variational filling solver is approximate within explicit tolerances. Its
reported positional/angular/curvature errors are checked, then the constructed
surface is independently sampled against every boundary for position, tangent
plane and the curvature tensor, including its principal directions. Conflicting
constraints or solver failure reject the feature. No constraint is silently dropped. Native B-rep and interference
validation still apply. As with other numerical geometry operations, these checks
are not a formal global continuity proof between every sampled point.

The first native solve requests an average of 50 initial discretization points
per curve, at most four refinement passes, and degree-14/16-segment spline approximation. If native
completion or independent verification rejects that construction, one fresh
solve requests an average of 75 initial points, at most five passes, and degree-16/32-segment
approximation. Each attempt rebuilds private copies of every original constraint;
no failed result supplies modified edges to the next attempt. Both use one tenth
of the public positional/angular/curvature tolerances internally and the same
unchanged public acceptance checks. A first accepted result is retained. Input
validation errors are not retried. Two rejected solves report both the original
failure and final failure with attempt settings. These point counts are native
initial resolution settings, not a cap on adaptive discretization; worker
resource limits still apply to the entire feature operation.

OCCT 8.0.1's `GeomPlate_BuildPlateSurface::Perform` exits at its iteration limit
even when `VerifSurface` reports unmet objectives. `BRepFill_Filling` reports that
plate error and separately approximates the plate as a spline, so completion
alone establishes neither curve interpolation nor continuity. The original
1e-5 mm cylinder regression retains its public tolerances, alongside the 1e-6 mm
case. Both now verify the fully constrained exact-support result independently
after STEP. Linux CI demonstrated why refinement alone is insufficient: the
same exact-cylinder constraints produced plate positional errors of 1.508e-5 mm
and then 3.579e-5 mm on the denser retry. Separate G1/G2 interior-bulge regressions
exercise the general solver and independently check the boundary and interior
point after STEP. General fills remain bounded numerical solves; exact support
reuse is not a claim that every solvable constraint set will converge.

The pinned OCCT 8.0.1 call path is `BRepOffsetAPI_MakeFilling::Add` →
`BRepFill_Filling::Build` / `AddConstraints` → `BRepFill_CurveConstraint` →
`GeomPlate_CurveConstraint` / `GeomPlate_BuildPlateSurface`. It forwards the
`GeomAbs_Shape` enum as a derivative-order integer. `GeomAbs_G2` is 3, while
GeomPlate expects order 2 for curvature. The adapter maps that solver argument
to 2 and retains G2 for independent checks. A nonplanar quarter-cylinder test
exports STEP, independently reads it and checks boundary positions, tangent
planes and directional curvature against a radius-10 mm analytic cylinder.
The tolerance is 1e-5 mm, 1e-3 radians and 1e-3 /mm. An interior point is checked
separately; boundary G2 does not imply an exactly cylindrical interior.

## Gordon curve networks

`surface_gordon` requires `u_curves`, `v_curves` and numeric `tolerance`
(1e-7 to 1e-3 mm). Each family contains 2–16 entries: world-coordinate line,
three-point arc, tangent arc, weighted or unweighted Bezier, interpolating spline
(including `periodic:true`), or `{type:"wire",segments:[...]}` with 1–64 exact
connected segments. A closed wire can contain rational arcs, such as two
semicircles defining a complete circle. Source curves and their rational weights
remain unchanged in the editable document.

An endpoint profile may instead be `{type:"point",point:[x,y,z]}`. Only the first
or last entry of a family may collapse, and every family needs a noncollapsed
curve. A point-ended cone is supported without replacing its apex with a tiny
edge. Collapsed profiles cannot occur in a cyclic transverse family.

Native curve/curve intersections establish every crossing. Missing crossings,
overlapping curves, multiple isolated crossings and grids whose crossings cannot
be monotonically ordered fail explicitly. Open curves must be covered from one
endpoint to the other. Curve orientation is reconciled to the authored family
order. All noncollapsed curves in a family must consistently be open or closed.
Closed profiles have one implicit closing copy of the first transverse curve;
that copy establishes the seam without dropping the remainder of the profile.

Optional `u_parameters` and `v_parameters` give desired surface crossing stations
rather than requiring every input curve to share one native parameterization.
Omitting them uses uniformly spaced stations. Each supplied array strictly
increases from 0 to 1, has minimum spacing 1e-6 and matches the transverse family
count, plus one implicit seam station when curves in that direction are closed.
For example, four guides around closed U profiles need five U stations.

Already compatible polynomial curves with explicit stations retain the exact
B-spline degree-elevation/knot-insertion construction. General networks use
monotone cubic parameter maps and adaptively refined quintic B-splines to compose
the original curves. Approximation is checked within each native knot span;
one eighth of the public tolerance is reserved for that composition, tested at
15 interior samples per candidate interval before refinement. Bounded
local cardinal functions construct the Gordon sum: U-family interpolation plus
V-family interpolation minus the tensor interpolation of crossings. Two-profile
interpolation remains linear, preserving ruled cylinder and cone cases. Closed
cardinal seams have matching derivatives. This is a full curve-network operation,
not a four-boundary fill.

The completed surface is checked against every original reparameterized curve
at 17 samples across every resulting knot interval, using the unchanged public
tolerance.
General rational/periodic construction produces a native B-spline approximation;
it does not claim exact rational or analytic support identity. These finite
checks supplement native validity/interference checks, not a global approximation
proof. Limits remain degree 25, 512 distinct knots per axis and 65,536 control
points, with at most 18 local refinement subdivisions. Difficult curves,
unresolved intersections, excessive refinement and invalid/crossed resulting
surfaces fail without publishing a revision.

## Curved projection

`curve_project` and `surface_project` require `input`, `target`, `faces` and
world-space `direction`. Input is an earlier exact curve or sketch; target is
an earlier surface or solid. `faces` is one geometric face selector with
`expected_count:1`.

Optional `branch:"unique"|"nearest"|"farthest"` selects a forward-ray sheet.
Omitted `branch` means `unique` and preserves the previous ambiguity error.
`nearest` and `farthest` choose the smallest or largest nonnegative ray distance
on that selected face, so complete cylinders and spheres can expose distinct
front/back projections without confusing face identity with branch identity.
Each source edge has 65 ray witnesses, with forward reach bounded to 1e9 mm
and 1e-7 mm tolerance at the source. Every witness must reach the requested sheet,
and one complete exact
OCCT projected wire must match all selected witnesses. Tangent/singular hits,
missing coverage, branch discontinuities and unresolved/split wires fail. Up to
64 candidate wires may be examined. Matching uses 1e-6 mm point/wire distance;
projected region candidates also check 17 witnesses per boundary edge against
the face. This is directional projection, not Euclidean
nearest-point projection.

Open wires use `curve_project`; `surface_project` requires closed boundaries and
at most one source sketch region, retaining holes. For sketches, intersection
of the exact forward region prism with the target builds periodic seam topology.
Adjacent partitions on the same support are unified with edge fitting and spline
concatenation disabled; private input, native validity, self-interference and
containment checks remain mandatory. This supports holed spherical regions
crossing the original parameter seam. Closed curve inputs retain the direct
bounded-wire construction and can still fail on unsupported seam configurations.

Projection preserves the curved support and its pcurves. It is not flattening or
geodesic wrapping. Complex folds, multiple faces, partial coverage and unresolved
sheet changes can fail explicitly. Finite ray/branch witnesses supplement exact
native construction; they are not a global visibility proof.

## Curved thickening

Before thickening, private polynomial support surfaces and boundary curves are
restricted to their existing parameter intervals using exact B-spline conversion
and knot insertion. UV parameters, source boundaries, source revisions and
requested join/thickness remain unchanged. This avoids OCCT's unorientable
side-wall construction on the trimmed biquadratic crown in the freeform panel.
The composed kernel history maps normalized entities back to their source
features. Both thickness signs and both join modes are regression cases.
All previous closed-solid, positive-volume and interference checks remain active;
no shape healing or tolerance increase is used.

## Verification

`parity_surfaces` exercises the crown, independent STEP readers, explicit
surface import and material rejection, exact contour holes and containment,
C0/G1/G2 filling, interior spline-network interpolation, different curve
parameterizations, rational arc/weighted profiles, exact closed-wire and periodic
spline profiles, collapsed cone endpoints, complete cylinder/sphere open and
holed projections on both forward branches, tangent/branch-change rejection, portable component support remapping, native snapshots/cache dependencies and worker rollback. Existing
`surface` and `shell_offset` suites cover earlier analytic and material checks.
Executed counts/results are recorded by the integration handoff after testing.

The generalized-network tests use analytic plane/cylinder/cone oracles and fresh
STEP readers. Periodic spline geometry is checked against a separately exported
source curve throughout its length. STEP area checks request converged native
quadrature explicitly; fixed-order area integration is insufficient for highly
nonuniform spline parameterizations. The dedicated Draft 2020-12 script validates
actual creates, queries, meshes, STEP captures, branch edits and historical reads,
including rejected closed-schema fields and failed-edit rollback. Reversed target
face normals and both signs of subsequent thickening are checked independently;
both spherical projection branches retain outward orientation after STEP.
