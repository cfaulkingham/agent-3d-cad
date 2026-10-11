# Modeling comparison and implemented phases

The local comparison target is `../build123d/` (the requested `../build123/`
directory is absent). The latest gap pass compares native baseline `f90033f`
with sibling `d3b12235`. It adds native editable features without accepting
Python programs or depending on sibling source/runtime. Geometry remains behind
`BuiltModel` and uses pinned OCCT 8.0.1.

| Area | Implemented | Current boundary |
|---|---|---|
| Shell/offset/thicken | Signed exact operations, geometric face rules, connected patches; both signs/joins of the crown thickening case | Full validity/interference remain required; general offset convergence is not guaranteed |
| Planar sketches | Exact Booleans, offsets, fillet/chamfer, transforms, instances, mirror, face reuse; analytic hull/trace/full-round | Hulls require lines/circular arcs; full-round requires three straight neighboring edges; no general constraint solver |
| Solid operations | Mirror/split/intersection, uniform/affine scale, existing-face draft, asymmetric/angle chamfer, twist extrusion | Draft supports planar/cylindrical/conical faces; geometric selection and exact solid validity are mandatory |
| Loft/extrusion/sweep | Explicit signed/directed/tapered/target extents, varying profiles/holes/frames; holed lofts and endpoint vertices | Multiple loft holes require explicit correspondence; disconnected loft regions and interior vertex sections are unsupported |
| Text/SVG/DXF | Portable captured fonts/bytes, DXF blocks/plain text/solid hatches, editable planar text-on-path | No complex-script shaping, nonplanar glyph placement, rich MTEXT, patterned hatches, SVG strokes/CSS/external references or arbitrary source semantics |
| Sheet metal | Named chained flanges, blank folds, open hems, jogs, explicit miters, rectangular relief and bend-crossing cuts; exact developed output | No crushed zero-radius hems, curved/freeform bend cuts, inferred corner solving or arbitrary imported-solid unfolding |
| Freeform surfaces | Rational patches, exact UV contours/holes, sewing/solid creation, checked C0/G1/G2 fills, polynomial Gordon networks, directional curved projection; surface STEP import/export | No periodic/rational Gordon networks or automatic reparameterization; folds/seams/tangent or ambiguous projection can fail explicitly |
| Curves/queries | Helices, rational Beziers, tangent line/arc constructors, extraction, arc-length trimming/sampling and richer geometric selectors | One connected wire or uniquely selected edge; undefined tangents/discontinuities fail; specialized convenience constructors remain fewer |
| Parametric expressions | Dimension-checked trig/root/conditional/comparison/extrema/power/log/rounding operations and parameter-driven pattern counts | Bounded declarative expressions; no execution of caller code or Python builder semantics |
| Mesh reconstruction | Native plane/cylinder/sphere recognition, conservative triangle residuals, leftovers and caller-guided editable exact proposals | No recovered history, arbitrary trim inference or complete noisy/freeform analytic decomposition |
| Export delivery | Source-qualified bounded STEP/STL/3MF/drawing resources and advertised host download request | Actual target-host save support and complete installed-host journeys remain unverified |

The sibling exposes a larger Python construction/query convenience API,
including specialized slots, polygons, airfoils and object operations. Native
curve segments and compositions can express many such shapes, but no full
build123d API parity is claimed. Numeric profiles and tangent constructors do
not establish a general sketch constraint solver. Surface solver checks combine
native construction with independent finite samples, rather than a global proof
of continuity or projection visibility.

Native CAD preserves structured feature IDs, immutable committed revisions,
portable source assets, worker isolation and qualified live selections. Surface
and curve outputs intentionally contain no solid material. Manufacturing and
assembly roots require explicit valid solids. A caller-supplied K-factor is
forming intent, not simulation or fabrication certification. Failed geometry
must leave the previous revision unchanged.

Detailed current contracts: [solid operations](PARITY_SOLIDS.md),
[surfaces](PARITY_SURFACES.md), [sheet metal](PARITY_SHEET_METAL.md),
[curves/sketches](PARITY_CURVES.md), [captured authoring](PARITY_AUTHORING.md),
[expressions](PARAMETRIC_EXPRESSIONS.md), [mesh reconstruction](MESH_RECONSTRUCTION.md)
and [export resources](EXPORT_DOWNLOADS.md). Earlier foundation contracts remain
in [shell/offset/thicken](SHELL_OFFSET_THICKEN.md),
[sketch/solid operations](SKETCH_OPERATIONS.md), [authoring](AUTHORING_IMPORTS.md),
[extrusion/sweeps](RICHER_MODELING.md), [sheet metal](SHEET_METAL.md) and
[surfaces](SURFACES.md).

[HANDOFF.md](HANDOFF.md) records executed evidence and unfinished
platform/host/signing/distribution gates. Implemented bounded operations and
native tests do not establish release readiness.
