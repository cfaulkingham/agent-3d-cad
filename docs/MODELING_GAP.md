# Modeling comparison and implemented phases

The local comparison target is `../build123d/` (the requested `../build123/`
directory is absent). This work implements the prioritized modeling gaps as
native editable features. It does not accept Python programs or depend on the
sibling source/runtime. All geometry remains behind `BuiltModel` and uses the
pinned OCCT 8.0.1 kernel.

| Area | Implemented | Current boundary |
|---|---|---|
| Shell/offset/thicken | Signed exact operations, expected-count geometric face rules, connected open patches | Geometry must pass containment, validity and interference checks; unsupported offsets fail |
| Planar sketches | Exact material Booleans, offsets, corner fillet/chamfer, transform/instance/mirror, planar solid-face reuse/projection | No general sketch constraint solver, open-edge projection or curved-solid silhouette reconstruction |
| Solid operations | Mirror, oriented halfspace split, material intersection | Split must create positive material on both sides; empty/contact-only results fail |
| Text/SVG/DXF | Captured fonts/source bytes with hashes, exact outlines and portable editable sketches | No complex-script shaping/text-on-path, SVG strokes/CSS/external references, or unsupported DXF entities |
| Extrusions/sweeps | Signed/directed/bidirectional/tapered extents; exact first/last target termination; varying sections, holes, frames/guides/transitions | Complete profile coverage and explicit ordered stations; bounded native operation failures remain errors |
| Sheet metal | Exact signed cylindrical bends/direct base-edge flanges; K-factor developed blanks and bend-line reports | No chained bends, hems, jogs, generated relief, cuts across bends or arbitrary-solid unfolding; tested Bezier bases hit an explicit OCCT verification abort |
| Freeform surfaces | Rational Bezier/B-spline patches, rectangular UV trims, manifold sewing, explicit closed-shell solid creation and thickening | No arbitrary UV contour/hole trimming, curved sketch projection, periodic patches or general surface blend/filling tools |

The sibling also exposes a much larger construction/query convenience API:
specialized arcs, helices, slots, polygons, airfoils, constrained line/arc helpers,
advanced surface construction and other Python object operations. Some shapes
can already be expressed with exact native curve segments and compositions;
their dedicated construction interfaces remain absent. Numeric parametric
profiles should not be presented as a general constraint solver.
Sketch hull/trace/full-round helpers and general curve-to-surface projection
also remain absent. Lofts still require one closed outer boundary per section;
the new multi-region sketch and extrusion support does not extend lofts to holes
or disconnected sections.

Native CAD additionally preserves structured feature IDs, immutable committed
revisions, source assets, worker isolation and qualified live selections. Those
requirements determine its API rather than mirroring Python builder semantics.
Surface outputs intentionally contain no solid material. Manufacturing roots
must be explicit valid solids. A K-factor is caller intent, not a forming
simulation or fabrication certification.

Detailed contracts: [shell/offset/thicken](SHELL_OFFSET_THICKEN.md),
[sketch/solid operations](SKETCH_OPERATIONS.md), [authoring](AUTHORING_IMPORTS.md),
[extrusion/sweep controls](RICHER_MODELING.md), [sheet metal](SHEET_METAL.md),
and [surfaces](SURFACES.md). Portable examples are
`examples/sheet-bracket.create.json` and `examples/freeform-panel.create.json`;
the bracket retains both formed and flat features, independently queryable and
exportable by feature ID. Executed acceptance evidence and release limits are
recorded in [HANDOFF.md](HANDOFF.md).
