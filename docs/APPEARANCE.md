# Appearance and saved review views

The viewer's Colors panel sets an opaque default model color and optional
colors for individual leaf occurrence paths. A part can return to the default
color, or Reset colors can restore all defaults. These settings affect source
surfaces and PNG captures. Exact section caps keep their distinct review color.
Colors do not change model documents, topology references, committed revisions,
placements, native measurements, manufacturing exports or material properties.

`cad_viewer`'s `context` action accepts an optional closed `appearance` object,
separate from the existing `presentation` object:

```json
{"default_color":[0.66,0.75,0.80],"parts":[
  {"part_id":"left/housing","color":[0.9,0.2,0.1]}]}
```

Both `default_color` and `parts` are required when appearance is supplied.
Each RGB vector contains exactly three finite numbers in [0,1]. The default
is `{"default_color":[0.66,0.75,0.80],"parts":[]}`. Up to 1,024 unique overrides
may name current assembly leaves; hierarchy groups and absent leaves fail.
A single solid can use the default color and cannot have leaf overrides.
Fields are closed; alpha, materials, textures and shader/code inputs are not
part of this contract. Omitted appearance retains the current setting.

Ready sync and `cad_context` return current appearance and saved presets.
Revision refresh keeps the default color and prunes overrides for removed
leaves from the current view and every stored preset. It also prunes removed
hidden IDs and exploded directions. Retargeting a view to another document
resets appearance and clears its presets. Reopening the same persisted view
restores its settings. Current writes with unknown owners fail rather than
silently pruning an invalid request.

## Native saved review views

The Saved views panel saves, applies, deletes and refreshes native presets.
Each of up to 16 entries contains a plain name, camera, presentation, appearance
and hidden leaf IDs. Names use 1–64 UTF-8 bytes without control characters or
all-whitespace content. Saving an existing name replaces that entry.
Presets belong to the current persisted view/document, rather than the source
model or a committed revision. They do not capture geometry selections or
section job results.

```json
{"action":"preset","view_id":"main","evaluation_id":"eval_current",
 "operation":"save","name":"Exploded overview"}
```

`operation` is `list`, `save`, `apply` or `delete`; all except `list` require
`name`. `save` captures the native view's current persisted settings, using the
default isometric camera when no camera has been stored. `apply` restores the
entry atomically and clears the old pick. `delete` removes the named entry.
Responses use the `cad_context` shape, including current `appearance` and
`presets`. Missing names fail explicitly. Save and apply require committed
geometry; list and delete remain available for a qualified motion preview.
All operations require the current evaluation.

The app persists its current snapshot before saving or applying a preset and
serializes preset operations with native context writes. Optional host context
acknowledgments do not block admission or source polling. Delayed responses
cannot overwrite later camera, color, visibility or presentation edits, or a
new source evaluation. An uncertain mutation is never automatically repeated;
read-only sync reconciles native state. Failed color writes keep a visibly
unsaved local choice for an explicit retry. Agent request snapshots include
appearance and become invalid after camera or color changes.

Applying colors, visibility, camera or the opposite kept side retains a
qualified native section for the same plane and exploded placement. Applying
a different plane/explosion retires and cancels its section. A preset never
resurrects a retired cap report. Source geometry references remain qualified
to their original document/revision/evaluation. See [SECTIONS.md](SECTIONS.md).

The viewport normalizes camera yaw and bounds pitch, zoom and pan for rendering.
Presets created from the viewer capture the actual normalized camera. Broader
native camera inputs remain subject to those viewport limits when displayed.

## PNG captures

Save PNG image captures the current WebGL frame, including visible colors,
hidden parts, clipping, exploded placement and any current qualified section.
The app requests a local browser download named for the document and revision;
the host may require a save location or restrict downloads. The PNG data URL
is bounded to 8 MiB. No upload or message is sent by this button. The removed Quick Edit composer
and its image-attachment option are no longer viewer controls.

Native behavior, controller race tests and mocked WebGL verification do not
establish rendered host readiness. HANDOFF records actual test and browser
evidence. Annotations remain separate work. Declarative native timed review is defined in
[PLAYBACK.md](PLAYBACK.md), with its own source pins and paused time metadata.
