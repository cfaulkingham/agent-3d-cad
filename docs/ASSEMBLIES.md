# Editable assemblies

An `assembly` feature groups independently editable source features into named
part instances. Each part keeps its own exact solid geometry and placement;
assembly construction does not fuse touching or overlapping parts. The saved
document remains the editable source for parts, placements, and rigid mates.
See [HANDOFF.md](HANDOFF.md) for executed validation evidence.

```json
{"schema_version":1,"units":"mm","parameters":{"gap":2},"features":[
  {"id":"plate","type":"box","size":[40,30,4]},
  {"id":"cover","type":"box","size":[40,30,3]},
  {"id":"case","type":"assembly","parts":[
    {"id":"base","input":"plate","placement":{"translation":[10,0,0]}},
    {"id":"lid","input":"cover"}
  ],"mates":[
    {"id":"lid_joint","type":"rigid","parent":"base","child":"lid",
     "parent_frame":{"origin":[0,0,4],"normal":[0,0,1],"x_direction":[1,0,0]},
     "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},
     "offset":[0,0,{"parameter":"gap"}],"angle_deg":0}
  ]}
],"output":"case"}
```

The example places the base at X=10 and the cover at Z=6. Editing `gap` moves
the cover while preserving its source dimensions. Editing the `cover` feature
changes the part itself. Reusing one input with several part IDs creates distinct
instances; an edit of that source changes every instance. Use separate source
features when independent dimensions are needed.

## Parts and placement

Each part requires a unique `id` within its assembly and an `input` naming an
earlier solid feature in the same document. Optional `placement` contains
`translation` and/or `rotation`. Translation is a three-element mm vector.
Rotation requires `origin` in mm, nonzero dimensionless `axis`, and `angle_deg`.
Rotation about the source-coordinate axis happens first, then translation.
An omitted placement or empty placement object is the identity transform.

Parts without an incoming mate are grounded by their placement, defaulting to
identity. A child controlled by a mate must omit `placement` entirely; even an
explicit identity placement conflicts. A root may anchor any number of children.
Part ordering does not control mate evaluation.

Assemblies contain 1–64 parts and up to 63 mates; the entire document permits at
most 256 assembly parts. Ordinary document, topology, mesh, worker and artifact
limits still apply, and the per-feature replication budget in
[PROTOCOL.md](PROTOCOL.md) bounds an assembly to 4,096 solids and 65,536 faces
summed over its part inputs. An input may contain multiple solids, such as a pattern or
STEP import, but is treated as one part instance. Nested assemblies and ordinary
solid operations consuming an assembly are rejected. Edit or transform source
features before assembling them. Source features and part IDs are distinct
namespaces; neither is an evaluated face or edge name.

## Rigid mates

A mate requires `id`, `type: rigid`, `parent`, `child`, `parent_frame` and
`child_frame`. Optional `offset` defaults to `[0,0,0]`; `angle_deg` defaults to
zero. Parent and child identify distinct existing part IDs. Mate IDs are unique
within the assembly. A child has at most one incoming mate, and cycles fail
explicitly; closed-loop constraint solving is not supported.

Each datum frame uses `origin`, `normal` and `x_direction` in that part's source
coordinates. Its normalized +Z is `normal`, normalized +X is `x_direction`, and
+Y is +Z cross +X. Both directions must be nonzero and perpendicular. Frame
origins and offsets use mm; frame directions are dimensionless; angles use
degrees. All scalar positions and angles retain parameter references and bounded
expressions with those units.

The mate computes the child's world transform as:

```text
parent_world * parent_frame * local_offset_and_Z_rotation * inverse(child_frame)
```

The offset is measured in the parent datum frame. Rotation is about that frame's
+Z; the translation offset is not itself rotated by `angle_deg`. With zero
offset and angle, the two datum frames coincide, including their normal
directions. To oppose normals, explicitly reverse the child frame normal and
choose a perpendicular X direction. This deterministic relation fixes all six
relative degrees of freedom. It is not a contact solver, interference check,
revolute joint, or motion simulation, and no gap or fit allowance is inferred.

## Editing and inspection

`cad_apply` and `cad_preview` accept these semantic operations:

```json
{"op":"set_part_placement","assembly_id":"case","part_id":"base",
 "placement":{"translation":[20,0,0]}}
```

```json
{"op":"set_mate","assembly_id":"case","mate":{
 "id":"lid_joint","type":"rigid","parent":"base","child":"lid",
 "parent_frame":{"origin":[0,0,4],"normal":[0,0,1],"x_direction":[1,0,0]},
 "child_frame":{"origin":[0,0,0],"normal":[0,0,1],"x_direction":[1,0,0]},
 "offset":[0,0,5]}}
```

```json
{"op":"remove_mate","assembly_id":"case","mate_id":"lid_joint"}
```

`set_part_placement` replaces a root's complete placement. `set_mate` replaces
the full mate with the same ID, or appends it when absent. `remove_mate` requires
the named mate to exist. Removing a mate makes its child a root at the default
identity placement; to retain or choose a world placement, include
`set_part_placement` in the same transaction. The final batch is validated, so
removal and placement may occur in either order. There is no inferred placement
bake when detaching. Replacing the whole assembly feature supports adding,
removing, or changing parts and removing a root's explicit placement before
making it a mated child. `set_parameter` updates shared dimensions or mate values.

