# Analytic mesh recognition and guided reconstruction

`cad_artifact` adds read-only `action: "recognize"` for captured STL, GLB and 3MF
reviews. First run ordinary `action: "review"` with original source SHA-256 and
explicit units. Recognition takes that result's absolute `review.json` path and
review SHA-256. The native service verifies the complete portable package,
reparses captured bytes, runs fitting in a bounded geometry worker, and verifies
the package again before returning. Deleting the external original does not
break a capture; changing a captured source, review or ledger is rejected.
STEP and robot/drawing reviews do not enter this mesh workflow.

```json
{
  "action": "recognize",
  "review_path": "/absolute/package/review.json",
  "expected_sha256": "<review SHA-256>",
  "options": {
    "distance_tolerance_mm": 0.05,
    "normal_tolerance_deg": 10,
    "weld_tolerance_mm": 0.0000001,
    "min_triangles": 8,
    "max_patches": 16,
    "max_candidates": 256
  }
}
```

All six options are required. Distance tolerance is 0.000001–10 mm; normal
tolerance is 0.01–45 degrees; weld tolerance is 0–1 mm and cannot exceed distance
tolerance. Zero weld tolerance uses exact coordinate equality. Minimum patch
size is 2–10000 triangles; patch count is 1–64 and candidate count is 8–512.
Captured geometry limits remain 200000 vertices/triangles, including the STL
reader's duplicated vertices. Recognition coordinates currently use the native
model range of ±1000000 mm. Worker wall-time, memory, cancellation and durable
`cad_job` submission apply to recognition and proposal validation.

## Evidence and limits

Each accepted connected patch reports its analytic plane, cylinder or sphere,
source triangle membership, area, area fraction and residual evidence. The
`origin_mm`, unit `axis` and `x_direction` give a right-handed frame; its Y axis
is `axis × x_direction`. A cylinder's origin lies on its axis, and a sphere's
origin is its center. `radius_mm` is zero for a plane. Plane UV bounds and axial
ranges describe observed vertices in that frame; they do not recover trim
curves, closure or manufacturing intent.

`max_distance_mm` bounds distance from every point of every assigned triangle
to the infinite analytic surface. Plane extrema occur at vertices. Sphere and
cylinder radial extrema include the closest point over the complete triangle
(or its projection perpendicular to the cylinder axis), so a coarse polygon
whose vertices lie on a perfect cylinder can still fail on facet sagitta.
`vertex_rms_distance_mm` explicitly samples triangle vertices; it is not an
area-weighted or Hausdorff metric. Normal deviation compares geometric facet
normals to the analytic normal cone over the triangle, treating opposite winding
as equivalent and returning a conservative 90-degree bound across a sign change.
Stored STL normal records are not trusted. `worst_triangle` identifies the facet
producing the largest full-triangle distance bound.

Adjacency uses source edges after optional bounded vertex welding. Welding only
connects triangles: it never moves coordinates or alters residual calculations.
Nonmanifold edges do not connect regions. Smooth connected components receive
least-squares plane, cylinder and sphere hypotheses; bounded deterministic local
hypotheses handle mixed components. Every inlier is independently checked against
both caller tolerances. Accepted components are removed before subsequent fits.
This is a bounded search, not a complete analytic decomposition algorithm. Small,
disconnected, highly noisy, undersampled, degenerate and difficult mixed regions
may remain unrecognized. All source triangles appear exactly once in a patch or
`leftover_triangle_indices`, with leftover area and search-budget qualification.
No outlier is silently discarded or repaired.

Patch IDs contain the complete source SHA-256 and a digest of the review identity,
options, primitive kind and sorted triangle membership. They are reproducible for
the same captured review, options and native algorithm, and are not native CAD
face selectors. A different source or options must be recognized again. Patch
membership does not establish original feature history, watertightness or material.

## Explicit reconstruction guides

Repeat recognition with up to 16 `reconstruct` entries referring to returned
patch IDs. Each guide supplies a unique `feature_id` of at most 40 characters:

```json
{"patch_id":"<returned ID>","feature_id":"Panel","extent":"rectangle",
 "bounds_uv_mm":[[-10,-5],[10,5]],"thickness_mm":2}
```

Planes require increasing caller UV rectangle bounds and positive thickness.
Cylinders require `extent: "cylinder"` and increasing `axis_range_mm: [start,end]`
in the patch frame. Spheres require explicit `extent: "sphere"`. Extra extent
fields, mismatched primitive kinds and stale patch IDs fail. Guide choices are
intentional extrapolation: the slab fills the caller rectangle, a cylinder fills
a full circumference and its end caps, and a sphere fills its complete interior.
Missing trim, holes, unseen surface and original history are never inferred.

Each proposal returns an ordinary editable `model`, matching `operations` for
`cad_apply`, an exact-solid summary and an explicit extrapolation statement.
The native worker actually validates and builds the model before returning it.
Planes and cylinders use sketch/extrude; spheres use an exact semicircle/revolve.
Dimensions become named editable parameters. These are exact analytic CAD
constructions fitted to approximate source evidence, not an exact replacement
of the mesh or a claim that the original CAD history has been recovered.

Adopt the returned model through ordinary `cad_create`, or apply its operations
with the current `expected_revision`. Adoption uses existing transactional checks;
feature/parameter IDs must be chosen to avoid collisions in the destination.
No document or revision is created by recognition. The evidence remains in the
source-qualified report; no unrecognized metadata fields are inserted into models.

## Verification

`mesh_reconstruction` tests independently generated plane/cylinder/sphere fixtures,
mixed regions and leftovers, partial cylinders, deterministic noise, facet-sagitta
rejection, membership conservation and independent barycentric residual samples.
Guided proposals are adopted through both create and apply, exported to STEP and
read independently for exact validity/analytic volumes. Portable captures, source
mutation, stale guides and durable successful/failed jobs are covered.
`tests/mesh_reconstruction_schema_tests.py` checks live Draft 2020-12 input/output
contracts, actual capture/verify/recognize/propose/create/export results and malformed
requests. Native runtime has no Python, sibling-library or copied sibling-code dependency.
