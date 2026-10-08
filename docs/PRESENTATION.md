# Live clipping and exploded inspection

The live viewer saves presentation independently of editable geometry. Its
Visual inspection panel enables a clipping plane, changes its axis and offset,
reverses the kept side, and separates assembly leaves by a distance in
millimeters. The Exact section panel calculates filled cut surfaces and exact
dimensions for that view. Reset returns to the assembled, unclipped view.

`cad_viewer`'s `context` action accepts the closed optional `presentation` object:

```json
{"clip":{"normal":[0,0,1],"offset_mm":3,"keep":"positive"},
 "explode":{"distance_mm":20,"directions":[
   {"part_id":"left/pin","direction":[0,0,1]}]}}
```

Both `clip` and `explode` are required when this object is supplied. `clip:null`
disables clipping. Otherwise the unit normal, finite offset and kept side are
required. Positive keeps `normal · displayed_point >= offset_mm`; negative
keeps the opposite half. Clipping operates after exploded displacement.
Offsets are bounded to ±1e12 mm. Directions have three components in [-1,1]
and squared length within 1e-6 of one. Explosion distance is in [0,1e6] mm;
positive distance requires an assembly. Up to 1,024 unique overrides may name
current leaf occurrence paths; hierarchy groups are not valid overrides.

Each leaf moves by distance times its direction. Without an override, direction
points from the source assembly bounds center to the leaf bounds center.
Coincident centers use deterministic signed axes in sorted occurrence order.
Source vertices, topology identity, exact measurements, mates, exports, BOMs,
model history and committed revisions retain their original meaning.

The default is `{"clip":null,"explode":{"distance_mm":0,"directions":[]}}`.
Ready sync and `cad_context` return current presentation, including defaults for
older views. Omitted input retains current settings. Matching revisions retain
the plane and distance and prune overrides for removed leaves; a solid output
resets distance to zero. Retargeting a view to another document resets settings.
Stale selection context does not restore obsolete presentation or geometry picks.

The app persists changes in order. Failed writes remain visibly unsaved and can
be retried. Delayed sync responses cannot undo pending or acknowledged local
changes. Quick Edit snapshots include presentation and become invalid after
presentation or visibility changes. Reopening restores camera and presentation
without reviving an obsolete pick.

The separate optional `appearance` setting colors the model and individual
assembly leaves without changing clipping, explosion or section qualification.
Saved review views capture camera, presentation, appearance and visibility in
one native preset. The viewer's Colors and Saved views panels edit these
settings and restore them when reopening the same model/view. See
[APPEARANCE.md](APPEARANCE.md) for the closed RGB contract, native preset limits,
revision handling and PNG image capture.

CPU picking and GPU rendering apply the same half-space in displayed coordinates.
Discarded faces do not occlude picks; edge segments are clipped before picking.
Hidden leaves remain excluded. Extended depth bounds support exploded geometry.
Changing only clipping updates uniforms and picking without rebuilding buffers;
graphics-context recovery retains current settings.

Clipping alone is tessellated and uncapped, with a 1e-7 tolerance in normalized
model coordinates. **Calculate exact section** intersects the native source
B-reps at the current displayed plane, including exploded leaf displacements,
then renders the qualified material regions and boundary curves. Its exact area
and boundary dimensions are independent of the cap mesh. Actual holes remain
open, and derived surfaces occlude original geometry behind them without
creating editable face/edge references. Moving the plane or explosion clears
the section; kept-side reversal and visibility retain its geometric result.
Hidden parts are removed visually but remain in the stated query coverage and
totals. [SECTIONS.md](SECTIONS.md) defines the calculation, bounds, cancellation,
source qualification and explicit subset behavior.

These review controls do not measure manufacturing distance, collision or clearance.
Exact topology-pair distances, witnesses, supported analytic angles and saved-pose
assembly clearance/interference are available separately through `cad_measure`
and the live Exact measurement panel. [MEASUREMENTS.md](MEASUREMENTS.md) defines
their source qualification and coverage; presentation does not alter them.
Annotations, time sequences and external-format review remain in
COMPOSITION_FABRICATION_REVIEW.md. HANDOFF records executed validation scope.

Saved [inspection notes](ANNOTATIONS.md) project numbered review pins at native
model/leaf bounds centers or resolved entity inspection centers. The coordinates
belong to one committed evaluation and are separate from presentation settings.
Current pins follow owner explosion and visual filtering; source changes retire
anchors without rebinding their text. Saved-view apply preserves current note
metadata. PNG capture composites visible pins and labels, while agent request
context carries complete text and explicit current/retired source identities.
