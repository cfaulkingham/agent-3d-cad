# Model, CLI, and MCP protocol

Implemented contracts for the native preview. Runtime discovery (`tools` or MCP
`tools/list`) publishes input and output JSON Schemas. Model definitions live in
`model_definitions()`; service contracts live in `tool_definitions()`. Each
published Draft 2020-12 schema is standalone: its `$defs` contains exactly the
definitions its local JSON pointer or `$anchor` references reach, transitively,
and is omitted when it has none. Lossless sharing, private aliases, inlining and
short local anchors keep discovery compact while retaining every constraint.
Resolve the published references rather than assuming private definition names
or pointer syntax. The MCP server builds this catalog once per process.

Plugin startup: `agent-3d-cad serve --default-workspace` uses a persistent
Documents/Agent CAD folder outside the plugin installation. Adding
`--workspace PATH` overrides that default for existing projects and must be
absolute in this mode. Claude uses `--default-workspace --workspace-setting VALUE`:
an empty value or the exact unset marker `${user_config.workspace}` chooses the
native default; other values must be absolute paths. No path-template expansion
is performed. Both startup flags are only accepted for `serve`; existing CLI
commands still require an explicit workspace. Modeling/tool/document contracts
are unchanged. See `RELEASE_1_0.md`
for the two desktop plugin targets and unfinished host acceptance gates.

## Document and scalar values

```json
{"schema_version":1,"units":"mm","parameters":{"height":6},"features":[
  {"id":"base","type":"box","size":[20,10,{"parameter":"height"}]}
],"output":"base"}
```

Unknown fields are errors. Identifiers match `[A-Za-z][A-Za-z0-9_-]{0,63}`.
A *new* document (`cad_create`, `cad_import`), job request (`cad_job` submit) or
view (`view_id`) must also not be a Windows device name: `CON`, `PRN`, `AUX`,
`NUL`, `COM0`–`COM9` or `LPT0`–`LPT9`, in any letter case. This is enforced on
every platform so new workspace files stay portable, and fails with
`invalid_argument`. Existing documents, jobs and evaluations are addressed with
the plain grammar: a POSIX workspace that already holds a document named `aux`
keeps reading, editing and listing it (it still cannot be opened on Windows; rename
its `documents/` directory and the `document_id` inside its records to move it).
Names inside a model (features, parameters, assembly parts and mates) never become
standalone file names and are not restricted by the Windows device-name rule.
Feature-scoped export names include the document and revision as well.
A `request_id` on a document tool is stored in
a receipt, not used as a file name, so it is not restricted either.
A document has at most 128 finite numeric parameters and 256 ordered features.
Dependencies name earlier features; IDs survive parameter edits. Every feature
is validated, including branches outside `output`. Output is solid material or
an explicit surface feature. Sketches remain intermediate planar regions.
Surface patches and shells report zero material volume until `surface_solid`
or `thicken` creates a valid solid; see [SURFACES.md](SURFACES.md).

Optional `components` records up to 64 captured source revisions, embedded model
snapshots and SHA-256 checksums, scalar bindings, and feature/parameter identity
maps. Their materialized dependencies are ordinary local editable features.
Snapshots have at most four provenance levels and count toward the 1 MiB document
metadata limit; embedded STEP, SVG/DXF source and font bytes have separate
budgets. Rebuilding a consumer needs no source document. See [COMPONENTS.md](COMPONENTS.md).

A scalar is a finite number within ±1,000,000, a parameter reference, or a bounded
arithmetic tree. Dimensions use mm; directions are dimensionless; rotation uses
degrees; selection angular tolerance uses radians and face-area predicates use
`mm2`. Numeric literals and parameter
references take their argument's unit. Explicit expressions must declare the
matching unit; no strings, scripts or implicit conversion are evaluated.

```json
{"expression":{"op":"multiply","args":[{"parameter":"height"},2],"unit":"mm"}}
```

`add` and `subtract` use two operands in the result unit. `multiply` and `divide`
scale the first operand by a dimensionless second operand. Each tree permits
128 nodes and 16 levels, rejects division by zero, and bounds intermediate results.
Primitive dimensions and radii are at least 0.00001 mm. Vectors have three scalars.

## Features

Every feature requires `id` and `type`. Fields below are additional fields.

| Type | Required fields | Optional fields / meaning |
|---|---|---|
| `box` | `size` | `origin` defaults to `[0,0,0]`; minimum corner |
| `cylinder` | `radius`, `height` | `origin` defaults to base center `[0,0,0]`; +Z |
| `external_thread` | `major_diameter`, `pitch`, `length` | +Z; optional `origin`, `handedness` (`right` default or `left`) |
| `cut`, `fuse` | `left`, `right` | Earlier solid features |
| `fillet` | `input`, `radius`, `edges` | `edges` is `"all"` or a geometric selector |
| `chamfer` | `input`, `distance`, `edges` | Symmetric chamfer; same edge selector contract as fillet |
| `shell` | `input`, signed `thickness`, `faces` | One solid; explicit oriented geometric face selectors remove openings; `faces:[]` makes a sealed cavity; optional `join: arc|intersection` |
| `offset` | `input`, signed `distance` | Independent parallel offset of each source solid; optional `join: arc|intersection` |
| `thicken` | `input`, signed `thickness` | Sketch regions, an open surface patch/shell, or selected connected open solid faces; `faces` required for solids; optional join |
| `sketch` | `workplane`, `profile` | Numeric or captured text/SVG/DXF profile; no constraint solver |
| `sketch_cut`, `sketch_fuse`, `sketch_intersection` | `left`, `right` | Exact coplanar planar regions, with holes retained |
| `sketch_offset` | `input`, signed `distance` | Optional `join: arc|intersection` |
| `sketch_fillet`, `sketch_chamfer` | `input`, `radius` or `distance`, `vertices` | All eligible corners or an expected-count geometric point selector |
| `sketch_transform`, `sketch_instance` | `input` | Optional `translation`, `rotation` |
| `sketch_mirror`, `mirror` | `input`, `plane` | Exact sketch or solid reflection in an explicit workplane |
| `sketch_face` | `input`, `faces` | Selected coplanar solid faces as a sketch |
| `sketch_projection` | `input`, `faces`, `workplane` | Exact orthogonal projection of planar solid-face boundaries |
| `split` | `input`, `plane`, `keep` | Solid halfspace split; `keep: both|top|bottom`, both sides must contain material |
| `surface_bezier` | `control_points` | World-space rectangular pole grid; optional rational `weights` |
| `surface_bspline` | `control_points`, `degree_u`, `degree_v`, `knots_u`, `knots_v`, `multiplicities_u`, `multiplicities_v` | Nonperiodic exact patch; optional rational weights |
| `surface_trim` | `input`, `u_range`, `v_range` | Exact rectangular UV trim of a patch |
| `surface_shell` | `inputs`, `tolerance`, `closed` | Connected manifold sewing with explicit closure claim |
| `surface_solid` | `input` | Closed shell materialization; optional explicit `reverse` |
| `sheet_metal` | `input`, `thickness`, `k_factor`, `flanges` | One planar sketch region with holes; exact signed cylindrical bends and direct base-edge flanges |
| `sheet_unfold` | `input` | Developed solid blank from unchanged sheet-metal intent |
| `extrude` | `input`, either `distance` or `until` + `target` | Signed travel; optional `direction`, `both`, `taper_deg`; first/last exact target termination excludes distance/both/taper |
| `revolve` | `input`, `axis`, `angle_deg` | Sketch; angle in (0,360] |
| `loft` | `sections` | 2–32 sketches; optional `ruled` boolean |
| `sweep` | Either `input` or `sections`, plus `path` | Exact path; optional orientation/binormal/guide and transition controls; 2–32 varying sketch stations with matching holes |
| `transform`, `instance` | `input` | Optional `translation`, `rotation`; solid reuse |
| `pattern` | `input`, `count`, `step` | 2–64 translated copies including original; replication budget below |
| `circular_pattern` | `input`, `count`, `axis`, `angle_deg` | 2–64 rotated copies; signed angular step, including original |
| `hole` | `input`, `origin`, `axis`, `radius`, `depth` | Cylinder cut along explicit direction; must remove material |
| `import_step` | `content`, `sha256` | Nonempty embedded STEP text with matching SHA-256; optional explicit `solid_indices` subset, no fixed source byte cap |
| `assembly` | `parts` | 1–64 named instances of earlier solids or assemblies; optional acyclic rigid/articulated `mates`, `couplings`, `poses`, and source-keyed `bom` metadata |