Every edit follows existing expected-revision, worker validation and atomic HEAD
publication rules. Failed placements, invalid graphs and kernel failures preserve
the previous revision. Restore, historical queries, previews, live-view refresh,
jobs and exports use the same saved model contract.

An assembly summary includes `assembly.parts` with each part's `id`, `input`,
world `transform`, `bounds_mm` and `volume_mm3`; `assembly.mates` identifies the
rigid relations. A transform is a 16-number row-major homogeneous matrix that
maps a source point column vector `[x,y,z,1]` to world coordinates. Translation
occupies entries 3, 7 and 11. Assembly volume is the sum of part volumes, including
overlap; it does not describe the boolean union's volume.

Face and edge descriptors include `part_id` for ownership. Mesh edge polylines
also carry `part_id`; triangle ownership follows `triangle_faces` into the
topology table. Coincident instances keep distinct topology identities. Face
and edge IDs remain evaluation-local, and assembly edges do not advertise
geometric fillet selectors. Query and edit the source feature for geometry edits.
Provenance carries part IDs for instance history without claiming persistent
topology naming.

STEP and STL export positioned geometry as separate solids in a compound.
The saved JSON preserves part IDs, editable features and mates; exports do not
promise those semantic relationships or product hierarchy to other CAD systems.

## Exploded drawings

Drawing views can offset named assembly parts for presentation while retaining
the assembled model and its revision. Explosion offsets belong to the drawing
recipe and affect that view's projection and measured dimensions. They do not
edit placements, change mate solutions, or create a different manufacturing
shape. See [DRAWINGS.md](DRAWINGS.md) for the exact exploded-view fields,
parameter units, validation, and output behavior.

## Bill of materials

The BOM groups part instances by their source `input`, so the same source used
twice produces one row with quantity two. It counts instances rather than solids
inside an imported or patterned source. Different source features remain separate
rows even when their geometry or supplied part numbers match. Quantities come
from assembly membership and cannot be entered as metadata.

Optional assembly `bom` entries supply metadata for used source features:

```json
"bom":[
  {"input":"plate","item_number":10,"part_number":"PLATE-01",
   "description":"Drilled mounting plate","material":"Aluminum"},
  {"input":"spacer","part_number":"SPACER-01","description":"Tubular spacer"}
]
```

`input` is required and must be used by that assembly. Each input appears at
most once. Optional `item_number` is a unique integer from 1 to 999. Automatic
numbers are assigned in source-input lexical order using the lowest available
positive numbers while reserving every explicit number. Rows are returned in
item-number order. Changing assembly membership can therefore renumber automatic
items; assign explicit numbers when references must remain fixed across edits.
`part_number` and `material` accept 1–64 printable ASCII characters, and
`description` accepts 1–120. Omitted metadata stays absent; the service does not
infer material, specification, purchasing identity or manufacturing intent.

Use `set_bom_item(assembly_id,item)` to replace or add one complete metadata entry
by its `input`, and `remove_bom_item(assembly_id,input)` to remove an existing
entry. Removing metadata does not remove the source or instances: the row remains
and returns to automatic numbering with no optional metadata. Edits share the
ordinary expected-revision checks, preview, jobs and atomic publication rules.

`cad_bom` takes `document_id`, `revision` and optional `feature_id` (defaulting to
the output), and exports that committed assembly as independent JSON and CSV,
with a manifest written after all artifacts succeed. Its `bom` object contains
`assembly_id`, `items` and `total_quantity`; each item contains `item_number`,
`input`, `quantity`, `part_ids`, and any supplied metadata. Generation leaves
HEAD and historical exports unchanged. Drawing `bom: true` uses the same rows;
geometry-checked balloons identify their item numbers in assembled or exploded
views. See [DRAWINGS.md](DRAWINGS.md) for anchors, labels and table exports.
Standalone BOM export and BOM drawings also support `cad_job`; wait for terminal
success before using their manifest and artifacts.

## Inspecting individual parts

The live viewer provides **Hide/Show**, **Isolate**, and **Show all** controls for
the displayed assembly. Hiding a selected part clears its selection. Hidden
faces and edges do not render, intercept picks or appear in viewport captures.
Fit uses the visible parts, and Show all recovers an entirely hidden assembly.

Visibility is saved per workspace view and document, independent of editable
model revisions. It survives reopening and same-document edits for surviving
part IDs; removed IDs are pruned and switching documents resets it. The
`hidden_part_ids` in `cad_context` and Quick Edit describes what the user sees.
Measurements, BOM quantities, STEP/STL and drawings continue to use the complete
saved assembly. The standalone offline viewer does not have these controls.

The current scope is one-level assemblies, fixed placements, deterministic rigid
datum mates, source-grouped BOMs, live part visibility and exploded drawings with
part balloons.
Kinematics, general constraint solving, nested assemblies, collision checking and
automatic physical fit are future work.
