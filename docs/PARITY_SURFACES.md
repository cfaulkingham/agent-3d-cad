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

The variational filling solver is approximate within explicit tolerances. Its
reported positional/angular/curvature errors are checked, then the constructed
surface is independently sampled against every boundary for position, tangent
plane and the curvature tensor, including its principal directions. Conflicting
constraints or solver failure reject the feature. No constraint is silently dropped. Native B-rep and interference
validation still apply. As with other numerical geometry operations, these checks
are not a formal global continuity proof between every sampled point.

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

`surface_gordon` requires `u_curves`, `v_curves`, `u_parameters`, `v_parameters`
and numeric `tolerance` (1e-7 to 1e-3 mm). Each family contains 2–16 nonperiodic
polynomial line, Bezier or interpolating-spline segments. Each curve runs in its
named direction and is normalized to [0,1]. `u_parameters` gives the crossing
stations of the V curves on every U curve; `v_parameters` gives the crossing
stations of U curves on every V curve. Stations strictly increase, include 0
and 1 and have a minimum separation of 1e-6. Counts match the transverse family.
Author curves in compatible directions and parameterizations; reversed curves
or inconsistent crossing positions fail with the offending curve indices.

The implementation elevates and inserts knots into exact compatible B-spline
bases and constructs the Gordon sum: U-family interpolation plus V-family
interpolation minus the tensor interpolation of crossings. It does not replace
the interior network with a four-edge fill. The resulting surface interpolates
complete polynomial curves; independent samples verify tolerance and reject
ill-conditioned station sets. Bounds are degree 25, 512 distinct knots per axis
and 65,536 control points. Rational arcs/weighted curves and periodic networks
remain unsupported, as does automatic reparameterization of incompatible curves.

## Curved projection

`curve_project` and `surface_project` require `input`, `target`, `faces` and
world-space `direction`. Input is an earlier exact curve or sketch; target is
an earlier surface or solid. `faces` is one geometric face selector with
`expected_count:1`, preventing a guessed choice between front/back faces.
Projection is directional along the forward ray. Source samples must have one
forward intersection, and the exact OCCT projection must produce one wire for
each source wire and preserve closure. Missing, backward, ambiguous or split
projections fail. Open wires use `curve_project`; `surface_project` requires
closed boundaries and at most one source sketch region, retaining its holes.
The projected region must remain inside the selected target face.

Projection is onto the exact curved support and preserves its pcurves. It is not
flattening, geodesic wrapping or a nearest-point projection. Complex folds,
seams, tangent rays and partial coverage can fail explicitly. Finite ray samples
supplement native exact projection checks; they are not a global visibility proof.

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
C0/G1/G2 filling, interior spline-network interpolation, curved open/closed
projection, portable component support remapping, native snapshots/cache dependencies and worker rollback. Existing
`surface` and `shell_offset` suites cover earlier analytic and material checks.
Executed counts/results are recorded by the integration handoff after testing.