`workplane` requires `origin`, `normal`, `x_direction`; directions must be nonzero
and perpendicular. A profile is `{"type":"rectangle","width":20,"height":10}`,
`{"type":"circle","radius":5}`, or `{"type":"polygon","points":[[0,0],[10,0],[0,5]]}`
with 3–128 points. Rectangle begins at local (0,0); circle is centered there.
An axis object has `origin` and `direction`. A rotation has `origin`, `axis`,
`angle_deg`; rotation precedes translation. Pattern results are compound solids,
not assemblies or fused unions. Boolean fusion is explicit. Imported STEP is an
opaque solid feature, not recovered source design intent. Saved content makes
imports independent of their original file path.

[SHELL_OFFSET_THICKEN.md](SHELL_OFFSET_THICKEN.md) defines signed material-side,
containment and connected-patch requirements. Face selectors use oriented
`normal`, optional center/area predicates and required expected cardinality;
area expressions declare `mm2`. [RICHER_MODELING.md](RICHER_MODELING.md) defines
extent, taper, path-station and sweep frame controls. Summary area/volume use
adaptive integration of the exact native surfaces, including rational swept
surfaces; triangulated volume is not substituted.
[SKETCH_OPERATIONS.md](SKETCH_OPERATIONS.md),
[AUTHORING_IMPORTS.md](AUTHORING_IMPORTS.md), [SHEET_METAL.md](SHEET_METAL.md)
and [SURFACES.md](SURFACES.md) define the additional bounds and explicit limits.

Circular patterns rotate each copy by `index * angle_deg` around the supplied
world axis. The signed step must have magnitude at least 0.00001 degrees, and
`abs(angle_deg) * (count - 1)` must be less than 360 degrees (with 1e-9 degree
boundary slack), preventing a duplicate full-turn endpoint. For a complete
four-copy ring use count 4 and step 90, not 120. Copies remain separate solids.

Patterns and assemblies replicate their inputs, and a pattern may take another
pattern as input. Each such feature has a per-feature replication budget,
checked from its inputs before any copy is built: at most 4,096 solids and
65,536 faces, counted as input solids/faces × `count` for a pattern and summed
over parts for an assembly. A 64 × 64 grid of single solids is within it; a
third nested 64-copy level is not. Exceeding it returns `limit_exceeded` with
`feature_id`, `solids`, `faces`, `solid_limit` and `face_limit`, and publishes
no revision. The budget does not bound Boolean work; job deadlines and memory
budgets still apply to every build.

An assembly part is `{id,input,placement?}`, with an earlier solid or assembly
input. Direct limits remain 64 parts per assembly and 256 declared parts per
document; nesting adds a maximum depth of eight, 1,024 leaf occurrences per
assembly and 4,096 expanded leaves summed across every assembly definition.
Query `assembly.parts` flattens leaves to occurrence paths such as `left/pin`;
`assembly.tree` preserves their preorder hierarchy and immediate owning
definition. Each path segment is a normal model identifier. Mates refer to
immediate children of their definition. See [ASSEMBLIES.md](ASSEMBLIES.md).
Placement has optional `translation`
and `rotation` with the transform convention above. A rigid mate is
`{id,type:"rigid",parent,child,parent_frame,child_frame,offset?,angle_deg?}`;
frames use the workplane fields in each source part's coordinates. Offset is in
the parent datum frame and rotation is about its +Z. An unmated root uses its
placement (identity when absent); a mated child must omit placement. Each child
has one parent at most, cycles fail, and parts/mates need not be ordered.
Assemblies permit 63 mates and the document permits 256 total assembly parts;
the replication budget above also bounds an assembly's total solids and faces.
Solid operations consuming assemblies are rejected;
edit source parts before assembling them. Full semantics and examples are in
[ASSEMBLIES.md](ASSEMBLIES.md).

Moving mates share the same parent/child datum frames. `revolute` requires
`angle_limits_deg: [minimum,maximum]`; `slider` requires `travel_limits_mm`;
`cylindrical` requires both. `angle_deg` and `travel_mm` default to zero and must
remain within their respective limits. A slider's optional `angle_deg` is fixed
alignment, not a moving coordinate. Limits and coordinates accept parameters
and expressions with degrees or mm units. Rotation and axial travel are about
and along the parent datum's +Z, after its local `offset` translation.

Assembly `couplings` are at most 126 `{id,source,target,ratio,offset?}` entries.
Source/target are `{mate_id,coordinate}`, with `coordinate: angle_deg|travel_mm`.
The target equals `source * ratio + offset`; the nonzero ratio is a numeric
coordinate multiplier (degrees or mm as named), and offset uses target units.
Each target has one driver, cannot also have an authored coordinate value, and
coupling cycles fail. Up to 64 named `poses` use `{id,values}` where each value is
`{mate_id,coordinate,value}`. Every independent coordinate must appear exactly
once; driven coordinates must be omitted. All named poses, including their
derived coupled coordinates, are validated against limits when the model is
validated. See `examples/articulated-arm.create.json`.

A sweep starts at its sketch origin with the initial path tangent perpendicular to
the sketch plane. Invalid/self-intersecting profiles and failed sweeps fail
explicitly. STEP readers normalize source units to document millimeters.
A hole whose cylinder misses its input, stops short of it, or only touches it
fails with `invalid_model` naming the hole (`feature_id`) and its input
(`source_feature_id`) plus `removed_volume_mm3` and `hole_volume_mm3`; it must
remove more than one millionth of the hole's own cylinder volume, so a small real
hole in a very large body is accepted.
Every STEP transfer root must transfer: a file where any root fails is rejected
with `kernel_failure` and `transferred_roots`/`total_roots` details, never
imported partially.

### Exact curve profiles and sweep paths

A sketch may use `profile: {type:"wire", segments:[...], holes:[[...], ...]}`.
Each ordered segment uses workplane-local 2D coordinates. `holes` is optional;
each entry is an explicit closed interior wire. Author winding does not decide
whether an interior is removed. Interiors must be strictly contained, disjoint,
non-nested, and separated from other boundaries by more than 1e-7 mm.

A sweep may use `path: {type:"wire", segments:[...]}` with world-coordinate 3D
points. Existing array-valued polyline paths remain supported. Segment forms:

| Type | Fields | Meaning |
|---|---|---|
| `line` | `start`, `end` | Exact straight segment |
| `arc` | `start`, `mid`, `end` | Exact circular arc through three distinct non-collinear points |
| `bezier` | `points` | 2–26 control poles; first and last are the endpoints |
| `spline` | `points` | Native interpolating B-spline through 2–64 distinct points |

Splines optionally set `periodic: true` with at least three points; closure is
implicit, so do not repeat the first point. Open splines may specify both
`start_tangent` and `end_tangent`, nonzero dimensionless vectors in the same
coordinate frame as the points. OCCT scales tangent magnitudes to the point
spacing; the vectors constrain direction. Endpoint tangents and periodicity
cannot be combined. All coordinates accept parameters and bounded expressions.

Wires permit 1–64 segments and profiles at most 16 interior wires. Segments must
connect in authored order within 1e-7 mm; profiles must also close. No automatic
closing segment, reordering, polygon approximation or repair is performed.
Zero-length/undefined-tangent geometry, crossings and self-interference fail
with feature context and, where available, `segment_index` or `hole_index`.

Curved profiles can be extruded, revolved, lofted and swept. Loft currently
requires one boundary per section: for a hollow loft, construct outer and inner
lofts separately and subtract explicitly. Interior loops are never discarded.
Curved sweeps require their initial tangent to align with either direction of
the sketch normal and may fail for excessive curvature or self-intersection.
See `examples/curved-plate.create.json` and `examples/curved-pipe.create.json`.
These checks (no-effect holes, partial STEP imports and the replication budget)
apply whenever a model is evaluated, including revisions committed by earlier
builds. A stored revision that relied on the old leniency is still preserved
byte for byte and readable with `cad_read`, but queries, views, exports and
drawings of it fail with the same feature-level error; `cad_apply` removing or
fixing the named feature, or `cad_restore` of an earlier revision, repairs it.

### External threads

```json
{"id":"stud","type":"external_thread","major_diameter":20,"pitch":2.5,
 "length":21,"origin":[0,0,17],"handedness":"right"}
```

This primitive makes one continuous helical B-rep solid: a 60-degree ridge swept
along a cylindrical-surface helix and fused to its cylindrical root core. It is
not a stack of rings or a polygonal substitute. Its dimensional fields accept
the usual millimeter scalars, parameters, and bounded expressions. Length is
measured along +Z from the base-center `origin` (default `[0,0,0]`).

