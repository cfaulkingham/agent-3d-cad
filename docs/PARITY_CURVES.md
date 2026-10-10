# Editable curves and arc-length queries

All curve construction, evaluation and trimming stays in the serial native OCCT
worker. Curve outputs have no material volume. New curve types participate in
ordinary feature hashes, parameter dependency tracking, snapshots and atomic
worker edits. Geometric edge selectors remain source-feature rules, not stable
edge enumeration IDs.

## Authoring and constraints

`curve_helix` takes a `frame` workplane, positive `radius` and `pitch`, and
`turns` in [0.0001,256]. `handedness` is `right` (default) or `left`. All numeric
fields accept dimension-checked scalar parameters/expressions; turns are
dimensionless. The frame origin lies on the axis; its normal is the direction
of positive axial advance and X direction fixes the start radius. Radius and
pitch are at least 1e-5 mm; height is at most 1e6 mm and length at most 1e7 mm.
The cylindrical support and linear UV curve encode the helix exactly; OCCT also
builds its required 3D spline representation within 1e-7 mm. The implementation checks analytic length; independent regression tests also
check differential geometry and STEP round trips.

Public Bezier segments now accept `weights`, one dimensionless scalar per pole,
in [1e-8,1e6]. This applies to world curves, sketch profiles, sweeps, fill
boundaries and UV trims. Weights remain rational intent; they are not silently
converted to unweighted curves. Gordon networks still require polynomial curves
and explicitly reject weighted segments. Captured font/DXF paths keep their
separate validated source contract.

A `tangent_arc` segment has `start`, `end` and a dimensionless `tangent` at start
in the same 2D or 3D coordinate convention as its containing path. Degenerate
constraints fail. `curve_tangent_line` references an earlier curve with `input`,
`position` and positive `length`; `curve_tangent_arc` instead supplies a world
`end` point. `position` is a normalized arc-length fraction. Optional `reverse`
reverses the initial tangent. Arc endpoints must be distinct and off the initial
tangent line. The chosen circle/line is independently checked at its start.

`curve_extract` takes `input` and `edges`, a selector with `expected_count:1`,
and captures an editable exact edge reference as a one-edge wire. Input may be
an earlier nonassembly solid, sketch, surface or curve. Missing and ambiguous
selectors fail.

`curve_trim` takes an earlier `input` curve and `start`/`end` normalized
arc-length fractions, with 0 <= start < end <= 1. It retains native curve segments and their direction, including a trim spanning
several edges. Existing synchronized surface/pcurve representations are retained
for helices and projected edges instead of discarding their surface intent. It does
not wrap across a closed wire's start station; use explicit intervals. A trim
must retain positive length above kernel resolution. Curves containing multiple
wires need a unique extraction before these one-path operations.

## Sampling and selection

`cad_query` accepts `kind:"curve"` and optional `curve` options:
`{stations:[0,...,1],edge:<geometric selector>}`. Stations are 1–257 numeric
fractions; default [0,0.25,0.5,0.75,1]. A curve feature with one wire needs no
selector. Face/solid sampling requires a unique explicit `edge` rule.

The result carries the committed document/revision, feature, evaluation ID,
summary and topology plus `curve`: units, total length, closure, parameterization
`normalized_arc_length`, and samples containing fraction, point, oriented unit
tangent and curvature magnitude in inverse millimeters. Native length inversion
respects ordered edge orientation. Undefined tangents and stations exactly at
tangent/curvature discontinuities fail explicitly; no neighboring edge is chosen
by enumeration. These are exact B-rep evaluations subject to numeric tolerances,
not samples of the displayed edge polyline.

Geometric edge selectors add `closed`, circular `radius:{value,tolerance}`,
circle/ellipse `axis:{vector,tolerance}` and
`endpoints:{points:[p0,p1],tolerance}`. Axis sign and endpoint ordering do not
change the match. Axis tolerance is radians; radius, endpoint and existing
length tolerances are millimeters. Descriptors expose closure and endpoints.
Each selector still requires its source feature, curve kind and expected count;
an insufficient rule produces an explicit ambiguity error.

## Sketch hull, trace and full round

`sketch_hull` takes 1–16 distinct sketch/curve `inputs` and a `workplane`.
It constructs the convex hull of up to 256 analytic support primitives from
straight lines and circular arcs. Angular support-function crossings and arc
limits determine exact line and circle segments; no sampled polygon replaces
the curves. Input sketch regions are independently checked for containment.
Inputs must be coplanar. Bezier/B-spline/elliptical hulls fail explicitly,
including rational Beziers that happen to describe circles.

`sketch_trace` takes a curve `input`, `workplane` and positive scalar `width`.
It sweeps a perpendicular line along one connected planar wire with right-corner
transitions, flat open ends and no automatic end fillets. Native approximation tolerances are 1e-8, with at most 128 sweep segments.
The native swept B-rep is checked for validity and self-interference before its
boundaries are represented on the authored plane and adjacent regions are
united before extrusion. A closed circular path yields an annulus. Planarity
is checked with exact native bounds and material area must agree with width
times native path length; folds, overlap and lost material fail. Circular paths
also reject widths reaching their curvature center. This supports
curved paths including rational Beziers, subject to native sweep convergence.

`sketch_full_round` takes a sketch `input` and `edges`, one geometric selector
for a straight outer edge. The selected edge and its two immediate neighbors
must all be straight. Interior supporting lines determine a unique positive
inscribed circle, and contact must occur inside all three finite edges. The
selected end is replaced by its convex tangent arc; neighboring edges are
trimmed exactly and holes are retained. Independent tangent, validity and
material-containment checks reject ambiguous, oversized or concave solutions.
A curved-edge or inverted full-round construction is not currently supported.

All three yield intermediate editable sketches; extrude or thicken to make
material. Their inputs and selectors participate in component capture/remapping,
cache invalidation and atomic worker failure handling.

## Verification and remaining scope

`parity_curves` uses analytic helices in both hands and an arbitrary frame,
independent STEP readers, rational quarter circles, tangent construction,
multiedge trims, selector ambiguity, source-qualified worker sampling, cache
invalidation, durable query jobs and failed-edit rollback. Analytic capsule hulls,
annular traces, tangent caps and portable curve-to-material components cover the
derived sketch operations. `parity_curves_schema_tests.py` validates
real request/results with an independent Draft 2020-12 validator.

Curve sampling is one connected
wire (or one explicitly selected edge); branches, undefined differential
geometry and discontinuity stations are rejected. General automatic geometric
constraint solving is not implied by the tangent constructors.
