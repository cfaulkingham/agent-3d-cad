# Measured fabrication review

`cad_fabrication_review` measures an explicit saved revision against an explicit
process profile. It writes a checksummed JSON report and leaves the document,
history and earlier exports unchanged. Use `cad_job` for substantial work.

```json
{
  "document_id": "nested_assembly_demo",
  "revision": 1,
  "options": {
    "profile": {
      "process": "fdm",
      "orientation": {
        "build_direction": [0, 0, 1],
        "x_direction": [1, 0, 0]
      },
      "build_envelope_mm": [50, 50, 50],
      "minimum_wall_mm": 1.2,
      "overhang_angle_deg": 45
    },
    "minimum_clearance_mm": 0.5
  }
}
```

These numbers are illustrative fixture inputs, not recommended production limits.
Supply machine, material, cutter, stock and design requirements from the actual
workflow. No manufacturing allowance is selected automatically.

## Identity and coordinates

The reply contains `report`, source document/revision/feature, model SHA-256,
kernel version, native build identity, and the JSON artifact's path, SHA-256 and
byte count. The saved file has `schema_version`, `source`, `options` and `report`.
Its raw bytes verify against the reply. Reports remain movable.

For assemblies, each unique leaf source is reviewed once in its original feature
coordinates, with quantity and all occurrence paths retained. The build frame
applies to those sources independently. Clearance/interference uses their actual
saved occurrence transforms. A named pose draft is not saved geometry. Optional
`feature_id` selects an earlier solid or assembly; sketches are rejected.

An orientation requires nonzero perpendicular `build_direction` and
`x_direction`, each three finite numbers. They are normalized into a right-handed
frame. Envelope comparisons use exact oriented B-rep extents and assume translation
to the bed minimum. This is a measurement, not an orientation search or an edit.
Support structures and fixtures are outside those extents.

Optional `parts: [{feature_id, profile}]` replaces the default profile for a
selected leaf source. Overrides are complete profiles, not partial merges.
Duplicate or unselected sources fail explicitly.

## Checks and their coverage

Each check has `id`, `status`, `method`, `reason` and `evidence`.
`pass` means that named check met its caller limit within its stated coverage.
`fail` means measured evidence violated that check. `unknown` means missing
inputs, unsupported geometry/coverage or no measurement. A successful tool/job
can return findings with `status: fail`; execution success is not process approval.

Part and report status aggregate their checks: any failure gives `fail`;
otherwise any unknown gives `unknown`. Unevaluated global/process checks keep
ordinary reports unknown even when every measured local check passes.

| Check | Measured evidence | Limits |
| --- | --- | --- |
| Build envelope | Exact bounds in the supplied build frame | Requires `build_envelope_mm`; supports, fixtures and placement planning are unknown |
| Mesh topology | Every native triangle; 0.000001 mm Euclidean vertex weld, edge incidence/winding, degeneracy and signed volume | Native deflection is 0.1 mm / 0.5 rad; self-intersection, slicer repair and external-mesh validation remain unknown |
| Sampled wall thickness | Exact inward-normal ray chords at interior, reclassified UV samples; witnesses include point, direction and distance | Requires `minimum_wall_mm`; at most 128 points/source and 4,096 total. Skipped/unresolved chords are recorded; finite samples cannot prove a global minimum |
| FDM overhang | Every native triangle, angle from vertical, offending area and witnesses | Requires `overhang_angle_deg`; horizontal downward bed-contact triangles are excluded. Support design, material and actual slicing are unknown |
| CNC cylinder radius | Exact concave cylindrical faces parallel to the supplied tool/build axis | Requires `tool_radius_mm`; smaller cylinder radius fails. Non-axial/unsampled cylinders, other pocket corners and swept cutter reach are unknown |
| CNC point access | Exact rays toward the tool axis from sampled upward-facing surfaces | Obstruction witnesses identify hidden points; cutter radius, stock, fixtures and toolpaths remain unknown |
| Sheet prismatic form | Exact planar end caps and parallel planar/cylindrical/extruded walls | Tilted planes and non-extruded cylinders, cones, spheres and tori fail. Other surface types remain unknown |
| Sheet stock thickness | Exact oriented height of a verified prismatic source | Requires both `sheet_thickness_mm` and `sheet_thickness_tolerance_mm`; kerf, bends, material and vendor acceptance remain unknown |
| Mold draft/undercuts | Exact sampled normals and pull rays on each side of the supplied parting plane | Requires `parting_plane_mm`, plus `minimum_draft_deg` for draft comparison. Samples on the plane are skipped. Global release, cores, shrinkage, fill and material remain unknown |
| Assembly interference | Exact B-rep common solid volume for recorded pairs | Touching faces/edges have zero material volume; numerical volume threshold is 0.000000001 mm³ |
| Assembly clearance | Exact B-rep minimum distance for recorded pairs, with witness points | Requires `minimum_clearance_mm`. Interference fails even with a zero requested gap |

Mold draft is signed relative to the outward pull direction: sources above the
parting plane pull along the build direction, those below pull oppositely. The
plane coordinate uses the source build frame. Vertical walls have zero draft;
caps normal to their pull direction have 90 degrees. The method describes the
stated two-sided axial setup only.

Face and triangle identifiers are temporary report-local evidence, qualified by
source identity and native build. They are not persistent design references or
committed viewer picks. Witness arrays contain at most 32 entries per check;
counts/coverage identify additional findings.

## Process options and bounds

`process` is `fdm`, `cnc`, `sheet_laser` or `molding`. Every profile requires
orientation. Common optional inputs are build envelope and minimum wall.
Overhang angle belongs to FDM; tool radius belongs to CNC; stock thickness and
allowance belong to sheet/laser; draft and parting plane belong to molding.
Wrong-process fields fail schema/native validation.

Lengths are in mm and angles in degrees. Positive lengths are at most 1,000,000;
clearance and sheet allowance may be zero. Overhang angle is 0–90 degrees,
minimum draft 0–89 degrees. Parting plane and vector components are finite within
±1,000,000. Identifiers retain the ordinary model/occurrence contracts.

At most 256 unique sources/overrides and 200,000 aggregate triangles are reviewed,
within ordinary topology, worker time/memory and 64 MiB report limits. Missing
sample coverage is recorded rather than silently passed.

Assemblies with at most 23 leaves automatically measure all pairs. Larger
assemblies require explicit `clearance_pairs: [{a, b}]`, at most 256 pairs.
Names are complete selected leaf paths such as `left/link`; groups, duplicates,
reversed duplicates and self-pairs fail. Explicit subsets retain their scope.
No measured pairs gives unknown, including an explicitly empty list.

## Manufacturing package integration

Supply the same review options as `cad_manufacture.options.fabrication_review`.
The native package then includes a hashed relative `review.json` and
`process_review: {status, report_path}`. The report preserves options, source
hash/revision and native build. Findings do not suppress geometry exports;
failed/unknown reviews remain failed/unknown in the manifest.

Omitting the review keeps `process_review.status: not_evaluated`.
Report/package publication is atomic and cancellable; failed computation
publishes no partial artifact and changes no revision.

## Measured findings and guidance

`guidance.basis: caller_limits_only` and an empty `sources` array distinguish
measured comparisons from process advice. The engine supplies no prescriptive
vendor/material thresholds or certification. Any external process guidance used
by an agent needs its own current citation and must stay separate from these
measurements. Native review does not generate G-code or authorize printer motion.
