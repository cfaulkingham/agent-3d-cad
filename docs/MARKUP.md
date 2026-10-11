# Viewport markup

The viewer's **Draw on model** panel supports freehand pen, line, arrow,
rectangle, ellipse and plain text. Pick a color and line width, draw directly
over the viewport, and use Undo, Redo or Clear. **Navigate** returns ordinary
camera and selection input. Drawing uses pointer capture for mouse, pen and
touch; cancelled gestures never create saved marks. Escape cancels a gesture
and leaves drawing mode. Ctrl/Cmd+Z and Shift+Ctrl/Cmd+Z operate drawing history
when the markup canvas has focus.

Markup is review evidence, not an editable sketch feature, a dimension, or a
surface/edge reference. Screen coordinates are normalized to the recorded
viewport. They describe the original frame only; the service never turns them
into geometric selection references or executes text content.

## Qualification and persistence

Every scene records its view, document/revision/evaluation/output feature (or
read-only artifact review hash), displayed draft identity, camera, visibility,
clipping/explosion, appearance, display settings, imported pose, current section
descriptor, review notes and viewport dimensions. Adaptive tessellation does not
change the source binding. Drawing binds to the current source/frame and its
displayed components; it may begin while later components are arriving. Capture
waits for the current requested component geometry and textures. A markup drawn
over an approximate or partially loaded view does not certify full-model detail.

The scene is saved as native per-view metadata through the existing app-only
`cad_viewer` context action. It survives native service and viewer restart and
does not modify the source document, revision history, STEP or STL. A different
document/artifact target starts with empty view metadata. Changes within the
same source view retain the original scene and report it as `retired`.

A camera, source evaluation, pose, visibility, section, display-setting, or
viewport-aspect change hides the old overlay. The panel explicitly identifies
the historical sketch. Restoring the exact original frame restores its meaning;
proportional resizing preserves normalized positions. Otherwise, clear the
historical scene before drawing a new one. Historical text/coordinates remain
available to agents in `cad_context`, with their original qualification. A new
evaluation never silently inherits old marks, even at the same committed
revision.

The native writer compares a changed scene against the authoritative source and
display frame while holding the existing publication locks. Mismatches fail
with `stale_markup` without changing either source or saved view state.
Republishing an unchanged historical scene preserves it without rebinding.
Source mutations use the existing `expected_revision` rules independently.

## Images and agent context

Save PNG and `cad_screenshot` composite the same visible markup over the actual
WebGL frame, after existing artifact overlays, review pins and point rulers.
The screenshot fingerprint includes persisted markup and viewport dimensions;
changed markup invalidates an admitted native capture. Agent/native captures
reject unfinished gestures and unsaved markup/settings. Local **Save PNG**
captures the qualified current visible frame without requiring native settings
persistence. Both paths check source/frame versions after asynchronous waits.
Retired overlays are absent from the current frame's PNG.

The host receives bounded structured markup through the existing model-context
channel where supported. The user makes their request in the main host chat.
**Send image to chat** appears only when the host explicitly advertises image
messages; it saves the original captured frame's context before sending the
composed PNG and that same qualified context after the user clicks it. Any
source/frame change between capture and delivery rejects the send. No embedded
request composer is used. Image delivery is never automatically
retried after uncertain acknowledgment. Existing PNG/host transfer limits apply.

## Data and limits

`cad_viewer` context accepts optional `markup` (scene or null) and `viewport`
(two finite CSS dimensions between 1 and 10,000). Omitting them preserves older
clients' saved metadata. `cad_context` and ready sync expose `markup`:

```json
{"status":"current","coordinate_space":"viewport_normalized",
 "anchor_lifetime":"source_and_frame","item_count":1,
 "scene":{"schema_version":1,"binding":{
   "source":{"view_id":"review","document_id":"plate","revision":1,
     "evaluation_id":"eval_plate","feature_id":"body",
     "review_sha256":null,"draft":false},
   "frame":{"camera":{"yaw":0,"pitch":0,"zoom":1,"pan":[0,0]},
     "hidden_part_ids":[],"presentation":null,"appearance":null,
     "render":null,"artifact_pose":null,"section":null,"annotations":[]},
   "viewport":[640,480]},
   "items":[{"id":"mark_1","kind":"arrow","color":"#e63946",
     "width":3,"points":[[0.1,0.2],[0.4,0.6]]}]}}
```

The example illustrates the closed shape. A live scene must use the actual
native settings; placeholder null presentation/appearance/render values do not
qualify against a populated live frame. `status` is `current`, `retired` or
`empty`. The current viewport aspect is persisted separately in context so
agent context also retires a resized scene.

Limits are 256 items, 8,192 total points, 2,048 points per freehand stroke,
512 KiB scene data, 64 KiB frame data, and 32 local undo states. Shapes have two
points; text has one point plus `text` and `font_size`. Text permits 1–512 UTF-8
bytes and at most eight lines, with no control characters except tab/newline.
Widths are 1–16 CSS pixels at the recorded viewport; text size is 10–48 pixels.
Coordinates are finite in [0,1]. IDs are unique and follow model identifier
syntax. Colors are six-digit hexadecimal values. Unknown fields, duplicate IDs,
nonfinite/out-of-range coordinates, oversized scenes and invalid sources fail
explicitly. Undo/redo is local to an open viewer; saved scene persistence does
not imply durable undo history.

## Verification

`markup` exercises native validation and Service publication/reopen, stale
source/frame rejection, screenshot invalidation, and unchanged source history.
`markup_ui` exercises actual controller pointer lifecycle, all drawing tools,
history bounds, text/point limits, camera/aspect retirement and canvas paint
operations. The optional developer test runs real Chromium against the complete
bundled app and native MCP:

```sh
AGENTCAD_HARNESS_VIEWER=embedded node tests/browser_markup_mcp.cjs /absolute/path/to/agent-3d-cad /absolute/path/to/chrome /absolute/path/to/evidence
```

Embedded mode reads the compiled viewer through the native MCP resource. Evidence
records executable and viewer hashes. Omitting the environment option uses current
workspace viewer files for development. The harness grants image-message capability locally and records messages;
it never sends to a real host chat. Chromium software WebGL evidence does not
establish desktop host installation or hardware GPU acceptance. Executed results
and remaining gates are recorded in `HANDOFF.md`.
