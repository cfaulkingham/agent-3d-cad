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
sketches, plus `path`. The path remains an authored point array or exact wire of
line, arc, Bezier and interpolated spline segments. Each varying section has one
planar outer region and the same number of interior boundaries. Interior
boundaries are paired by their centers in the local sketch frame.
Section origins must lie at unambiguous strictly increasing path stations,
including both endpoints, with each plane perpendicular to its path tangent.
Sections are not moved automatically to the path.

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
