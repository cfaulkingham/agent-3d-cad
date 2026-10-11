# Six measured build orientations

Use the reusable frames in [orientations.json](../assets/orientations.json).
Each label names the **source direction that becomes build +Z**, not the face
that touches the bed. Native bounds assume translation to the bed minimum.
These frames do not rotate or commit source geometry.

For one selected solid, keep document/revision/feature, machine envelope,
material limits, wall limit, and overhang threshold identical. For each frame,
replace only `options.profile.orientation` and call `cad_fabrication_review`.
Do not replace six measurements with six copies of one report. A practical job:

```json
{"action":"submit","request_id":"unique_orientation_z_plus","tool":"cad_fabrication_review","arguments":{"document_id":"actual_document","revision":1,"feature_id":"actual_solid","options":{"profile":{"process":"fdm","orientation":{"build_direction":[0,0,1],"x_direction":[1,0,0]},"build_envelope_mm":[200,200,200],"minimum_wall_mm":1.2,"overhang_angle_deg":45}}},"budget":{"timeout_ms":30000,"memory_mb":2048}}
```

The numbers above are **fixture examples**, not production recommendations.
Replace them with sourced/user limits. Poll `cad_job` with `action: get` and the
returned job ID until terminal. Reuse the same live handle after a transient
poll failure; record actual failures and unknowns. Pace jobs within available
worker capacity (native maximum four); six sequential calls also work.

Extract from each part's checks by `id`, never array position:

- `build_envelope.evidence.size_mm`, and the envelope status.
- `fdm_overhang.evidence.area_exceeding_limit_mm2`; this excludes downward
  horizontal triangles at the bed and is **not** support volume or print time.
- `sampled_wall_thickness.evidence.minimum_sampled_chord_mm`, together with
  sample/skipped coverage and its status. Missing evidence stays unknown.
- Other failed/unknown checks and the saved report hash/path.

Compare a table of label, X/Y/Z bounds, envelope fit, offending area, sampled
wall evidence, and unresolved criteria. Reject known envelope violations for
that machine. Among remaining candidates use the user's priority: layer/load
alignment, functional surface quality, accessible supports and then measured
overhang/height. Do not collapse missing data into zero or invent a numeric
strength/support score. Say which measured tradeoff supports the recommendation.
Six axis directions are a baseline search, not an optimum over all rotations.

For source edits or build placement, re-read current revision before applying
the requested transform or creating a separate print-oriented document. Native
slicing preserves source Z orientation; a winning report frame alone does not
change slicer input. Re-measure the actual transformed source in +Z before
export/slicing. A requested review alone does not require a source edit.
