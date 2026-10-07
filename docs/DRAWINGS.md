# Native engineering drawings

`cad_drawing` derives vector drawings from a named, committed model revision.
It does not edit the model or advance HEAD. CLI and MCP use the same service;
projection and rendering run in a bounded native geometry worker. The installed
runtime needs no Python, Node, browser print engine, or PDF conversion program.

Repeated drawings reuse exact geometry and complete projected view sets from a
bounded, disposable workspace cache. Changing sheet layout, dimensions, explicit
tolerances, notes, view names or formats rerenders without repeating projection.
Model/build changes invalidate geometry and projections; changing the ordered
views, hidden lines or section/hatch settings invalidates the view-set projection.
Per-view exploded part IDs and resolved translations also participate in the
projection key, so assembled and exploded views never reuse one another's
geometry.
The first exact hidden-line drawing of a complex threaded part can still exceed
the normal job budget. The pinned dependency recipe includes an OCCT midpoint
classification optimization for these cold projections; see
[DEPENDENCIES.md](DEPENDENCIES.md) for its scope and [HANDOFF.md](HANDOFF.md) for
measured results. Cache contents are never editable source or stable
selection identities; deleting them only causes regeneration.

```json
{
  "document_id": "plate",
  "revision": 2,
  "drawing": {
    "title": "Mounting plate",
    "dimensions": [
      {"view": "top", "kind": "width"},
      {"view": "front", "kind": "height"},
      {"view": "top", "kind": "diameter", "center": [10, 10],
       "radius": {"parameter": "hole_radius"}}
    ]
  }
}
```

Omitting `drawing` selects an A4 landscape sheet with top, front, right and
isometric views in third-angle arrangement, automatic common scale, hidden
lines, and all export formats.
Dimensions are explicitly requested. `examples/plate.drawing.json` and
`examples/duplex-35-sprocket.drawing.json` are complete requests.

## Views and coordinates

Views have unique `id` values and `orientation` values `front`, `top`, `right`,
`isometric` or `section`. Supply one to six views. Directions below describe
the camera position relative to the part, not a world transformation:

| View | Camera direction | Drawing horizontal | Drawing vertical |
|---|---|---|---|
| front | -Y | +X | +Z |
| top | +Z | +X | +Y |
| right | +X | +Y | +Z |
| isometric | +X, -Y, +Z | Projected horizontal | Projected vertical |
| section, axis x | plane X = offset | +Y | +Z |
| section, axis y | plane Y = offset | +X | +Z |
| section, axis z | plane Z = offset | +X | +Y |

`layout` selects `third_angle`, `first_angle`, or `grid`:

- Third angle: top above front, right to the right of front, isometric above right.
- First angle: top below front, right to the left of front, isometric below right.
- Grid: labeled cells in recipe order, without implying a projection convention.

Standard arrangements align front/top world X coordinates and front/right world
Z coordinates at one common scale, including when dimension lanes differ. They
require a front view, accept at most one of each orthographic/isometric
orientation, and reserve the three orthographic cells even when a view is omitted.
Up to three auxiliary views (isometric plus sections) occupy the remaining cell
and an optional third column. The layout never silently falls back to a grid.
The title block identifies the convention. Drawing coordinates are millimeters
before sheet scale is applied; negative coordinates are valid.

For compatibility, a recipe with explicit `views` and no `layout` retains the
original grid arrangement. A recipe without `views` defaults to third angle.
Set `layout` explicitly in new reusable recipes.

A section view also requires `section: {"axis":"z","offset":3}`. This is the
actual planar cross-section profile, not a cutaway view of everything behind
the plane. A plane that has no section curves fails explicitly. The offset can
use a model parameter or the existing bounded scalar expression syntax.

