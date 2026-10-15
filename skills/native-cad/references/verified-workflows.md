# Measured authoring workflows

These examples are executable tool requests. The native developer tests read
these exact JSON blocks through MCP and verify the geometry, parameter edits,
failed-edit rollback, STEP round trips and assembly occurrence identities.
They require only the installed native service for modeling.

## A plate that remains editable

This 80 × 50 mm plate has four vertical R5 corners and four Ø6 through holes,
10 mm from the unrounded rectangular edges. Thickness is 6 mm initially; this
example's tested design range is 3–12 mm. The four hole axes are at (10,10),
(70,10), (10,40) and (70,40). Its volume is
`(80*50 - (4-pi)*5*5 - 4*pi*3*3) * thickness` mm³; its center of mass is
(40,25,thickness/2) mm. Volume alone cannot verify the hole positions.

<!-- cad-example: plate -->
```json
{"tool":"cad_create","arguments":{"document_id":"skill_plate","request_id":"skill_plate_create","model":{"schema_version":1,"units":"mm","parameters":{"thickness":6},"features":[
{"id":"base","type":"box","size":[80,50,{"parameter":"thickness"}]},
{"id":"rounded","type":"fillet","input":"base","radius":5,"edges":{"type":"geometric","feature_id":"base","curve_kind":"line","expected_count":4,"direction":{"vector":[0,0,1],"tolerance":0.000001}}},
{"id":"hole_a","type":"hole","input":"rounded","origin":[10,10,0],"axis":[0,0,1],"radius":3,"depth":{"parameter":"thickness"}},
{"id":"hole_b","type":"hole","input":"hole_a","origin":[70,10,0],"axis":[0,0,1],"radius":3,"depth":{"parameter":"thickness"}},
{"id":"hole_c","type":"hole","input":"hole_b","origin":[10,40,0],"axis":[0,0,1],"radius":3,"depth":{"parameter":"thickness"}},
{"id":"plate","type":"hole","input":"hole_c","origin":[70,40,0],"axis":[0,0,1],"radius":3,"depth":{"parameter":"thickness"}}],"output":"plate"}}}
```

Read the saved source with `cad_read` before editing. At revision 1 this request
changes thickness to 12 mm. The receipt lists the changed parameter and its
dependent features. A retry with the same request ID returns that same receipt.

<!-- cad-example: plate-edit -->
```json
{"tool":"cad_apply","arguments":{"document_id":"skill_plate","expected_revision":1,"request_id":"skill_plate_thicker","operations":[{"op":"set_parameter","name":"thickness","value":12}]}}
```

For a real design, walk driving parameters through both ends of their intended
range and back to their defaults. Check measurements, placement and explicit
selection cardinalities at each step. Do not infer supported ranges from one
successful default build.

## Explicit workplane axes

The sketch's local Y is `normal × x_direction`. This XZ plane therefore maps
local (10,20) to world (10,0,20). Extruding a 2 × 4 mm rectangle centered
there by +3 mm along the normal gives center of mass (10,-1.5,20) and
volume 24 mm³.

<!-- cad-example: plane -->
```json
{"tool":"cad_create","arguments":{"document_id":"skill_plane","model":{"schema_version":1,"units":"mm","parameters":{},"features":[
{"id":"profile","type":"sketch","workplane":{"origin":[0,0,0],"normal":[0,-1,0],"x_direction":[1,0,0]},"profile":{"type":"polygon","points":[[9,18],[11,18],[11,22],[9,22]]}},
{"id":"body","type":"extrude","input":"profile","distance":3}],"output":"body"}}}
```

## Shared geometry and nested occurrences

There are four separate 2 × 3 × 4 mm blocks, totaling 96 mm³. Their lower X
bounds are 0,10,30,40 mm. Paths `left/a`, `left/b`, `right/a`, `right/b`
identify the four leaves. Editing `width` changes all four; moving `left`
changes only that group's placement. Outputs remain separate solids.

<!-- cad-example: assembly -->
```json
{"tool":"cad_create","arguments":{"document_id":"skill_assembly","model":{"schema_version":1,"units":"mm","parameters":{"width":2},"features":[
{"id":"block","type":"box","size":[{"parameter":"width"},3,4]},
{"id":"pair","type":"assembly","parts":[{"id":"a","input":"block"},{"id":"b","input":"block","placement":{"translation":[10,0,0]}}]},
{"id":"assembly","type":"assembly","parts":[{"id":"left","input":"pair"},{"id":"right","input":"pair","placement":{"translation":[30,0,0]}}]}],"output":"assembly"}}}
```

<!-- cad-example: assembly-move -->
```json
{"tool":"cad_apply","arguments":{"document_id":"skill_assembly","expected_revision":1,"operations":[{"op":"set_part_placement","assembly_id":"assembly","part_id":"left","placement":{"translation":[0,20,0]}}]}}
```

The declaration change list names `left/a` and `left/b`. Dependency impact lists
are conservative: an assembly change can mark other reachable leaves as
potentially affected. Use the measured assembly summary to establish which
world placements actually changed.

## Diagnose a failure without editing HEAD

Geometry errors can include `details.failure_package`. Call `cad_failure` with
`action: "get"` and its `failure_id` to inspect the saved error, tool arguments,
build and budget. `action: "replay"` runs captured geometry in a temporary
workspace and returns `reproduced`, `different_failure` or `succeeded`.
Use `cad_job` for longer replays. The diagnostic outcome does not commit the
failed edit or prove that the user's design has been fixed.
