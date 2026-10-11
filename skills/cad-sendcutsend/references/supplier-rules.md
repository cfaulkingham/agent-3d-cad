# Current supplier sources and native coverage

Checked 2026-10-11; re-fetch the relevant pages for a live preflight:

- [SendCutSend sheet-cutting guide](https://sendcutsend.com/guidelines/sheet-cutting/)
  is the current consolidated 2D/3D guide. It prefers DXF for flat cuts and STEP
  for bends/forms. Files use actual-size mm/inch geometry. Cut files must omit
  annotations; 2D paths need closed, non-overlapping contours on one cut layer.
  STEP requires one uniform sheet solid, a face aligned to a plane, matching
  stock and bend specifications, and no modeled partial-depth hole treatments.
  Do not assume an arbitrary extruded solid satisfies their sheet-body rule.
- [Materials](https://sendcutsend.com/materials/) identifies available stock and
  stock-specific process/limits. Follow the selected material's technical table;
  preserve the exact row in the report. The sheet guide's generic hole rules
  are a starting point only.
- [Bending guidelines](https://sendcutsend.com/guidelines/bending/) and
  [bend calculator](https://sendcutsend.com/bending-calculator/) supply the chosen
  material/thickness bend setup. Recheck bend radius, K-factor, flange length,
  relief and feature-to-bend spacing there.
- [Common file problems](https://sendcutsend.com/faq/what-are-common-problems-to-look-for-in-my-files/)
  highlights duplicate paths and small geometry. A visually correct preview is
  not proof of clean cutting contours.

For each criterion preserve the required value, measured value/method and
coverage. Exact thickness/envelope and sampled wall evidence are different
checks. A sampled wall result does **not** establish the narrowest planar bridge;
a cylinder radius does **not** establish that every cutout meets a minimum.
Measure named features or mark global coverage unknown. Native STEP solid count
is useful; it does not establish the supplier parser's recognition of a sheet
body. A failed call is not a supplier rejection.

Native drawings keep separate `VISIBLE`, `HIDDEN`, `CENTER`, `DIMENSIONS`,
`HATCH`, `BALLOONS` and `HOLE_CALLOUTS` layers. Turning off hidden lines and
omitting dimensions does not remove all other possible entities. Do not ship a
sheet border/PDF as the cutting file, or count visible coincident projection
lines as proven closed contours. When DXF cleanup is performed externally,
preserve the original and recheck units, bounding size, closed loops, duplicates,
isolated/floating interiors and resulting hash. Keep the operator drawing as a
separate artifact from the cut file.

A supplier upload can reveal acceptance failures native geometry does not
model. Record that observed result if upload was requested, including which
exact file and options were assessed; do not invent a successful quote.
