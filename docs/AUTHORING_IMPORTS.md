# Editable text, SVG and DXF authoring

Native authoring profiles extend the existing `sketch` feature. Every profile
retains an explicit editable workplane and produces exact closed planar regions
that normal extrusion, revolve, Boolean cuts and subsequent sketch operations
can consume. Disconnected regions remain separate exact faces/solids. Sources
are captured inside the model, including their raw SHA-256 identity; rebuilding
never reads a system font or the original file path. Import does not recover
external design constraints or a source application's feature tree.

## Capture into an existing document

Use `cad_import_sketch` for a local font, SVG or ASCII DXF. It appends one sketch
atomically to an existing document, requires `expected_revision`, and keeps the
existing output feature selected. The full candidate model is built in a bounded
native worker before publication. A source, geometry, revision or budget failure
preserves HEAD. Optional `request_id` supplies ordinary durable mutation replay;
`cad_job` supports both sketch tools. Imports return compact committed identity,
source SHA-256 and the output summary. Use `cad_read` to retrieve the embedded
editable source. Files need not fit the 1 MiB MCP request frame because requests
carry paths; the frame limit is unchanged.

For example, first create a solid stock/document, then import a font label:

```json
{"document_id":"label","expected_revision":1,"format":"text",
 "path":"/absolute/path/font.ttf","feature_id":"letters",
 "workplane":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},
 "text":"BO","height":20,"request_id":"capture_letters"}
```

At the returned committed revision, ordinary `cad_apply` adds
`{"id":"raised_letters","type":"extrude","input":"letters","distance":2}`
and chooses that output, or cuts/fuses it with stock. The original captured
feature is an ordinary editable `sketch`. Replacing its profile's `text`,
`height`, `spacing`, `scale`, or workplane rebuilds dependents. `height`,
`spacing` and `scale` accept normal scalar parameters/expressions; text content
is an editable UTF-8 string. `expected_sha256` optionally verifies the local
artifact at capture time. The original source hash remains exact during edits.
Changing source/font bytes without replacing their hash fails explicitly.

`cad_capture_sketch` is a read-only helper accepting the same path, format,
feature ID and workplane plus literal numeric dimensions. It validates exact
closed geometry in a native worker and returns the complete portable feature,
source hash and contour/segment counts. It creates no document or revision.
For captured sources larger than a transport request, use `cad_import_sketch`
so the embedded bytes need not be submitted again inline.

## Profile contracts

The normal sketch envelope remains:

```json
{"id":"letters","type":"sketch","workplane":{
 "origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},
 "profile":{"type":"text","text":"BO","height":20,
 "font":{"content_base64":"...","sha256":"..."}}}
```

- `text`: required `text`, `height`, and `font`; optional signed `spacing` in mm.
  `font` requires canonical padded `content_base64` and raw-byte `sha256`;
  optional `face_index` is 0..31 (default 0) for font collections. Height is the
  font em size in mm, 0.00001..100000; it is not an assertion about visible cap
  height. FreeType 2.14.3 loads captured scalable TrueType/OpenType Unicode
  outlines without hinting, bitmap fallback or operating-system substitution.
  Line, quadratic and cubic outlines remain exact curves. The layout maps
  code points with font kerning and advances, at most 256 code points/1024 UTF-8
  bytes. It supports a single line without controls. Missing glyphs and fonts
  without usable scalable Unicode outlines fail; whitespace-only text has no
  filled geometry and fails. Complex-script shaping, ligature substitution,
  variable-font axis controls and text-on-path remain separate work.
- `svg`: required raw UTF-8 `content` and `sha256`; optional dimensionless
  positive scalar `scale`, default 1.
- `dxf`: the same content/hash/scale fields for ASCII DXF. Binary DXF is not
  accepted.

SVG/DXF source bytes are bounded to 4 MiB each; fonts to 8 MiB decoded bytes.
Captured asset strings have separate byte budgets and do not consume the
1 MiB document/worker metadata budget. Component snapshots/materialization
preserve captured data and scalar bindings, so consumers rebuild independently
of the source document. Transport requests still retain their existing bounds.
Parsed sources permit at most 128 closed contours, 8192 exact curve segments,
and 32768 poles. SVG further permits 2048 elements/16 levels and 32 transforms
per element; DXF permits 200000 group pairs. Geometry worker time/memory budgets
still apply to parsing, exact containment, union and rebuilding.

