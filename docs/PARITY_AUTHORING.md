# Captured DXF and text authoring parity

This increment extends the native captured-sketch path. The model stores the
original DXF bytes and SHA-256, plus explicitly supplied font bytes and hashes.
It never resolves a font filename from a DXF STYLE record against the filesystem.
No Python, system font, sibling repository or external authoring runtime is used.

## Capturing DXF text

`cad_capture_sketch` and `cad_import_sketch` accept optional `fonts` only when
`format` is `dxf`. Each case-insensitive style name maps to an explicit local font
source with **required** `path` and `expected_sha256`; optional `face_index` is an
integer from 0 through 31. There are at most 16 bindings. For example:

```json
{
  "format": "dxf",
  "path": "/absolute/drawing.dxf",
  "feature_id": "artwork",
  "workplane": {
    "origin": [0, 0, 0],
    "normal": [0, 0, 1],
    "x_direction": [1, 0, 0]
  },
  "fonts": {
    "STANDARD": {
      "path": "/absolute/chosen-font.ttf",
      "expected_sha256": "<64 lowercase hexadecimal digits>"
    }
  }
}
```

The captured profile embeds each binding as `content_base64`, `sha256`, and
optional `face_index`. A text entity whose style has no binding fails. Case-folded
duplicate bindings and conflicting source hashes fail. Raw DXF and font bytes
remain unchanged through parameter edits, components, snapshots and job replay.
Topology provenance includes `font_sha256_by_style` for DXF and `font_sha256` for
ordinary captured text. Each font retains the existing 8 MiB decoded limit;
source DXF retains 4 MiB and the total model retains its existing payload limit.

## Blocks and inserts

BLOCK definitions may contain all supported authoring entities, including other
INSERTs, text and hatches. INSERT applies block-base subtraction, separate X/Y
scale, array offset, rotation and insertion translation, then its parent
transform. Negative scales preserve reflected geometry; singular transforms fail.
Array spacing is rotated but is independent of scale. Drawing `$INSUNITS` and
profile `scale` apply once to the completed drawing.

Definitions are bounded to 128, nesting to 16 levels, array rows/columns to 64
each and expanded entities to 4096. The existing final contour, edge, pole and
coordinate limits apply to expanded geometry. Cyclic, missing, duplicate-name,
external, attributed, empty or nonplanar blocks fail explicitly. Imported spline
poles now receive the complete affine transform, including both coordinates.

## TEXT and MTEXT

Text uses exact unhinted scalable FreeType outlines from the captured face.
DXF height uses the font's OS/2 capital-height metric, or the exact H (then A)
outline height when that metric is absent; a font with neither fails. Ordinary
native `text` profiles retain their established em-height semantics.

TEXT supports baseline/left, center/right, bottom/middle/top alignment, rotation,
width scale, oblique angle and X/Y generation reflection. Nonbaseline alignment
requires its explicit second alignment point. STYLE width, oblique and generation
defaults apply; vertical, shape and bigfont styles fail. Aligned/fit TEXT modes
remain unsupported.

Plain MTEXT supports ordered chunks, explicit paragraph breaks, left-to-right
flow, nine attachment positions, bounded word wrapping to the reference width,
and exact/at-least line spacing. Rotation or an explicit X direction places the
result. Layout uses font advances and exact outline extents; default line spacing
is five thirds of nominal height. A word exceeding its reference width fails.
Formatting groups, inline font/colour/stacking controls, columns, backgrounds,
vertical flow and transformed styles fail explicitly. Literal UTF-8, DXF
`\U+hhhh` escapes and escaped braces/backslashes are supported; MTEXT also
supports `\P` and `\~`. Surrogate escape pairs and percent controls require
literal Unicode instead. Text and geometry budgets bound every expansion.

## HATCH regions

Solid HATCH with normal island detection produces exact filled boundary regions,
including nested holes, rather than its display pattern. Closed polyline paths
support bulges. Edge paths support lines, circular/elliptical arcs and clamped
nonperiodic degree-1-through-7 B-splines with positive rational weights. Supplied
boundary ordering, counts, closure, spline knots and geometry are validated.
Fit-point/tangent spline representations, periodic splines, patterns, gradients,
nonzero elevation and unsupported island styles fail explicitly. Recorded source
handles and seeds do not replace the authoritative captured boundary geometry.

