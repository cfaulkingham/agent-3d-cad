# Exact parametric surfaces and shells

Surface features extend the native feature graph with intentional open geometry.
An explicit surface output has `solid_count: 0` and `volume_mm3: 0`, including a
closed shell. Only `surface_solid` materializes a closed shell into solid material.
The editable source retains world-space control points, exact rational weights,
knots, parameter references, trim ranges and every shell input ID.

## Feature contracts

| Type | Required fields | Optional fields |
|---|---|---|
| `surface_bezier` | `id`, `type`, `control_points` | `weights` |
| `surface_bspline` | `id`, `type`, `control_points`, `degree_u`, `degree_v`, `knots_u`, `knots_v`, `multiplicities_u`, `multiplicities_v` | `weights` |
| `surface_trim` | `id`, `type`, `input`, `u_range`, `v_range` | Exact rectangle in the source patch's UV domain |
| `surface_shell` | `id`, `type`, `inputs`, `tolerance`, `closed` | Exact manifold sewing of earlier patches/shells |
| `surface_solid` | `id`, `type`, `input` | `reverse`: explicitly reverse an inward shell |

`control_points` is a rectangular grid of 2–26 rows and 2–26 columns. Each entry
is a world-space `[x,y,z]` coordinate in model millimeters. Rows vary in `u` and
columns in `v`; the initial oriented normal is the cross product of the positive
`u` and `v` derivatives. Coordinates support existing bounded parameter expressions.
Bezier degrees are the row and column counts minus one. Its UV domain is `[0,1]`
in both directions.

`weights`, when supplied, is a matching grid of positive dimensionless scalars
at least 1e-8. Omitted weights give a nonrational surface. Rational Bezier and
B-spline surfaces remain native exact surfaces, including rational conics; they
are never replaced by a sampled mesh.

B-spline degrees are integers from 1 to 25. Each direction has 2–128 strictly
increasing dimensionless knots, with a matching array of integer multiplicities.
Interior multiplicities are at most the degree; endpoint multiplicities may be
degree plus one. Multiplicities must sum to the pole count plus degree plus one.
These surfaces are nonperiodic. Their finite parameter domain follows the native
knot/degrees definition, which need not be normalized to `[0,1]`. Clamped endpoint
multiplicities of degree plus one are useful when the domain should end at the
first and last control points.

```json
{"id":"patch","type":"surface_bezier",
 "control_points":[[[0,0,0],[0,20,0]],[[10,0,0],[10,20,0]]]}
```

This bilinear patch has area 200 mm² and a +Z normal. Thickening it by 2 mm creates
an exact 10 × 20 × 2 mm solid. Negative thickness extends along the opposite side.

## Trimming, sewing and solid materialization

`surface_trim` accepts a patch or an earlier UV trim, with two increasing bounds
in each dimensionless range. Each range must span at least 1e-9 and remain inside
the source face's actual parameter bounds. It builds an exact trimmed native face
and retains orientation. UV trim ranges are parameter coordinates rather than
world lengths. Out-of-domain ranges fail with the source domain in error details.
Contour trims with holes, constrained filling, Gordon networks and curved
projection are now specified in [PARITY_SURFACES.md](PARITY_SURFACES.md).

`surface_shell.inputs` contains 1–64 distinct earlier patch or shell IDs, with
at most 256 source faces in total. `tolerance` is an explicit numeric sewing
tolerance from 1e-9 to 1e-3 mm. Sewing must retain every source face, preserve area
within the geometry tolerance and yield one connected manifold shell. Disconnected
inputs, repeated source faces, nonmanifold edges, removed faces and interference
fail. Native sewing matches and merges boundaries within the supplied tolerance.
Free-edge cutting and nonmanifold sewing are disabled; no extra shape fixing,
gap filling, tolerance escalation or mesh substitution is used.

`closed` is a required claim about the result. `true` requires closed manifold
boundaries; `false` requires an open shell. A mismatch fails. A valid closed shell
still remains a shell with zero solid material volume.

`surface_solid` accepts an earlier `surface_shell` or imported single surface
shell that is actually closed.
It materializes one exact solid and applies the existing rigorous positive-volume,
closed-solid validation. No orientation repair is performed. An inward shell
fails unless the caller explicitly sets `reverse: true`; inconsistent local
orientations or invalid material also fail. Once materialized, ordinary solid
features, assemblies, portable components and manufacturing operations apply.

## Thickening, queries and independent outputs

`thicken` consumes an explicit surface patch, trimmed patch or connected open
shell, with optional shared geometric `faces` selectors. Without selectors it
uses every source face as one connected open patch. Positive thickness extends
along the oriented surface normal; negative thickness extends to the other side.
The result must be a valid closed solid. It is never enough for an offset to return
an open shell or intersecting material. Closed shell inputs must first select an
open patch or use `surface_solid`.
Private exact polynomial support restriction now allows the trimmed biquadratic
crown to thicken with either sign and either join. Existing shape/material
validation remains strict; see PARITY_SURFACES.md for the construction and tests.
The example keeps its editable surface as output, ready for explicit thickening.

Surface features support summaries, evaluation-scoped face/edge topology and
provenance, exact measurements, preview triangulation, orthographic drawings,
STEP export and explicitly open STL export. STEP keeps independent exact faces
and shells. Surface STL uses complete bounded tessellation and may be open; it
is a visualization/interchange output, not an assertion of printable material.
3MF print meshes, solid modeling operands, assembly parts, portable component
roots and manufacturing preparation require solid material. An explicit
`surface_solid` or valid `thicken` result meets that prerequisite.

STEP import defaults to solids; explicit `geometry:"surface"` captures surface
STEP exports without promoting them into material. Existing planar
`sketch_face`/`sketch_projection` operations retain their planar-solid-face
contract; arbitrary curved surface-to-sketch projection is not implied.

## Persistence and verification

Control points, weights, knots and trim ranges use the existing parameter graph.
`surface_shell.inputs` participates in feature-cache dependency keys, native
provenance and portable component materialization. An upstream patch edit
invalidates the shell and every dependent solid. Cache restoration revalidates
finite positive-area patches, manifold shells and the authored closure claim;
it never treats a cached open surface as a solid. Failed geometry, preview or
publication preserves the committed revision.

The `surface` CTest verifies analytic planar areas, rational quarter-cylinder
areas and positive/negative thickened volumes; both Bezier and B-spline forms;
interior knot spans; exact UV trimming; manifold open/closed box shells; explicit
orientation reversal and solid materialization; and nonmanifold/closure failures.
It independently reads STEP output and validates B-reps, checks open STL,
triangulation and drawings, and covers snapshots, parameter/cache invalidation,
component dependency remapping and service rollback/reopen.
