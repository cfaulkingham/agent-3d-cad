# Articulated gripper reference

This editable native document contains two mirrored, two-joint fingers, a mounting
plate, a hollow actuator housing and 48 leaf solids made from 19 distinct source
parts. It exercises shared dimensions, reused hardware, nested assemblies,
coupled motion, named poses and manufacturing handoffs. Hardware shapes and
materials are design assumptions; no commercial actuator or fastening system has
been selected. The housing's actuator envelope is not a working transmission.

## Open and edit

Pass [gripper.create.json](gripper.create.json) to `cad_create`, then use
`cad_open` on `articulated_gripper`. This is the complete editable source; end
users do not run a generator or install a language runtime.

Each finger has `open` (0 degrees), `approach` (-5 degrees) and `grasp`
(-10 degrees) poses. The knuckle is driven 1:1 by the base hinge. Right-hand
joint datum normals are reversed so both fingers close toward the center while
reusing the same exact part geometry. Apply the matching pose to both `finger`
and `right_finger`; [gripper.close.edit.json](gripper.close.edit.json) shows the
first edit from revision 1. Read the current document before later edits and
replace `expected_revision` accordingly.

Parameters are `proximal_length` (48 mm), `distal_length` (32 mm),
`base_thickness` (8 mm) and initial `curl_angle` (0 degrees). The native service
retains their editable geometry and normal revision history. A named pose sets
joint values; it does not provide a physical actuator, contact controller or
feedback loop.

[gripper.review.json](gripper.review.json) reviews one source proximal link
under explicitly illustrative FDM limits. [gripper.manufacture.json](gripper.manufacture.json)
exports the whole assembly's source, unique part geometry and BOM. Update their
revision to the intended committed pose. Machine/material selections and fit
allowances must come from the actual fabrication workflow before making parts.

## Reproduce the checks

The repository's [verify.py](verify.py) is a developer acceptance runner:

```sh
python3 examples/articulated-gripper/verify.py /absolute/path/to/agent-3d-cad --output /new/isolated/evidence-directory
```

It refuses an existing workspace. Every tool request/result, failed probe,
source identity and output hash is retained in that directory. It calls the
native Service through the CLI; Python generates no geometry and is not an
end-user requirement. The native input files can be regenerated for repository
maintenance with [generate.py](generate.py).

The runner verifies independent trigonometric forward transforms for both
fingers in all three poses, 16 explicitly selected clearance pairs per pose,
parameter-driven length changes, rejected edits preserving HEAD/history, BOM
quantities, STEP readback, GLB leaf placements, native PNGs, dimensioned vector
drawings, six source build orientations, every relocated manufacturing artifact
hash, and an editable rebuild in a separate workspace. It adds controlled
invalid/collision fixtures solely to expose current limits. It does not upload
files, slice, start hardware or operate a supplier account.

## Measured reference results

On 2026-10-11 the corrected native source hash was
`f28ab8e6b755597c92f72572638fc71379d1559e1e6cc25afcdbc9230fc54fdc`.
The final integrated run passed 947 checks, including drawing dimensions
compared with exact model bounds and all 19 independently expected BOM sources.
Run output records its executable/build identity and check count.

| Pose | Each independent hinge | Measured pad separation |
| --- | --- | --- |
| Open | 0 degrees | 51.600000 mm |
| Approach | -5 degrees | 27.159273 mm |
| Grasp | -10 degrees | 3.768922 mm |

The selected interfaces have zero common material volume in these three saved
poses. The native pin/bush and bush/link gaps are 0.1 mm; adjacent link levels
have 1 mm axial separation. These are nominal CAD values, not chosen production
fits. Contact surfaces intentionally measure zero gap. Only 16 of 1,128 possible
leaf pairs are assessed; neither that subset nor the three poses proves full
assembly or swept clearance.

With the illustrative 1.5 mm wall / 45-degree FDM profile, proximal-link
orientations measure:

| Source direction used as build +Z | Build-frame size (mm) | Non-bed overhang area (mm²) |
| --- | --- | --- |
| +Z | 14 × 62 × 8 | 0 |
| -Z | 14 × 62 × 8 | 168 |
| +X / -X | 62 × 8 × 14 | 324.241 |
| +Y / -Y | 14 × 8 × 62 | 205.741 |

The sampled minimum wall is 2 mm in every frame. +Z is the useful baseline for
this geometry and stated profile. Support volume/time, material anisotropy and
actual slicing remain unmeasured; even that orientation's report is `unknown`,
not manufacturing approval.

## What the project exposed

- **Geometry validity does not establish assembly fit.** The initial base pins
  overlapped the plate by 19.635 mm³ each, and rear support posts intersected
  adjacent screw heads by 31.470 mm³. Added pivot bores and relocated posts
  remove those measured clashes. Pads now have a finite top-face interface and
  symmetric contact ribs rather than a tangential-only end attachment.
- **Forward kinematics does not prevent contact.** Permitting -12 degrees on
  both joints commits successfully but overlaps the opposing pads by 65.986 mm³.
  The shipped limits/poses stop at -10 degrees. The runner recreates the -12
  degree collision in its separate `collision_limit_probe` document, showing
  why a motion limit must be checked geometrically.
- **Closed loops are rejected.** A rigid return mate from `distal` to `mount`
  yields `invalid_model: Assembly mate graph contains a cycle`; a four-bar or
  tendon drive needs a separate kinematic model or future solver capability.
  Rejecting that edit preserves the original source.
- **Full clearance is bounded.** `cad_measure` all-pairs mode rejects the 48-leaf
  assembly because its current limit is 23 leaves. Explicit scoped fabrication
  pairs remain usable, with incomplete global coverage kept visible.
- **The model is rigid and unsourced.** Linear joint coupling is a declared
  relation, not a force/transmission solve. Pad compliance, actuator torque,
  fastener threads/retention, contact friction, fatigue and build performance
  require further engineering; the reference does not simulate them.

These are reproducible observed boundaries and explicit design omissions, not
claims that the native geometry or manufacturing tools silently solve them.
