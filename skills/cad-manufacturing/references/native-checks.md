# Supported measurements and redesign

The smallest native request is:

```json
{"document_id":"actual_document","revision":1,"feature_id":"actual_solid","options":{"profile":{"process":"fdm","orientation":{"build_direction":[0,0,1],"x_direction":[1,0,0]}}}}
```

Replace identities with the actual saved source. Add only known inputs, in mm
and degrees. Optional `build_envelope_mm` is `[X,Y,Z]`; optional
`minimum_wall_mm` is a positive sampled-wall threshold. Review lengths refer to
the mm build frame even if source document units differ.

| Process / fields | Native evidence | Useful redesign when requested | Unmeasured boundary |
| --- | --- | --- | --- |
| `fdm`, `overhang_angle_deg` | Every native triangle; angle from vertical, non-bed offending area and witnesses | Compare orientations, replace selected horizontal ledges with chamfers, split into separately printable pieces, increase measured thin sections | No supports, print time, strength, bridge span, first-layer adhesion or slicer validation |
| `cnc`, `tool_radius_mm` | Concave cylinders parallel to tool/build axis; sampled tool-axis rays | Increase an undersized internal radius, move inaccessible pockets to another setup, simplify deep features | No full cutter/holder swept volume, stock, fixtures, feeds/speeds or toolpaths |
| `sheet_laser`, `sheet_thickness_mm`, `sheet_thickness_tolerance_mm` | Prismatic form and exact oriented height when form is verified | Remove partial-depth geometry, use actual stock thickness, separate bend/formed geometry from flat cut checks | No global minimum web/hole, kerf, nesting, bendability or vendor upload acceptance |
| `molding`, `parting_plane_mm`, `minimum_draft_deg` | Sampled signed draft and axial pull obstruction on each side of a stated plane | Add draft relative to the same pull, reposition parting plane if design permits, remove captured features | No complete undercut proof, material flow, shrinkage, cooling, tooling or global release |

Every profile requires nonzero perpendicular `build_direction` and `x_direction`.
Fields from another process are rejected. Report `parts[].checks[]` includes
`id`, `status`, `method`, `reason`, `evidence`; keep those scopes intact. Native
sampling is finite: `sampled_wall_thickness` is not a proven global minimum.
`mesh_topology` checks native triangles, not self-intersection or slicer repair.
`global_minimum_wall` and `mesh_self_intersections` remain unknown.

Each unique assembly leaf source is reviewed once **in its source coordinates**,
with occurrence quantities/paths. One orientation is applied independently to
each source, not to an assembled build plate. `options.parts` entries contain
`feature_id` and a complete `profile` to override individual sources.

Assembly clearance uses saved occurrence transforms. Use
`minimum_clearance_mm` in `options`; assemblies of at most 23 leaves measure all
pairs automatically. Above that, supply explicit `clearance_pairs` of `{a,b}`
with complete leaf paths; at most 256 pairs. A selected subset is never a claim
about every pair. Touching is zero clearance, not positive common material volume.
Saved static poses do not establish swept motion clearance.

For quantitative handoff retain document, revision, model hash, feature/part,
native build, complete options, artifact SHA-256, and every finding's coverage.
Choose a proposed remedy because it addresses a witnessed failure and preserves
the intended function. Changed limits need their own user/design/source basis.
