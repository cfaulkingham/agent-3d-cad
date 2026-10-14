# Controlled extrusions and sweeps

These features extend the saved native document. They use the pinned OCCT kernel
inside bounded workers and do not execute scripts or require a Python runtime.
The original `extrude` and `sweep` forms retain their behavior.

## Extrusion

`extrude` takes a planar sketch `input` and one of two extent modes:

- `distance`: signed travel in millimeters. Optional `direction` is a nonzero
  dimensionless vector, normalized by the kernel; it defaults to the sketch's
  normal and must have a component normal to the sketch. `both:true` sweeps each
  side by the full distance, so a distance of 5 spans -5 to +5. Optional
  `taper_deg` gives a constant profile inset along each side: positive narrows
  the exterior and widens holes; negative expands the exterior and narrows holes.
  Angles must have magnitude below 89 degrees. Exact contour offsets and ruled
  lofts preserve curved boundaries. Collapsed, divided, or invalid boundaries
  fail rather than shortening the requested distance.
- `until:"first"|"last"` plus `target`: terminates against an earlier solid
  feature in the requested direction. Exact Boolean partitions determine the
  boundary, including a sloping or curved target. The target must terminate the
  entire source profile; misses, partial coverage, and targets behind the profile
  fail. Distance, bidirectional extent and taper are excluded in this mode.

Tapered regions with holes are made from exact outer lofts with inner lofts
subtracted. Separate sketch regions are built individually and joined, without
discarding an unsuccessful region. Target dependencies participate in cache
invalidation, component capture and provenance.

## Sweep

`sweep` takes either one sketch `input` or an ordered `sections` array of 2–32
sketches, plus `path`. The path is an authored point array, exact wire of
line, arc, Bezier and interpolated spline segments, or the ID of an earlier curve
feature. Saved curves include `curve_helix`, `curve_trim` and `curve_extract`.
The native wire, orientation and surface-curve representation are retained;
there is no polyline sampling or spline refitting. Curve edits invalidate the
sweep cache and participate in component capture and provenance, including
sweeps with multiple section sketches. A saved curve must contain one connected
unbranched wire; solids and sketches must first yield a curve through explicit
edge extraction. Each varying section has one
planar outer region and the same number of interior boundaries. Interior
boundaries are paired by their centers in the local sketch frame.
Section origins must lie at unambiguous strictly increasing path stations,
including both endpoints, with each plane perpendicular to its path tangent.
Sections are not moved automatically to the path.

For a right-handed helix on the world Z axis, radius R and pitch P, its start
is `[R,0,0]` and its tangent is parallel to `[0,2*pi*R,P]`. A circle sketch at
that location with the same normal and X direction `[1,0,0]` can sweep directly
with `{"id":"spring","type":"sweep","input":"wire_profile","path":"helix",
"orientation":"frenet"}`. Use dimensioned parameter expressions to keep the
profile frame consistent with editable helix dimensions. Curve queries return
native start points and oriented tangents for other frames or trimmed paths.
The bundled `helix-sweep.create.json` example keeps its radius,
pitch and profile frame connected through editable parameter expressions.

Optional orientation controls are mutually exclusive:

- `orientation:"corrected_frenet"` (default), `"frenet"`, or `"fixed"`.
- `binormal`: a nonzero constant dimensionless direction.
- `guide`: a second exact wire using native curvilinear correspondence to control
  the sweep frame, without contact or automatic profile scaling.

`transition` is `"transformed"` (default), `"right_corner"`, or `"round_corner"`.
Profiles with holes sweep their boundaries separately and subtract the interior
solids. The result must contain only valid closed solids with positive volume.
Missing sections, invalid frames, changing hole counts and kernel failures retain
the feature ID and leave the committed revision unchanged.

These controls do not provide a general thickness law or an arbitrary expression
evaluated continuously along a path. Varying sections provide explicit editable
stations. Complex paths or sections may be rejected by OCCT; an error does not
authorize moving, simplifying or dropping the requested profile.

## Evidence

The `richer_modeling` native suite checks analytic volumes and bounds for directed,
bidirectional and tapered extrusion, finite target termination, controlled and
varying sweeps including holes and curved paths, dependent cache keys, committed
parameter edits, failed-edit rollback, cold reopening and exact STEP export.
Executed test results and portability limits are recorded in `HANDOFF.md`.