The implementation follows the primary [INSERT](https://help.autodesk.com/cloudhelp/2021/ENU/AutoCAD-DXF/files/GUID-28FA4CFB-9D5E-4880-9F11-36C97578252F.htm),
[TEXT](https://help.autodesk.com/cloudhelp/2024/ENU/AutoCAD-DXF/files/GUID-62E5383D-8A14-47B4-BFC4-35824CAE8363.htm),
[MTEXT](https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-DXF/files/GUID-5E5DB93B-F8D3-4433-ADF7-E92E250D2BAB.htm)
and [HATCH boundary](https://help.autodesk.com/cloudhelp/2016/ENU/AutoCAD-DXF/files/GUID-DC5215D6-E73F-4DFF-8BE9-01CA9610FAEE.htm)
group-code definitions. These are parsing references; native tests independently
check analytic geometry and raw-source portability.

## Editable text on a path

A `text_on_path` feature takes `input`, the ID of an earlier direct `sketch`
whose profile is captured `text`, and `path`, the ID of an earlier exact curve:

```json
{
  "id": "curved_label",
  "type": "text_on_path",
  "input": "captured_label",
  "path": "baseline_curve",
  "start": 5,
  "offset": 2,
  "reverse": false
}
```

The curve must be a single connected wire lying entirely in the text sketch's
plane. `start` is a nonnegative arc length from the authored path beginning;
`reverse` measures from its end and reverses the reading direction. Signed
`offset` moves along the sketch normal crossed with the reading tangent. Both
lengths accept editable scalar parameters and default to zero.

Each glyph is moved rigidly so its advance center lies at the corresponding
arc-length station with its baseline aligned to the exact path tangent. Font
kerning and inter-glyph spacing remain part of the layout; whitespace advances
are preserved. A final extra spacing interval is not included in text length.
Outlines are exact native curves, not warped or tessellated. The resulting
planar regions can be extruded or used by ordinary sketch operations.

The full text advance must fit on the path, including a closed path; wrapping
across its seam is not implicit. An undefined tangent or discontinuity at a
placement station fails explicitly. Already merged source glyphs cannot be
assigned independent placements and fail as ambiguous. Overlapping placed
regions fail rather than being silently fused. Ink overhang is controlled by
font outlines; the fit criterion is typographic advance, not an ink bounding box.

Native face history records the actual input faces, and provenance includes both
text and path dependencies. Parameter or feature edits invalidate downstream
layout caches. Captured components include and remap both dependencies and all
font bytes, so rebuilding does not require the source document or font file.

## Validation

`parity_authoring` contains analytic block-transform, nested-array, spline,
hatch-hole, cap-height, alignment, mirrored/rotated text and multiline oracles,
independent STEP readback, captured-hash failures, durable job replay, source
file deletion, parameter edits, failed-edit rollback and portable components.
`tests/parity_authoring_schema_tests.py` validates actual tool requests/results
with an independent Draft 2020-12 validator.

On 2026-10-10, macOS arm64 / pinned OCCT 8.0.1 / Release build, with
shared mass-integration prerequisites `450656f` and `6d82d1f`:

- `parity_authoring`: **158 checks**, 3.27 s, including analytic straight/curved,
  reversed and rotated-plane placement, exact quadratic glyphs, closed paths,
  spacing and end-fit, native face history, independent STEP readback, snapshots,
  edits, selective cache invalidation, durable jobs, rollback and portable
  components after source deletion.
- Existing `authoring`: **142 checks**, 4.46 s; `parity_solids`: **359 checks**,
  13.58 s; `parity_curves`: **263 checks**, 0.55 s; `jobs`: **370 checks**,
  19.99 s; `dependency_cache`: **87,050 checks**, 5.30 s; `component`:
  **56 checks**, 3.44 s. These seven suites passed in an eight-suite run
  taking **51.17 s**.
- Independent actual request/result schema validation: **94 checks / 31 tools**.
- The unchanged `app_protocol` gate failed: catalog **546,848 bytes** exceeds
  the **476,160-byte** budget. Compact mutation receipts are separate integration
  work; full-source assertions use `cad_read` at the returned revision.
- Native build and `git diff --check` passed. No platform/package/host/release
  readiness beyond this local evidence is claimed.

The strict spaced-glyph centroid regression exposed mass-only adaptive
convergence under-integrating moments for planar polygons. The shared kernel
fix uses fixed-degree Gauss integration for plane faces bounded only by lines,
while retaining rational and curved-face quadrature. Independent STEP probes
also verified translated/off-axis glyphs and a mixed affine-cylinder compound;
the authoring tests retain their original analytic tolerances.