## SVG subset and units

Supported filled shapes are `path`, `polygon`, `circle`, `ellipse`, and `rect`
(including elliptical rounded corners), nested under `svg`/`g`. Paths support
absolute and relative `M/L/H/V/C/S/Q/T/A/Z`, repeated parameter groups and the
standard reflection behavior for smooth controls. Every filled subpath must
explicitly close with `Z`. Cubic/quadratic curves and circular/elliptical arcs
retain exact B-reps; arcs under affine transformations use exact rational
quadratic curves. Inherited `nonzero` and `evenodd` fill rules retain holes and
nested islands. Distinct filled SVG elements combine by exact union.

`matrix`, `translate`, `scale`, `rotate` about an optional center, `skewX` and
`skewY` transforms compose in SVG order. Singular transforms fail. Without a
viewport, one source user unit is one mm before `scale`. Explicit viewport
width/height support px, mm, cm, inches and points; px is 25.4/96 mm. A `viewBox`
requires both dimensions and supports `preserveAspectRatio="none"` or the
standard centered `xMidYMid meet` behavior. Without `viewBox`, viewport user
coordinates use the standard px scale. SVG's numeric Y coordinates are retained;
choose the workplane orientation explicitly to orient them in the part.

Metadata `title`/`desc` plain text and element IDs are accepted. Unsupported
geometry, attributes or paint semantics fail rather than being discarded.
This includes scripts, `use`/external references, CSS/style, text/images,
stroked geometry, paint servers, clipping/masks, document types/entities,
nested viewports and open paths. Paint keywords/functions are checked after
trimming whitespace and folding ASCII case; escaped CSS paint tokens, variables
and contextual paints are rejected. Checks never rewrite captured source bytes.
Standard XML 1.0 declarations support UTF-8 encoding and standalone yes/no;
other processing instructions, declarations and external stylesheets fail.
When the root declares a namespace, it must be `http://www.w3.org/2000/svg`.
Omitted namespaces remain accepted. Comments and title/desc text are retained
in the captured source; other non-whitespace XML text is unsupported.
The parser never fetches URLs or executes source content. Convert stroked
artwork/text to supported filled contours in
the source application, or use native font text authoring.

## DXF subset and units

The parser requires ASCII group pairs, one ENTITIES section, balanced sections
and a final EOF group. Supported entities are XY-plane `LINE`, `ARC`, `CIRCLE`,
`ELLIPSE`, ordinary 2D `LWPOLYLINE`/`POLYLINE` with exact bulge arcs, and
nonperiodic clamped control-point `SPLINE` of degree 1..7 with optional positive
rational weights. Spline poles, weights, knots and multiplicities remain exact;
fit-point/periodic spline modes are rejected. Polyline widths/thickness,
nonzero elevations and nonstandard extrusion vectors require explicit outlines
and are rejected. Every entity participates in a closed contour. Joining open
entity chains requires uniquely matching endpoints within 1e-7 mm; ambiguous
joins, dangling paths and zero-area regions fail. Nested closed contours use
even-odd fill semantics.

`$INSUNITS` supports unitless (explicit user units), mm, cm, m, inches and feet;
unitless or omitted units use one source unit per mm before the caller's
`scale`. HEADER/TABLES/BLOCKS/CLASSES/OBJECTS metadata are accepted without
executing or instancing them. Unsupported ENTITIES such as `INSERT`, `TEXT`,
`HATCH`, 3D meshes and surfaces fail, even when another supported entity is
present. Importing a DXF is independent of the existing read-only artifact
review and drawing-export workflows.

## Validation evidence

The native `authoring` CTest uses an original MIT-licensed tiny TrueType fixture,
created reproducibly by `tests/fixtures/make_authoring_font.py`. The generator
is a developer helper; neither end users nor CTest need Python/fontTools to
capture or render text. Tests assert analytic B geometry with two holes,
quadratic O contours, editable content/size, extrusion/cut, exact STEP readback,
curved SVG/DXF volumes, SVG fill semantics/viewport transforms, DXF bulge arcs
and B-splines, source integrity rejection, failed-edit rollback, cold reopening
after source/font deletion, large-source metadata budgeting, pinned component
closure and durable capture/import jobs. Platform/host installation claims
require their independent release-gate evidence.