The nominal profile has crest width `pitch/8`, pitch diameter
`major_diameter - 0.649519053 × pitch`, and external minor diameter
`major_diameter - 1.226869322 × pitch`. The root is flat. M20 coarse uses a
2.5 mm pitch, giving nominal pitch/minor diameters 18.3762/16.9328 mm; compare
[TR Fastenings' metric coarse dimensions](https://www.trfastenings.com/knowledge-base/thread-geometries/metric-coarse-standard).
This is a nominal geometry feature, without manufacturing allowance, a rounded
rolled-thread root, or certification of a 6g fit class.

Ends are clipped at the specified length. The free +Z tip has a 45-degree lead
chamfer whose axial length is the lesser of the radial thread depth and
`length/4`. The base remains full profile for attachment to another solid.
An explicit `fuse` joins it to a knob or shaft; a small intentional overlap is
supported. The example above overlaps a head ending at Z=18 by 1 mm and leaves
20 mm exposed, including the lead chamfer.

Bounds are `pitch >= 0.1 mm`, `major_diameter <= 200 mm`,
`3 <= major_diameter/pitch <= 100`, and `1 <= length/pitch <= 16`.
These bound sweep complexity and keep detail above kernel tolerances. Invalid
inputs or a failed/invalid sweep return a feature-level error without publishing
a revision. Handedness reverses the helical advance; arbitrary thread standards,
multiple starts, fit classes, and internal threads are not implemented.

## Persistent selectors and evaluated picks

```json
{"type":"geometric","feature_id":"base","curve_kind":"line","expected_count":4,
 "direction":{"vector":[0,0,1],"tolerance":0.000001}}
```

The selector's feature must equal the fillet input. Curve kinds are `line`,
`circle`, `ellipse`, `hyperbola`, `parabola`, `bezier`, `bspline`, `offset`, `other`.
`expected_count` is mandatory (1–10,000). Optional predicates are:

- `direction`: nonzero `vector`, angular `tolerance` in radians; unoriented parallel lines only.
- `center`: `point` and distance `tolerance` in mm, against edge center of mass.
- `length`: `value` and `tolerance` in mm.

Tolerances are ≥1e-9; direction tolerance is ≤π/2. Scalar parameter references and
expressions are accepted. A missing or ambiguous match reports candidates and
feature context; it never selects an arbitrary member or silently drops a fillet.

Evaluated picks instead contain `document_id`, `revision`, `evaluation_id`,
`feature_id`, `kind` (`face`/`edge`), and `entity_id`. IDs such as `edge-1` are valid
only within that stored evaluation. They are never persistent design references.
Rebuilding the same revision creates a new evaluation identity. Stored evaluation
metadata remains resolvable after restart; resolving a pick requires current HEAD
to still equal its revision. Metadata of superseded revisions may be deleted by
live-view retention (see LIVE_VIEWER.md); such picks are stale either way. Draft
picks, missing evaluations and identity
mismatches fail explicitly. Unique source geometry may receive a suggested edge
or face selector; ambiguous rules fail explicitly. Face selectors support shell
openings, thickening patches, planar face reuse and projection. Assembly picks
remain occurrence-qualified inspection references; edit their source features.

## STEP diagnosis and explicit extraction

`cad_inspect_step(path, expected_sha256?)` transfers the original STEP in a bounded
native worker without committing a document. It returns `source_sha256`, pinned
`kernel_version`, aggregate `valid`/`meshable`, `solid_count`, and every solid's
one-based `index`, exact bounds, volume, validity, meshability and errors. Invalid
B-reps report up to 64 native entity/status diagnostics per error, with explicit
truncation. Entity indices in errors qualify only that failed shape. Inspection
never heals, drops or replaces geometry. Source labels/history are not recovered.

`cad_import` optionally accepts `solid_indices` (1–4096 unique integers) with the
required `expected_sha256` from inspection. The saved `import_step` embeds the
complete original bytes and subset. It transfers every source root, then builds
only the explicitly selected solids; missing indices fail. Unselected invalid
solids are intentionally outside that import. Indices qualify only the exact
source SHA-256 and pinned kernel, never a later file or revision. Subset imports cannot claim unchanged `purchase` metadata for the whole artifact.
Full imports continue to reject any invalid solid or loose geometry. Both inspection and
import support `cad_job`; inspection needs no document ID.

## Tools

Document operations require `document_id`. External artifact review/show and
job/view management have separate document-free inputs. Revisions are positive
JSON-safe integers. See runtime schemas for exact closed field definitions.

| Tool | Arguments beyond document_id | Result |
|---|---|---|
| `cad_create` | `model`, optional `request_id` | Committed record + summary, revision 1 |
| `cad_read` | optional `revision` (defaults HEAD) | Committed editable record; no geometry build |
| `cad_capture_sketch` | No document ID; `format`, absolute `path`, `feature_id`, `workplane`, format-specific dimensions and optional source hash | Worker-validated portable text/SVG/DXF sketch, captured bytes/hash and contour/segment counts |
| `cad_import_sketch` | `expected_revision`, `format`, absolute `path`, `feature_id`, `workplane`, format-specific dimensions; optional source hash and request ID | Atomic appended sketch, compact committed/source identity and existing output summary; retrieve editable bytes with `cad_read` |
| `cad_apply` | `expected_revision`, `operations`, optional `request_id` | New committed record + summary |
| `cad_restore` | `expected_revision`, `source_revision`, optional `request_id` | Historical intent rebuilt as a new revision |
| `cad_inspect_step` | No `document_id`; `path`, optional `expected_sha256` | Source-qualified per-solid validity, meshability, bounds and BRep diagnostics; no document creation |
| `cad_import` | `path`, optional `request_id`, `expected_sha256`, `purchase`, `solid_indices` | New document with exact embedded STEP bytes; explicit subset requires the source hash and cannot carry `purchase`; see [sourced-part contract](PURCHASED_PARTS.md) |
| `cad_artifact` | No `document_id`; `action: review` with absolute `path`, raw `expected_sha256`, explicit `format`, `units`, optional `references`, `native_source`; or `action: verify` with `review_path`, review `expected_sha256` | Portable captured read-only review, source/review hashes, parsed summary and explicit representation limits; see [artifact contract](ARTIFACT_REVIEW.md) |
| `cad_artifact_show` | No `document_id`; `review_path`, review `expected_sha256`, optional `view_id` | Verify/reparse and display a frozen artifact in the MCP App; `{view_id,document_id:null,read_only:true,artifact,resource_uri}` |
| `cad_query` | `revision`, optional `kind`, `feature_id` | Summary, topology or mesh of that revision |
| `cad_measure` | `revision`, `evaluation_id`, `feature_id`, `query` | Exact source-pose pair distances/angles, clearance/interference, or native planar section curves/material caps; see [measurement](MEASUREMENTS.md) and [section](SECTIONS.md) contracts |
| `cad_export` | `revision`, `format` (`step`/`stl`/`3mf`), optional `feature_id`, 3MF-only `layout` | Artifact path, bytes, units and identity; 3MF also returns all plates and layout manifest |
| `cad_manufacture` | `revision`, optional `feature_id`, `options` | Complete native manufacturing package: editable source, unique leaf STEP/STL/drawings, saved assembly pose, BOM/purchasing data, explicit process assumptions and portable hash manifest; see [manufacturing contract](MANUFACTURING.md) |
| `cad_fabrication_review` | `revision`, `options`, optional `feature_id` | Hashed native JSON review with explicit process inputs, exact or sampled measurements, unknown unsupported checks and saved-pose clearance/interference; see [review contract](FABRICATION_REVIEW.md) |
| `cad_gcode_review` | `revision`, absolute plain `.gcode` `path`, `expected_sha256`, `options`, optional `feature_id` | Native stateful static review, unchanged G-code and portable hash ledger; explicit machine/material/initial assumptions and caller CAD association; see [G-code contract](GCODE_REVIEW.md) |
| `cad_printer_handoff` | `revision`, `action: plan` with absolute plain `.gcode` `path`, `expected_sha256`, explicit printer/profile/review `options`, optional `feature_id`; or `action: verify` with `plan_path`, `expected_sha256` | Portable offline handoff package and native re-verification; caller-declared CAD association, static findings and visible setup prerequisites; native upload/start unsupported; see [printer contract](PRINTER_HANDOFF.md) |
| `cad_slice` | `revision`, `action: plan` with explicit executable/profile hashes, `options`, optional `feature_id`; or `action: run` with `plan_path`, `expected_sha256` | Native bounded installed OrcaSlicer 2.4.2 workflow for one solid; reviewed plan, actual G-code/effective settings, static findings and portable manifest; no printer contact; see [slicing contract](SLICING.md) |
| `cad_robot_export` | `revision`, `robot: {format, joint_properties, inertials?}`, optional `feature_id` | URDF+SRDF or SDF 1.12 directory with STL meshes, SI joint coordinates, frame ledger and hash manifest; explicit physical inputs required; see [robot contract](ROBOT_EXPORT.md) |
| `cad_bom` | `revision`, optional assembly `feature_id` (defaults output) | Source-grouped BOM with instance quantities, JSON/CSV artifacts and manifest path |
| `cad_drawing` | `revision`, optional `drawing` recipe | Native PDF/SVG sheet, per-view DXF, aligned layouts, sections, measured dimensions, tolerances, optional BOM/balloons and saved recipe/manifest paths |
| `cad_view` | `revision`, optional `feature_id` | Offline HTML path, .view.json path, summary and evaluation identity |
| `cad_preview` | `expected_revision`, `operations`, optional `feature_id`, `kind` | Draft HTML/data artifacts (`kind: view`, default) or full draft mesh/topology (`kind: mesh`); no commit |
| `cad_resolve_selection` | Remaining evaluated pick fields | Measurements and available persistent selector |
| `cad_compare` | `from_revision`, `to_revision` | Parameter/feature changes and volume/area deltas |
| `cad_job` | Job action fields below; no document_id at top level | Durable job status/result |
| `cad_list` | No arguments | Up to 1,000 document IDs and committed HEAD revisions; `truncated` flag |
| `cad_open` | Optional `document_id`, `view_id` | Open an MCP App; `{view_id, document_id, resource_uri}` |
| `cad_show` | `document_id`, optional `view_id` | Retarget a workspace view without opening another app |
| `cad_context` | Optional `view_id` | Validated selection, camera, visibility, presentation, prompt and explicit stale state |
| `cad_viewer` | App-only actions below | Asynchronous view state, mesh chunks, context publication |

`cad_viewer` context accepts optional closed `presentation` settings for a unit
clipping plane and leaf explosion directions. Ready sync and `cad_context`
return current settings; omitted input retains them. These view transformations
preserve source geometry, measurements and topology identity. See
[PRESENTATION.md](PRESENTATION.md) and [SECTIONS.md](SECTIONS.md) for limits,
persistence and optional native material caps. Closed opaque RGB `appearance`
settings and saved review presets are defined in [APPEARANCE.md](APPEARANCE.md).
They affect review only and never source geometry or material properties.

`cad_viewer action:"sequence"` lists/saves/deletes native source-qualified
keyframes, persists speed/loop options and seeks a bounded joint/presentation
sample. Ready sync/context expose `sequences` and nullable `playback`, with
explicit unapplied/pending/displayed time state. The viewer provides a local
play/pause clock with one native evaluation in flight. No model code or physical
dynamics execute; source HEAD remains immutable. See [PLAYBACK.md](PLAYBACK.md)
for closed schemas, bounds, interpolation and stale-source retirement.

`cad_apply`/`cad_preview` accept 1–256 operations: `set_parameter(name,value)`,
`add_feature(feature)`, `replace_feature(id,feature)` (preserves ID),
`remove_feature(id)`, `set_output(feature_id)`,
`set_part_placement(assembly_id,part_id,placement)`,
`set_mate(assembly_id,mate)`, `remove_mate(assembly_id,mate_id)`,
`set_bom_item(assembly_id,item)`, `remove_bom_item(assembly_id,input)`,
`set_joint_value(assembly_id,mate_id,coordinate,value)`,
`set_coupling(assembly_id,coupling)`, `remove_coupling(assembly_id,coupling_id)`,
`set_pose(assembly_id,pose)`, `remove_pose(assembly_id,pose_id)`, and
`apply_pose(assembly_id,pose_id)`,
`set_component(id,source_document_id,source_revision,source_feature_id?,bindings?,discard_local_changes?)`,
`detach_component(id)`, and `remove_component(id)`.
`set_mate` upserts a complete mate by its ID; removal requires an existing mate.
Placement replaces the complete root placement. Detaching a child and choosing
its placement can be combined in either order. Validate the final batch graph.
A valid unchanged batch still creates a revision. Restore never rewinds HEAD.
An error raised while applying one operation carries `details.operation_index`
(zero-based). A feature added or replaced by a batch must be an object with a
string `id` when it is applied, otherwise the batch fails with `invalid_model`.

Coupling and pose setters upsert complete entries by ID; removals require an
existing entry. `apply_pose` copies the stored independent values, retaining
their scalar expressions. `set_joint_value` changes only a declared moving
coordinate, not a slider's fixed alignment. To make an authored coordinate
coupled, use `set_mate` to omit its own value and `set_coupling` in one batch.
The final document, including all stored poses, must remain valid.

`set_component` captures or updates an explicit source revision from this workspace.
Its dependency closure becomes local features rooted at `id`; the source feature
defaults to the captured revision's output. `bindings` maps used source parameters
to consumer scalars and validates against the final batch. Omitted bindings retain
existing mappings on update. Source changes never propagate automatically.
Updating a locally edited component fails with `component_modified` unless
`discard_local_changes: true` is explicit; use preview to inspect that replacement.
Detach keeps the local features/parameters; remove deletes them. Repair remaining
references in the same batch. Full ownership, portability and limits are in
[COMPONENTS.md](COMPONENTS.md).

`cad_import` has no fixed source-file byte cap. Embedded STEP content is excluded
from internal document, component, worker-input and durable job-result JSON byte
budgets; ordinary metadata and geometry limits still apply. Large imports use
`cad_job` with an appropriate `memory_mb` and timeout budget. The source remains
in memory during import, so actual memory use can exceed the file size.
MCP/CLI request envelopes still have a 1 MiB limit: pass the local file path to
`cad_import` instead of sending large STEP strings inline.

`cad_import` embeds the file's bytes unchanged in a JSON string, so the file must
be valid UTF-8 (ISO 10303-21 files are normally ASCII and encode other text with
`\X2\` escapes). Otherwise it fails with `invalid_argument` and
`details.byte_offset` of the first invalid byte; bytes are never transcoded,
because the saved SHA-256 identifies the exact content.

Optional `expected_sha256` verifies the raw file against a caller/catalog hash;
optional `purchase` binds supplier, part number and HTTP(S) source URL to those
bytes. Native import fills `purchase.artifact_sha256`; a supplied hash must match
or fail with `artifact_mismatch` before publication. Raw `import_step` features
with purchase require that same verified hash. Unchanged sources and rigid copies
derive purchasing identity into BOMs and packages; conflicting BOM purchase fails
validation. Source STEP bytes join manufacturing manifests as `source_artifact`.
See [sourced-part contract](PURCHASED_PARTS.md). Import performs no URL fetch.

`set_bom_item` replaces or adds the complete metadata entry keyed by `item.input`;
`remove_bom_item` requires an existing entry. Assembly `bom` accepts at most 64
unique used inputs with optional unique `item_number` (1–999), `part_number`
(printable ASCII, 0–64), `description` (0–120), `material` (0–64), and `purchase`.
Purchasing records require nonempty printable ASCII `supplier` (1–120), supplier
`part_number` (1–64) and HTTP(S) `source_url` (1–512), with optional lowercase
`artifact_sha256`. They record caller provenance without fetching or certifying
the source. CSV appends supplier, supplier part number, URL and artifact hash to
the previous seven columns, leaving absent values blank. Quantities
are derived from instances. Automatic numbers follow lexical input order while
skipping reserved explicit numbers; results sort by item number. `cad_bom` returns
revision identity, `bom: {assembly_id,items,total_quantity}`, `artifacts` and a
manifest `path`. Items contain `item_number`, `input`, `quantity`, `part_ids` and
supplied metadata. Nested BOMs roll up leaves and add `structure`; its optional
per-node `bom` retains the owner's local metadata and item number, including
metadata for subassemblies. Rolled-up numbers retain explicit root leaf numbers
and otherwise allocate fresh numbers. Conflicting descriptive metadata for a
shared leaf source fails explicitly. JSON/CSV exports preserve these rows and do not change HEAD;
CSV text cells that a spreadsheet would read as formulas get a leading `'`.
See [ASSEMBLIES.md](ASSEMBLIES.md).

Drawing `bom: true` adds a sheet table and JSON/CSV sidecars. Optional `balloons`
specify `view`, `part_id`, source-local `anchor` (3 mm scalars) and projected
`label` (2 mm scalars); item numbers are derived from the BOM. Exact boundary
and visibility checks reject missing, occluded or ambiguous anchors. Section
balloons are unsupported. Responses include `bom` and resolved `balloons` with
projected `anchor_mm`/`label_mm`; PDF/SVG and DXF `BALLOONS` geometry share those
values. Full limits, fit rules and regeneration are in [DRAWINGS.md](DRAWINGS.md).

Mutation `request_id` values are bound to tool + canonical arguments excluding
the ID. An identical retry returns the original result even after HEAD advances;
a different payload under that ID returns `request_conflict`. Receipts reside in
committed immutable revisions, so interruption after publication cannot duplicate
an edit. A per-document receipt index makes the lookup constant-time; it is a
hint that is verified against the named revision and rebuilt from the revisions
when absent (workspaces from older builds) or untrustworthy. IDs are scoped to a document for synchronous mutations and workspace-wide
for jobs. Without request_id, reread HEAD after an uncertain outcome.

### External artifact review

`cad_artifact` reviews original STEP/STL/3MF/GLB/DXF/URDF/SDF/SRDF bytes without
creating an editable document. Review admission requires an absolute regular
file path with no symlink parents, the actual raw lowercase SHA-256, a matching
explicit format and declared units. STEP/3MF use `units: "file"`; GLB and robot
descriptions use `"m"`; STL/DXF require `mm`, `cm`, `m`, `in`, `ft` or `um`.
All display geometry is normalized to millimeters. Explicit robot mesh references
supply contained relative URIs, matching absolute paths, raw hashes and units;
unlisted references and external/network fetching are unsupported.

The result's `sha256` identifies captured `review.json`; `source.sha256`
identifies the original file. The portable package retains original/reference
bytes and a relative size/hash manifest. `action: "verify"` uses the absolute
`review.json` path and its expected review hash, checks the complete ledger and
persisted representation, then reparses captured bytes. Verification does not
prove authenticity, geometry equivalence to a native source, physical readiness
or recovered editable history. STEP exact operations stay in bounded serial
kernel workers; meshes/curves/robot semantics retain their stated limitations.

Optional `native_source: {document_id,revision,feature_id}` is a caller-declared
association to a real historical record and feature. The source lock protects
resolution and publication; `source_record_sha256` is checked again before
showing the review. This association does not convert the artifact into an
editable native document or grant its display native face/edge selectors.
`cad_import` remains the explicit path for creating a new opaque STEP feature.

`cad_artifact_show` verifies/reparses the package and publishes a frozen live
view. Its MCP App resource is the same viewer used by `cad_open`; `view_id`
defaults to `main`. Showing a real native document with `cad_show` restores the
ordinary native workflow. See [ARTIFACT_REVIEW.md](ARTIFACT_REVIEW.md) for exact
format support, unsupported features, bounds, package lifetime and errors.

### Geometry, views and previews

Summary reports `valid`, `units`, volume, area, center of mass, bounds, and unique
solid/face/edge counts. `cad_query.kind` defaults `summary`; `topology` returns
face/edge geometric descriptors; `mesh` includes topology and tessellation from
the **same** evaluated shape. `feature_id` scopes the shape (defaults to output)
and is always included in query results, including summaries.
Explicit surface summaries use area-weighted centers, `solid_count: 0` and
`volume_mm3: 0`. Direct formed/flat sheet features additionally report
`sheet_metal` with caller K-factor, developed bend lines/allowances and distinct
formed/flat modeled volumes. Reports do not assume physical volume preservation.

Documents with component metadata also report `components`, with each tracked
`id`, pinned `source`, snapshot `sha256`, `modified`, and `changes` listing local
feature and parameter IDs. Changes to bound consumer parameter values retain the
binding and are not local source edits. This inventory describes the document
even when a geometry query is scoped to a feature.

Assembly summaries additionally include `assembly.parts` with `id`, source
`input`, a 16-number row-major world `transform`, `bounds_mm`, and `volume_mm3`,
plus `assembly.mates` with each rigid mate's `id`, `type`, `parent` and `child`.
Transforms multiply source point column vectors; translation is at indices 3,
7 and 11. Assembly measurements sum individual solids, including any overlap.

Version 1 topology has `faces` and `edges` with evaluation-local string IDs,
surface/curve types, centers, bounds, area or length, and applicable directions.
Provenance records available OCCT shape history for loft sections and other
operations. Pattern history includes a zero-based `instance_index` for each copy.
Assembly history and face/edge descriptors include the owning `part_id`; distinct
instances retain distinct topology even when coincident. Assembly edges omit
suggested fillet selectors; source features remain the geometry edit targets.
Source and result entity IDs in this history are evaluation-local evidence, not
persistent design references; `history_truncated` reports the 10,000-entry cap.
A mesh contains `positions`, `triangles` (zero-based vertex indices),
`triangle_faces` (one B-rep face ID per triangle) and edge polylines `{id,points}`.
Assembly mesh edges additionally carry `part_id`, and triangle part ownership
follows the corresponding face descriptor.
The mesh and edge mappings share the topology identity. The hard bounds are 10,000
combined faces/edges, 200,000 vertices, 200,000 triangles and 200,000 edge points.
Persisted evaluation metadata, worker results and artifacts are each bounded to
64 MiB. Embedded STEP and bounded captured sketch asset bytes in model-bearing
internal payloads are excluded from metadata byte budgets; ordinary model
metadata remains limited to 1 MiB. SVG/DXF sources are bounded to 4 MiB each;
decoded fonts to 8 MiB. Captured sources are hashed and never executed.
Transport request envelopes remain limited to 1 MiB. Exceeding a bound is explicit
`limit_exceeded`, not truncation of geometry.

View artifacts need only a browser. Orbit/zoom, select faces/edges, copy or save a
pick reference, and load another `.view.json` in the same viewer to review an
updated model. No HTTP endpoint or network connection is needed. Draft views
carry a visible draft label and cannot resolve committed edit references.

The viewer depth-tests faces and edges, rejects indistinguishable overlapping
picks, validates loaded mesh mappings/bounds, and clips off-screen edges before
sampling. Render work is bounded (4 million pixels, 50 million triangle pixel
tests, 4 million edge samples); a model exceeding this requires a feature-scoped
view. Browser artifacts accept coordinates up to ±1e12 mm. These are review
limits, not changes to the exact saved geometry.

STEP exports exact solids. Binary STL and native 3MF use 0.1 mm linear and
0.5 rad angular meshing tolerance. Print meshes weld within 1e-7 mm and must be
closed and consistently oriented; missing faces or open meshes fail explicitly.
Periodic face failures retry on temporary iso-parametric splits. Display patches
retain the original selectable face identity; print meshes may normalize the seam of a complete analytic cone/cylinder wall
on its original surface, coordinate adjoining boundaries and check exact area,
volume and bounds. Unsupported trims still fail explicitly. Saved exact solids and STEP
exports remain untouched by tessellation. Files are independent of service memory/source documents.
Assembly exports retain positioned solids as a compound; editable part IDs and
mate semantics remain in the saved JSON, without a promised exchange hierarchy.
Artifact destinations are service-generated beneath `exports`; arbitrary export
paths are not accepted. Re-export of a revision replaces its derived STEP/STL. `feature_id` can scope any
export to an existing feature. Native 3MF exports preserve each solid as a named
object and build item, in mm, with valid standard metadata. Print output is a
content-qualified directory with `layout.json` and one or more `plate-N.3mf`
files. The response's `path`/`bytes` identify the first plate; `plates` lists all
files with hashes and source IDs, and `layout_path` identifies the manifest.

For 3MF only, optional `layout` requires `bed_mm: [width, depth]`, with optional
`margin_mm` (default 8), `spacing_mm` (default 3), `allow_quarter_turn` (default
true). Packing uses conservative bounding rectangles, may rotate about Z by 90°,
places each part on Z=0, and creates up to 64 plates. It is deterministic but
makes no optimality, support, material, slicing or printer-readiness claim.

Alternatively `layout.placements` explicitly covers every source solid once with
`{source_id, plate, x_mm, y_mm, rotation_deg:[x,y,z]}`. Rotations apply world X,
then Y, then Z; x/y locate the rotated bounding-box minimum. Copy these five
fields from `layout.json` to reuse a layout after edits. Native assembly leaf IDs
survive dimensional edits; `solid-N` suffixes are revision-local and require
correspondence review after topology changes. Margins and pairwise rectangle
spacing are revalidated independently before publication. Geometry is never
omitted to fit a bed. Up to 4096 solids, two million mesh vertices/triangles,
32 MiB model XML per plate and 256 MiB per package are supported; worker budgets
still apply. No export changes the saved assembly pose.

### Live MCP App views

`cad_open` declares `_meta.ui.resourceUri = ui://agent-3d-cad/viewer.html`.
The resource is self-contained HTML, CSS and JavaScript embedded in the native
executable, served by `resources/read` as `text/html;profile=mcp-app`. It has no
external resource or connection origins. Clipboard permission is requested;
denied clipboard access falls back to selectable request text. Browser code uses
the MCP Apps 2026-01-26 `postMessage` handshake with its parent host. Native MCP
stdio retains protocol version 2025-11-25. There is no HTTP listener.

Open/show/context default `view_id` to `main`. IDs are **workspace-scoped**, not
implicitly conversation-scoped: independent chats should choose distinct IDs.
Open without a document retains the previous association or creates an empty
view. Show can initialize a view, but does not cause a host to mount an app;
call open once when a UI is needed. Show on the same failed document explicitly
retries its evaluation. Ordinary committed edits need no additional show call.

An artifact view admits only `sync`, `mesh` and `context`. Sync/context carry
`read_only: true`, null `document_id`, `revision` and `feature_id`, plus
`artifact: {review_sha256,source,summary}`. Context also has `head_revision: null`.
They expose no editable model; `hidden_part_ids`, `presets`, `annotations` and `sequences` are empty, and
`playback` is null. Measurement, section, preset, annotation, sequence and motion
actions fail as `read_only_artifact` before native document dispatch.

Artifact mesh transfer uses the same byte/chunk bounds below, but validates the
frozen displayed review instead of a document HEAD. Payload `kind: "artifact"`
contains `artifact_geometry`, format metadata and parser limitations, without
native topology. Artifact context selections are exactly
`{review_sha256,kind:"mesh_group"|"curve",entity_id}` or null. Labels such as
`artifact-1` and `curve-1` expire with that complete review hash; server
publication checks their membership and the current display under the view lock.
They cannot be passed to `cad_resolve_selection` as original face/edge picks.
Camera, prompt, visual clipping and default color remain available. Clipping
leaves cut surfaces open; no native caps or exact mesh measurements are implied.
Sync does not continuously reread original external files or portable packages.

The app uses `cad_viewer` with these closed actions:

- `annotation`: `view_id`, `evaluation_id`, `operation` (`list`, `add`, `update`,
  `delete`, `clear`), and operation-specific bounded plain text/native anchor
  inputs. Saves at most 32 notes of 512 UTF-8 bytes. Ready sync and context include
  independent `annotations` with their own document/revision/evaluation/feature,
  current/retired status and explicit evaluation lifetime. Native resolved
  inspection centers are review evidence, not stable design references. Source
  changes retire pins without rebinding historical text. See ANNOTATIONS.md.

- `section`: `view_id`, `evaluation_id`, optional section `query`. An object
  starts a native durable section job; omission polls; null clears/cancels it.
  The query must match current clipping and explosion. Ready sync/context expose
  its independent `section` metadata. Plane/placement or source changes retire
  caps; kept-side reversal, camera and visibility do not. The live panel renders
  native filled surfaces with holes and reports exact dimensions. Cap IDs are
  read-only result identifiers. See SECTIONS.md.
- `preset`: `view_id`, `evaluation_id`, `operation` (`list`, `save`, `apply`,
  `delete`), with `name` required except for list. Stores up to 16 current review
  views containing camera, presentation, visibility and appearance. Save/apply
  require committed geometry; stale evaluations and unknown owners fail. Apply
  clears picks and retires sections only when their plane/explosion changes.
  See APPEARANCE.md.
- `measure`: `view_id`, `evaluation_id`, optional `query`. A query object starts
  a native durable `cad_measure` job; omission polls its source-qualified result,
  and null clears/cancels it. Ready sync and context include the job ID and query.
  A new evaluation retires this metadata. See MEASUREMENTS.md for target and
  coverage limits. Draft or stale view references fail explicitly.
- `sync`: `view_id`, optional `known_evaluation_id`. Returns `state` (`empty`,
  `loading`, `ready`, `error`), document/revision, and `changed`. Ready state also
  includes evaluation/feature identity, summary, `presentation`, `appearance`,
  saved `presets`, qualified camera when available and `hidden_part_ids` (default `[]`), including when `changed` is false; the complete editable model
  is included when the caller's known evaluation differs. Sync starts or observes
  a durable mesh job and does not wait for kernel completion. Publication checks
  both the view generation and current HEAD under locks. An obsolete result never
  replaces the displayed current revision.
- `mesh`: `view_id`, `evaluation_id`, optional integer `offset` (default zero).
  Returns `{data, offset, next_offset, total_bytes}`. `data` contains at most
  128 KiB of an ASCII-escaped JSON evaluation (64 MiB maximum). Offsets count
  bytes, which equal JavaScript string offsets for this encoding. Concatenate
  chunks before parsing; `next_offset: null` means complete. Every chunk checks
  displayed evaluation and HEAD. A changed view/revision fails explicitly.
- `context`: `view_id`, `evaluation_id`, `selection` (a complete evaluated pick
  or null), optional `camera`, `prompt`, `hidden_part_ids` and `presentation`. Camera has finite `yaw`, `pitch`,
  `zoom`, and two-element `pan`; angles are radians, pan is in viewport fractions.
  Prompt is bounded to 8,192 UTF-8 bytes. Selection resolution and a final locked
  HEAD/evaluation recheck precede publication. Null clears selection. Omitted
  camera/prompt fields retain prior values only within the same evaluation.
- `motion_preview`: `view_id`, `evaluation_id`, optional `assembly_id`, and either `pose_id` or
  `values: [{mate_id,coordinate,value},...]`. Numeric values must specify every
  independent coordinate of the chosen definition exactly once. The target must
  be a moving assembly definition reachable from the displayed assembly; omitted
  `assembly_id` selects the displayed definition. Repeated occurrences share its
  values. A preview replaces that definition's draft values and retains drafts
  of other definitions (at most 256 combined edit operations). Validates the
  complete candidate, then schedules a bounded native mesh job for the whole
  displayed assembly; never commits HEAD. A new pose invalidates transfers and
  picks from the previous pose even when the base revision is unchanged.
- `motion_reset`: `view_id`, `evaluation_id`. Discards the preview and returns
  to saved geometry. Also accepts the evaluation that started the current preview
  so reset remains usable while its mesh is being built or transferred. An older
  view/document or changed HEAD still fails. Reset does not cancel an in-flight
  save; wait for its outcome. Obsolete preview jobs cannot publish into the view.
- `motion_save`: `view_id`, `evaluation_id`, optional `pose_id` and `assembly_id`. Requires a
  displayed draft. Schedules the existing `cad_apply` path with its base revision
  and all preview operations. Optional pose ID also upserts independent values
  for the selected definition; omitted `assembly_id` selects the most recently
  previewed definition. Other definitions' named presets remain unchanged.
  Successful publication creates one normal revision. Failed saves preserve
  HEAD. Mutations are never retried automatically after an uncertain response.

All motion actions return sync status; subsequent `sync` polls finish the job.
Sync includes `saving`, and ready evaluations include `draft`. Ready drafts and
`cad_context` also carry `preview_operations` (`set_joint_value` or `apply_pose`)
so the agent can interpret the displayed pose. Draft context requires null
selection; native selection resolution rejects draft picks. Camera and part
visibility persist across poses. A new committed HEAD retires any preview of the
previous revision. The embedded UI disables geometry selection and export during
a draft, and offers reset, explicit save and optional named-pose creation.

Assembly summaries include `mechanisms: [{assembly_id,occurrences,motion}]` for
every reachable moving definition. `occurrences` lists its paths in the displayed
assembly, using `""` for the displayed definition itself; `motion` uses local mate
IDs and lists degrees of freedom and named poses. The existing `assembly.motion`
still describes only the displayed definition. The UI offers a definition picker
and names every occurrence affected by its controls.

Frozen view data and pending read jobs are qualified by the native build.
Synchronizing after an upgrade regenerates summaries and mesh data even when
HEAD is unchanged. Saved draft intent, camera and visibility survive;
old evaluations are stale. An admitted save retains its request identity and
reconciles its outcome before the new build refreshes the view.

`hidden_part_ids` is a view-level array of at most 1,024 unique leaf occurrence
paths from the currently displayed assembly. UI subassembly actions expand to
those leaf paths. `[]` shows all parts; omitting the field retains
the current view state. Unknown IDs, duplicates, non-arrays, and a pick whose
resolved `geometry.part_id` is hidden fail with `invalid_argument`. Send null
selection when hiding the selected part. Visibility and selection are checked
together under the final document/view locks; a rejected or stale request
changes neither. Hiding affects presentation and picking, with the complete
mesh, saved model, exact measurements, exports and BOM unchanged.

Hidden IDs persist across service restart and same-document revisions. When a
new display publishes, IDs for removed parts are pruned; switching to a solid
output clears the array. Retargeting to a different document clears visibility,
and returning to the earlier document does not restore its former mask. A same-
document `cad_show` retry retains the array. The array is stored separately from
the evaluation-qualified selection context.

`cad_context` returns view/document identity, displayed or selected revision and
evaluation, current `head_revision`, `stale`, `selection`, `hidden_part_ids` and
`presentation`. Hidden IDs and presentation settings describe the current display even if a saved pick is
stale. Validated geometry
and a selector, when available, appear in `resolved_selection`. Saved camera,
prompt and timestamp are optional. Old context is retained with `stale: true`
until replaced; never interpret it as a reference to the new revision.

The UI verifies the full transfer identity and rechecks sync before displaying
it. Same-document changes preserve the camera and clear old picks; switching
documents clears the old mesh while loading. A sync `error` state is a successful
tool result (only `isError` marks a failed call): for a different document it
clears the old mesh, model, camera and hidden parts; for the same document it
keeps the last solid with picks disabled. Reopening restores the document's saved
camera and presentation; selection requires context matching the current evaluation. Assembly
part controls hide individual instances, isolate one instance, or show all.
Isolating uses the same hidden-ID array for the other displayed parts.

Read-only app polling treats `workspace_busy`, `queue_full` and a
`stale_selection` during transfer as temporary waits. It disables old selections
and retries a complete, revalidated transfer on the next poll while retaining
the last rendered solid and camera. Other failures remain visible. This recovery
does not retry context publication, modeling mutations or message delivery.

The viewer has no embedded chat composer. Face/edge selection and clearing
immediately publish validated native context; camera movement remains debounced.
Users enter their requests in the main chat. Agents read `cad_context` with the
view ID returned by `cad_open` and resolve a current native selection before
editing. Stale or draft references cannot become guessed targets. Selection
changes use
`ui/update-model-context` when supported; that optional acknowledgment does not
block native persistence or revision polling. The viewer does not post
`ui/message` requests. Legacy native `prompt` fields remain supported; no tool
or document contract changed. Parameter controls still use the shared validated
`cad_apply` path; selection alone does not mutate geometry.

The bridge accepts messages only from its parent window, pins the responding
origin, bounds pending requests with timeouts, and disposes them on teardown.
WebGL2/WebGL1 rendering and CPU ray picking use the exact evaluation mapping,
depth-tested occlusion and explicit overlap ambiguity. Edge polylines and face
triangulations are sampled independently within `mesh.linear_deflection_mm`, so
edge picking treats an edge sample up to twice that deflection behind the
occluding triangle as visible, divided by the cosine between that triangle's
normal and the view ray (at most 4×). GPU work is bounded to
four million framebuffer pixels; pick traversal has a two-million-work budget.
Native geometry/payload limits continue to apply. Tests exercise math and mocked
GPU lifecycle. Real Codex-host rendering, a human edge pick, selective edit,
same-view refresh, camera retention and stale-pick clearing were observed on
macOS arm64; host message routing used a user-submitted composer draft. Other
hosts and native platforms retain their own acceptance gates.

### Jobs

```json
{"action":"submit","request_id":"plate_edit_2","tool":"cad_apply",
 "arguments":{"document_id":"plate","expected_revision":1,"operations":[
   {"op":"set_parameter","name":"thickness","value":8}]},
 "budget":{"timeout_ms":30000,"memory_mb":2048}}
```

Use `{"action":"get","job_id":"plate_edit_2"}`, `cancel` with the same job_id,
or `{"action":"list"}`. Submit supports
inspect_step/artifact/create/apply/restore/import/query/measure/export/
manufacture/fabrication_review/gcode_review/printer_handoff/slice/robot_export/
preview/view/drawing/bom. `cad_artifact` review/verify jobs require no native
`document_id` at the top level or inside `arguments`; their typed success result
is the same portable read-only review returned by a direct call. An optional
`native_source` remains an explicitly qualified association. `cad_artifact_show`
is a viewer action, not a supported durable job tool. Cancellation checkpoints,
deadline/memory budgets, terminal errors and identical-request replay apply to
artifact jobs. They never create or advance a native HEAD. Review package
publication precedes durable job-result persistence, so an interruption can
leave a captured package without a successful job result. Re-verify its actual
path/hash when reconciling; job failure alone does not prove no package exists.
Manufacturing packages use the
same bounded workers, cancellation and committed-revision contract; see
[MANUFACTURING.md](MANUFACTURING.md). Drawing contracts, first-/third-angle and grid arrangements,
section hatching, angular references and degree results, explicit manufacturing
tolerances, view placement coordinates, references,
regeneration and export limits are specified in [DRAWINGS.md](DRAWINGS.md).
At most eight active jobs and four geometry workers are admitted
per workspace. Excess admission returns `queue_full`. Jobs persist after CLI/MCP
exit and can be inspected from a new process. List returns at most 1,000 records.
Submit validates `arguments` with the same closed field sets, identifiers and
revision numbers as a direct call, so malformed input fails immediately with
`invalid_argument` and records no job. `get`/`cancel` of an unknown job ID return
`not_found`.

Retention: terminal jobs (`succeeded`, `failed`, `cancelled`, `interrupted`,
or damaged records) are collected during submit admission once they were last
updated more than **7 days** ago, or when they fall beyond the newest **256**
terminal records. Queued, running and cancelling jobs, records whose coordinator
still holds its ownership lock, and the request ID being submitted are never
collected. A collected ID returns `not_found`; resubmitting a mutation with the
same request_id still replays its committed receipt rather than editing twice.

A damaged, unreadable or symlinked job record never makes `list` or `submit`
fail. It reads as a `failed` job with error `job_record_corrupt`, and counts
toward admission only while a live coordinator still owns it. Use a new
request_id instead of a damaged one.

Budgets: timeout 1–300,000 ms; memory at least 128 MiB, up to the native signed
32-bit MiB representation (2,147,483,647). There is no 4 GiB policy ceiling; choose
a memory budget appropriate for the machine. Defaults are 30 seconds/2,048 MiB.
Queue time counts toward the asynchronous deadline. Synchronous geometry uses
default budgets. Workers run via native process spawning, without model-supplied
code. Coordinators and workers execute the running service image itself, whatever
its file name (on Linux, `/proc/self/exe` after an in-place upgrade). A detached
coordinator holds no caller pipe; Windows children inherit only their NUL streams. Linux enforces address-space limits; Windows uses Job Objects; macOS checks
physical footprint every 10 ms (there can be sampling overshoot). Worker-local
watchdogs also bound work if a coordinator exits. Geometry is serial in each
worker; no OCCT object is shared between workers.

A drawing with two or more views that are not cached projects them in parallel:
hidden-line removal dominates its cost and is independent per view. The
coordinator runs one worker process per uncached view (an internal `projection`
request; the only process-to-process hand-off is a result file), then a final
worker renders the drawing from the supplied projections. Parallelism uses only
worker slots that are idle at that moment and never waits for one: the caller's
own slot runs the first view and the workspace-wide limit of four geometry workers
still holds, so with no idle slot the views run one at a time, with identical
results. Each worker has the request's full `memory_mb` budget (a drawing can use
up to four times it at once) and the remaining wall-time budget. Cancellation,
a deadline or any worker failure stops every worker, reports the failing view's
error, and publishes no cache entry; cache entries for the projected views are
published only after the whole drawing succeeds. Cache diagnostics add
`projection_workers`, the peak number of workers that ran at once.

Repeated operations automatically reuse a disposable `.cache` inside the
workspace. Exact feature keys include each feature's geometry intent, referenced
parameter values, upstream feature keys, native build/SDK fingerprints, kernel
and cache-format versions. An edit rebuilds affected dependencies; unchanged
features restore individually. Complete model snapshots remain a fast path.
Every declared feature is validated, including branches outside the output.
Unused parameters, assembly BOM/preset metadata and component source provenance
do not invalidate shapes; responses derive their metadata from current intent.
Drawing projections key the output feature's dependency closure and each view's
definition (orientation, section plane, hatch extraction, explode offsets, balloon
anchors) and hidden-line choice. Changing labels, dimensions, tolerances, layout
or export formats rerenders from those projections. Editing an unrelated branch
still validates that branch without reprojecting unchanged output. Evaluation
identities and revision checks are always fresh; caches never serve as editable
sources. See `DEPENDENCY_CACHE.md` for rebuild examples and developer diagnostics.

Entries carry SHA-256 checksums. Geometry snapshots contain every feature's exact
B-rep and provenance, with shapes validated when restored. Assembly snapshots
include independent compound children; placements and topology ownership are
rederived from saved intent and actual restored shapes. Private snapshot format
2 does not trust saved face/edge ownership indices. Snapshot B-rep data is capped
at 32 MiB and encoded cache entries at 64 MiB. Each worker stages at most 32 MiB
of encoded feature entries in addition to its existing model/view limits.
The shared cache keeps at
most 128 entries / 256 MiB, evicting oldest publications under a native lock.
Busy, missing, unwritable, symlinked, corrupt and oversize cache entries are
skipped and geometry is rebuilt as needed. The cache may be removed while the
service is stopped; documents and exports are unaffected. The first projection
of a complex threaded part can still require an explicit long-running job budget.
No additional MCP tools, public response fields, or cache-management API are added.

States are `queued`, `running`, `cancelling`, `succeeded`, `failed`, `cancelled`,
`interrupted`; progress is coarse phase progress rather than a predicted percent.
Terminal success contains `result`; failures contain structured `error`. Cancel
and mutation publication share the document lock: a commit already published is
reported as success; otherwise cancellation prevents publication. Reads remain
available while geometry runs. Publication rechecks expected_revision under the
writer lock, so concurrent builds cannot silently overwrite one another.

Duplicate submits with identical tool/arguments/budget return the same job.
A changed payload/budget with the same ID returns `request_conflict`. Recovery
checks abandoned nonterminal jobs after five seconds, uses a coordinator ownership
lock rather than a bare PID, and reports success when the job's `result.json` is
present and belongs to the job, or restores a successful committed mutation from
its receipt. A job interrupted before commit can be resubmitted with the same ID.
The coordinator publishes `result.json` before the state that reports success.
If any later step fails, it logs the failure to stderr and persists a `failed`
state with the error, unless a durable result already exists for recovery.
Failed/cancelled jobs remain terminal; use a new request_id for a deliberate retry.
Receipt recovery applies to committed source mutations, not offline artifact
packages. A printer package can publish before its job result is persisted; a
coordinator interruption or result-storage failure can therefore leave a package
without a successful job result. An interrupted retry (same ID) or deliberate
failed-job retry (new ID) can create another package. Inspect and re-verify the
actual package bytes before retrying; job failure alone does not establish that
no artifact exists. Printer planning has no hardware effects. See
[PRINTER_HANDOFF.md](PRINTER_HANDOFF.md).
This application job API does not advertise the MCP Tasks extension.

## Persistence and errors

```text
workspace/
  .lock
  .locks/<document_id>.lock
  documents/<document_id>/HEAD.json
  documents/<document_id>/revisions/<revision>.json
  documents/<document_id>/receipts/<sha256(request_id)>.json  # receipt index hint
  documents/<document_id>/receipts/coverage.json  # revision through which receipts are indexed
  evaluations/<evaluation_id>.json
  artifact_reviews/<review_sha256>/review.json
  artifact_reviews/<review_sha256>/manifest.json
  artifact_reviews/<review_sha256>/original.<extension>
  artifact_reviews/<review_sha256>/references/  # explicitly captured robot meshes
  exports/<document>-r<revision>.step
  exports/<document>-<evaluation>.html
  exports/<document>-<evaluation>.view.json
  jobs/.lock                        # job admission
  jobs/<request_id>/state.json      # small job record: state, error, result digest
  jobs/<request_id>/request.json    # immutable submitted tool arguments
  jobs/<request_id>/result.json     # result (≤64 MiB metadata, STEP bytes excluded), published before success
  jobs/<request_id>/.lock           # coordinator ownership
  jobs/<request_id>/cancel          # cancellation request
  views/<view_id>/state.json        # workspace-scoped association and context
  views/<view_id>/evaluations/      # frozen mesh JSON of the displayed evaluation only
  evaluations/.retention            # last superseded-metadata sweep (LIVE_VIEWER.md)
  .workers/                         # bounded worker slots and temporary files
  .cache/<content-key>.json          # disposable exact geometry / per-view projections
```

HEAD defines visibility; snapshots beyond it are uncommitted candidates. Document
records preserve schema version, units, kernel version, feature IDs and revision.
Job `state.json` (`schema_version: 2`) stays small: identity, tool, budget, state,
progress, timestamps, structured error and the SHA-256/size of `result.json`.
Admission and list read only these records, never results. `request.json` and
`result.json` repeat the job ID and request fingerprint; a result is trusted only
when it matches them and the recorded digest. Job directories written before this
layout (arguments and result embedded in `state.json`) remain readable, and a
terminal one is rewritten in the current layout the first time it is read.
POSIX writes fsync then rename then fsync the parent (macOS uses `F_FULLFSYNC`,
falling back to fsync where unsupported); Windows uses file flush and
write-through replacement. Atomic publication has an uncertain outcome if the OS
reports a durability error after rename; idempotent receipts let callers reconcile.

A commit durably writes its receipt index entry `{schema_version, request_id,
revision}` before the revision and HEAD, then replaces `coverage.json` without
fsync. Replay uses an entry only if that revision is at or below HEAD and records
the same request; damaged, forged or beyond-HEAD entries trigger a verified scan
of all revisions, and a missing entry scans only revisions above coverage. The
first commit by this build on an older document backfills the index. Deleting the
whole `receipts/` directory is safe; deleting single entries is not supported.

Writers take the workspace `.lock` shared and `.locks/<document_id>.lock`
exclusive. Mutations (admission and commit) and artifact publication (export,
BOM and drawing manifests) retry for up to 5 seconds with 2–50 ms backoff,
holding neither lock between attempts, and then fail with `workspace_busy` and
`details.waited_ms`. Viewer state, job admission and other short critical
sections fail fast with `workspace_busy` and are retried by their callers.

Use a trusted local workspace; managed-path symlinks are rejected. This is not a
hostile-user or network-filesystem sandbox. No HTTP/remote multi-tenant service is
exposed. Abrupt exit may leave temporary files; these are never documents.

Domain errors are `{"code":"...","message":"...","details":{...}}`. Important
codes include invalid_json, invalid_argument, invalid_model, limit_exceeded,
unsupported_schema, unsupported_feature, invalid_shape, kernel_failure,
selection_missing, selection_ambiguous, selection_count_mismatch, stale_selection,
draft_selection, already_exists, not_found, revision_conflict, request_conflict,
kernel_mismatch, artifact_invalid, artifact_mismatch, artifact_source_missing,
artifact_source_mismatch, read_only_artifact, workspace_busy, queue_full, job_timeout, job_cancelled,
job_interrupted, job_record_corrupt, worker_failed, memory_limit, export_failed,
slicer_failed, slicer_version_mismatch,
storage_error, internal_error.
Modeling errors identify their failed feature. Missing/ambiguous selectors include
candidate descriptors for repair. A failure never silently changes modeling intent.
The service maps unexpected library failures once, so MCP, CLI and jobs report the
same code: malformed JSON values are `invalid_argument`, filesystem failures are
`storage_error` and anything else is `internal_error`.

## MCP stdio

Baseline `2025-11-25`; initialize then notifications/initialized. `ping`, tools/list,
and tools/call are supported. Input is newline-delimited UTF-8 JSON, ≤1 MiB and
64 nesting levels; no JSON-RPC batches. String/integer request IDs are preserved.
Notifications have no reply, and tools/call notifications never execute tools.
LF or CRLF delimiters are accepted. Blank or whitespace-only lines are ignored, and
client JSON-RPC responses (objects with `result` or `error` but no `method`) are
dropped without a reply. Malformed JSON gets one `-32700` reply, an invalid request
or a line over 1 MiB gets one `-32600` reply; none ends the stream. A final
complete frame at EOF is accepted without a trailing newline.
All stdout lines are protocol JSON; diagnostics use stderr.

Tool replies contain JSON text and structuredContent; domain failures set isError.
The server advertises the MCP Apps extension and resources with no subscriptions.
`resources/list` exposes the single viewer resource; `resources/read` accepts
only its exact URI. Tools include the same UI metadata in CLI discovery.
`cad_viewer` has `_meta.ui.visibility: ["app"]`; the host filters model visibility
and app calls. The service still accepts the tool through CLI/stdio for tests and
other adapters. HTTP, sampling, streaming progress and MCP Tasks are not exposed.
Use cad_job for asynchronous application work.


## Desktop launcher and client configuration

The native CLI additionally accepts:

```text
agent-3d-cad viewer --workspace PATH [--document ID] [--view ID]
agent-3d-cad config --client claude|opencode|codex --workspace PATH
```

`viewer` validates/opens the workspace view (`main` by default) with `cad_open`,
then starts the bundled Tauri executable and returns `{launched, workspace,
view_id}`. `launched` confirms process creation, not a successful first render.
It requires the desktop archive; a core-only install returns
`viewer_not_installed`. OS launch errors return `viewer_launch_failed`.
`config` prints JSON for Claude/OpenCode or TOML for Codex using absolute paths;
it neither creates a workspace nor modifies client settings. `--help` is human
readable. `serve` stdout remains exclusively newline-delimited JSON.

The Tauri shell is an adapter to the same MCP stdio service. Its page can call
only library/show/context/viewer tools for its window’s view. Native export IPC
accepts a document, committed revision and one of `step`, `stl`, `3mf`, `pdf`, `svg`,
`dxf`; the shell uses `cad_job` and OS save dialogs. Workspace dialogs and recent
paths are local UI state. This adds no modeling tool or document schema, HTTP
endpoint, shell execution tool, or remote chat-send API. Agents connected to the
same workspace read the exact displayed picks with existing `cad_context` and
`cad_resolve_selection` contracts.