Sections are hatched by default. The kernel intersects the solid with the plane
to obtain material faces, including their inner boundaries. Thin 45-degree lines
are clipped to those regions: holes and keyways stay empty, disconnected material
is included, and overlapping material intervals are united. Spacing is 2.5 mm on
the sheet (therefore 2.5/scale mm in a view's 1:1 DXF). Hatching uses the same
analytic lines/circles/arcs and bounded polyline approximation as the outlines.
It is annotation and cannot become a dimension reference. Set `hatch: false`
on a section view to export only its outline. A tangent profile with curves but
no material area fails with `invalid_drawing` when hatching is requested.

```json
{"layout":"third_angle", "views":[
  {"id":"front","orientation":"front"},
  {"id":"top","orientation":"top"},
  {"id":"right","orientation":"right"},
  {"id":"cut","orientation":"section",
   "section":{"axis":"z","offset":3},"hatch":true}
]}
```

OpenCascade's exact B-rep hidden-line algorithm produces visible and hidden
edges. Lines, circles and circular arcs remain analytic vector entities. Other
curves are approximated by polylines at a 0.02 mm model-space deflection; this
is a drawing tolerance, not a manufacturing tolerance. `hidden_lines: false`
omits obscured edges. Hidden geometry is emitted before visible geometry in
PDF/SVG and DXF so coincident hidden dashes do not paint over visible outlines.
Center marks, dimensions and balloons follow the geometry. The 3D source and
STEP exports remain exact solids.

## Exploded assembly views

An assembly output can add `explode` to any drawing view. It contains one to 64
unique assembly part IDs, each with a three-component translation in world mm:

```json
{"layout":"grid", "views":[
  {"id":"assembled","orientation":"front"},
  {"id":"exploded","orientation":"front",
   "explode":[{"part_id":"spacer","translation":[0,0,{"parameter":"explode_gap"}]}]}
]}
```

Offsets apply after each part's solved placement and rigid mates. They affect
only that view; unlisted parts retain their assembled positions. Values support
the same parameters and bounded mm expressions as model coordinates. Empty
lists, duplicate IDs, unknown parts and an output that is not an assembly fail
explicitly. A section with `explode` intersects the translated assembly with
the requested world plane and hatches the resulting material.

PDF/SVG view labels and individual DXFs visibly say `EXPLODED`. Dimensions in
these views measure the **exploded geometry**, including artificial spacing;
they do not report the assembled distances. Use a normal view for assembled
dimensions. A grid can show assembled and exploded versions of the same
orientation side by side. Standard layouts still permit only one view of each
orthographic/isometric orientation.

The source model, mate definitions, committed placements and STEP/STL exports
are unchanged by exploded drawing generation. Saved drawing recipes retain
parameter references, so regeneration after an edit re-resolves both placements
and explode offsets. `examples/assembly.create.json` and
`examples/assembly.drawing.json` demonstrate editable plates and spacers, rigid
mates, and normal and exploded views in one sheet.
BOM tables and part balloons are described below; exploded paths and automatic
separation are outside this increment.

## Assembly BOM tables and part balloons

Set `bom: true` on a drawing recipe to include the output assembly's bill of
materials. Rows group instances by source feature and use saved assembly metadata
and deterministic item numbering; quantities count instances. The same item
number applies to every instance of that source. Neither item numbers nor
quantities are arbitrary drawing annotations. See [ASSEMBLIES.md](ASSEMBLIES.md)
for metadata edits, numbering rules and the standalone `cad_bom` export.

Optional `balloons` connect item numbers to visible part surfaces:

```json
{"bom":true,"views":[{"id":"front","orientation":"front"}],
 "balloons":[{"view":"front","part_id":"base",
              "anchor":[10,0,2],"label":[-10,2]}]}
```

Each balloon requires an existing view and part ID. `anchor` is a three-element
point in that part's **source coordinates**, before placement, mates or explosion.
It must lie on the exact solid's boundary and be visible from that view. `label`
is a two-element balloon center in the view's projected model-mm coordinates,
before sheet scale. Both support mm parameters and bounded expressions. Saved
recipes preserve those references; regeneration recomputes the surface position,
solved placement, exploded offset and item number from the requested revision.

At most 64 balloons are accepted, with at most one per part in each view. They
require `bom: true`, an assembly output, and a nonsection view. Off-surface,
occluded, coincident or ambiguous part anchors fail explicitly. A projected
coincidence is not permission to attach to another part. Balloon circles have a
7 mm sheet diameter. Their centers must clear the view's geometry bounding
rectangle by their radius plus 1 mm, and circles must be separated by 1 mm.
Leaders must reach beyond the circle by at least 2 mm and cannot cross another
balloon. Balloon circles must also clear dimension annotations, including angular
arcs, extension/leader lines, arrowheads and text. Circles and leaders participate in sheet fitting; overlapping labels or
a layout that cannot fit fail rather than silently relocating or omitting
annotations. Put labels in clear space around the view. Balloons identify parts
and do not measure lengths.

Attachment checks use a 0.00001 mm boundary tolerance. Missing/off-boundary
anchors return `selection_missing`, an occluded anchor returns `invalid_drawing`,
and an anchor shared with another part's boundary returns `selection_ambiguous`.
Errors identify the output feature, view and part for repair.

PDF/SVG sheets include the BOM table. `bom.json` and `bom.csv` are independent
table sidecars regardless of the selected geometry formats. JSON includes the
source document/revision identity; CSV quotes every field, uses CRLF records and
joins each row's part IDs with semicolons. Like standalone `cad_bom` CSV, text
cells beginning with `=`, `+`, `-`, `@`, tab or carriage return get a leading
`'` against formula injection (see [ASSEMBLIES.md](ASSEMBLIES.md)). DXFs retain the part balloons and leaders
on a `BALLOONS` layer at 1:1 mm. The response includes `bom` and resolved
`balloons` with `view`, `part_id`, `item_number`, `anchor_mm` and `label_mm`.
The anchor and label coordinates in the response are projected 2D mm. A saved
recipe records both the original input and resolved evidence. Balloon label-only
changes rerender from cached projections; changing source anchors invalidates
the view projection cache because geometric attachment and visibility must be
checked again. BOM metadata edits are model changes and retain the complete-model
cache invalidation rule.

`examples/assembly-bom.drawing.json` adds a BOM and balloons to the existing
`examples/assembly.create.json` design. It leaves the older assembly drawing
recipe unchanged.

## Dimensions and notes

Each dimension names a `view`. Up to 32 dimensions are accepted:

- `width` and `height` measure the projected view's extents.
- `diameter` and `radius` require `center: [x,y]` and `radius`. These are
  matching rules for an actual projected circle or circular arc, not replacement measurements.
  Identical projected circles collapse to one geometric reference. Missing or
  ambiguous matches fail; the measured geometry supplies the printed value.
- `horizontal` and `vertical` require `from: [x,y]` and `to: [x,y]`. Anchors
  must resolve uniquely within 0.02 mm to a line segment, circular curve, full
  circle center, or a curve endpoint. The measured coordinate
  difference supplies the value; arbitrary dimension text is not accepted.
- `angular` requires two directed `lines`, each with `from: [x,y]` and
  `to: [x,y]`. Both points must match the same projected straight line within
  0.02 mm and be more than 0.04 mm apart. The resolved line directions supply
  the angle; the ordered `from`/`to` points select its rays. Coincident collinear
  fragments are one reference, but distinct nearby lines fail as ambiguous.
  The line intersection supplies the vertex, including virtual intersections
  beyond trimmed edges. Curves/polyline segments, parallel lines and collinear
  pairs cannot define an angular dimension. `sweep` defaults to `minor` (0–180
  degrees); `major` requests the reflex angle (180–360 degrees). Neither accepts
  an arbitrary nominal angle. These are angles in the selected 2D projection,
  not measurements of a 3D angle from an isometric image.

An angular dimension draws a circular arc with tangential arrows, extension
lines and a labeled leader. Optional `arc_radius` controls that annotation's
radius in model millimeters; it is not a measured part radius. By default it is
35% of the shorter matched segment. Angular arcs and virtual vertices participate
in layout fitting without changing width/height measurements. Unreadably small
arcs fail explicitly: increase `arc_radius` or the sheet scale. PDF/SVG preserve
the vector arc; a DXF stores an analytic ARC on `DIMENSIONS`, at 1:1 mm.

```json
{"view":"top", "kind":"angular", "arc_radius":12,
 "lines":[{"from":[40,0],"to":[0,0]},
          {"from":[40,0],"to":[0,40]}],
 "manufacturing_tolerance":{"type":"symmetric","value":0.25}}
```

For a matching right-triangle profile, this measures and prints `45 +/-0.25 deg`.

Coordinates and radii accept numbers, model parameter references, and bounded
scalar expressions. Circle `tolerance` controls matching only. No matching
tolerance is printed as a fabrication tolerance. View references are geometric
rules; no transient face or edge index becomes a stable design reference.
Untoleranced dimension labels round to three decimal places, omitting trailing
zeros. The response retains unrounded measured values as `value_mm` for lengths
and `value_deg` for angles. Every dimension also returns its printed `label`;
angular results include `vertex_mm` and `arc_radius_mm`. Display precision is
not a fabrication tolerance.

## Explicit manufacturing tolerances

Any requested dimension may specify `manufacturing_tolerance`:

| Type | Fields | Example printed label |
|---|---|---|
| `symmetric` | positive `value` | `40 +/-0.1` |
| `deviation` | signed `lower`, `upper`, with lower < upper | `40 +0.01/-0.02` |
| `limits` | absolute nonnegative `lower`, `upper`, with lower < upper | `39.9..40.2 LIMITS` |

Lengths use mm and angular tolerances use degrees; angular labels append `deg`.
Unilateral deviations are supported, including zero bounds and same-sign signed
deviations. Limits must contain the displayed measured nominal. Acceptance
bounds cannot be negative or exceed 360 degrees for angles. Circle `tolerance`
continues to control reference matching only; it never assigns fabrication limits.

Optional `general_tolerances: {"linear":0.1,"angular":0.5}` supplies symmetric
defaults for dimensions without an individual override. Either member can be
omitted. Defaults are printed in the sheet's notes and in each standalone DXF;
an individual tolerance is printed beside its dimension and takes precedence.
No allowance is inferred if neither is supplied. These general values apply to
the requested dimensions in the drawing, not undocumented model features.

Tolerance values and `arc_radius` accept literals, parameters and bounded scalar
expressions. Tolerance expressions must use the dimension's `mm` or `deg`
context. General `linear` and `angular` values use those respective contexts.
Explicit tolerances support up to six decimal places; finer values fail instead
of silently changing the requested allowance. Toleranced nominal labels use six
decimal places with trailing zeros omitted. Symmetric/deviation limits are based
on this displayed nominal. The response retains both the unrounded measurement
and `display_value_mm` or `display_value_deg`, effective `manufacturing_tolerance`,
`tolerance_source` (`dimension` or `general`), and `lower_limit_mm`/`upper_limit_mm`
or `lower_limit_deg`/`upper_limit_deg`. A sheet scale never changes these values.
Geometric projection precision remains the independent 0.02 mm contract above.

`examples/angular-plate.create.json` and `examples/angular-plate.drawing.json`
demonstrate angular measurement, all three tolerance styles and general defaults.
The allowances are explicit demonstration values, not inferred production fits.

`title`, `material`, and `notes` supply explicit annotations. They use bounded
printable ASCII text for consistent native PDF/font and DXF interoperability.
Do not infer material, fit class, tolerances, finish, or certification from the
model. Threads remain projected geometry; thread conventions and GD&T are not
automatically authored.

## Sheets and exports

`sheet` is `A4` or `A3`, both landscape. Omit `scale` to fit the views and their
dimension lanes; an explicit numeric scale must fit. The title block records
document ID, model revision, kernel version, units and scale. Requested formats
are a unique nonempty subset of `svg`, `pdf`, and `dxf`.

- SVG and PDF contain the drawing sheet, annotations, center marks and dimensions.
- Each view gets a separate DXF in **1:1 model millimeters**, independent of
  sheet scale. Layers `VISIBLE`, `HIDDEN`, `CENTER`, `DIMENSIONS` and `HATCH`
  separate geometry and annotations. Hatching is clipped LINE entities on its
  own layer, not a CAD editor's associative HATCH object. These are view
  projections, not automatically approved laser-cut or manufacturing profiles.

When `bom: true`, the complete table must fit the selected sheet with its views;
long metadata wraps and an oversized table fails explicitly. Rows are never
truncated or silently split across pages. JSON/CSV sidecars are included in
addition to the requested `svg`/`pdf`/`dxf` geometry formats, for at most ten
artifacts (six DXFs, two sheets and two BOM tables).

The response contains artifact paths and byte counts, measured dimensions,
sheet dimensions, scale, `layout`, `view_layouts`, a recipe path, and a manifest
path. Each placement contains `view`, `cell_mm: [x,y,width,height]`, and
`origin_mm: [x,y]` in sheet coordinates measured right/down from its top left.
A projected model point `[u,v]` maps to `[origin.x+scale*u, origin.y-scale*v]`.
Each generation
uses a fresh directory under `exports/`; the manifest is written last, after
all files and the recipe succeed and cancellation is rechecked. Previous
drawings remain intact. Abrupt termination may leave an unpublished directory;
a directory without its final manifest is not a completed export.

Projection limits apply across all views: 10,000 entities, 200,000 coordinate
points, 40,000 extracted edges, and finite projected coordinates within +/-1e9 mm.
Section material boundaries count toward these limits as well as their outlines.
Hatching is additionally bounded to 2,001 scan lines per view, two million
boundary-intersection checks and 20,000 output segments per drawing.
The output feature also retains the existing 10,000-face/edge topology limit.
The combined generated artifact content is bounded to 32 MiB. Inputs remain
subject to the model's scalar and protocol limits. Exceeding a limit fails
explicitly; no curves or requested dimensions are silently truncated.

## Regeneration and asynchronous use

`drawing.json` preserves the document identity, exact source revision, original
parameterized `drawing` request, resolved values and a source-model SHA-256.
To regenerate after an edit, call `cad_drawing` with the same `document_id` and
saved `drawing` object, changing `revision` to the committed revision desired.
This re-resolves parameters and geometry references. It does not silently
overwrite an older drawing or automatically follow HEAD.

Use `cad_job` for complex drawings, particularly curved or threaded parts:

```json
{"action":"submit","request_id":"plate_drawing_r2","tool":"cad_drawing",
 "arguments":{"document_id":"plate","revision":2},
 "budget":{"timeout_ms":120000,"memory_mb":2048}}
```

Poll for terminal success before reporting files. Existing queue, memory,
timeout and cancellation rules apply. Invalid recipes, unresolved dimensions,
empty sections, unfittable layouts and geometry failures do not modify HEAD.

This is a native drawing preview, not an ISO/ASME drawing certification or a
general drafting editor. Multi-sheet layouts, arbitrary camera views, GD&T,
automatic tolerance assignment and automatic revision-following are outside
this increment. Platform evidence and exact checks are recorded in HANDOFF.md.
