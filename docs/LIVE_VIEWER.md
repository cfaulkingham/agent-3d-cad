# Live CAD viewer preview

The viewer runs as an embedded MCP App or a standalone Tauri desktop window
for the core create–view–select–edit loop. The WebGL canvas fills the whole viewer and
the controls float over it: a project pill (models menu, revision, connection), a
Scene card (features, parameters, parts), a dock of inspection tools, an Export
menu, an orientation cube and a compact selection indicator. Requests are entered
in the main host chat; agents retrieve the selected geometry through `cad_context`. Committed
edits appear automatically, retaining the camera.
Selections name an exact evaluated revision; updated geometry clears old picks.

This is an implementation preview. Native integration, real MCP SDK, revision
races and renderer mathematics are tested. The actual Codex MCP App on macOS
arm64 has rendered a plate, accepted a human edge pick, and followed an agent's
selective fillet to revision 2 in the same viewer, retaining the camera and
clearing the old pick. An earlier UI also demonstrated handing a request to
the host's composer. That embedded composer has since been removed; current
shared-context interaction and host acceptance evidence are in HANDOFF.
The Tauri window has separately demonstrated face/edge picking, CLI context
read-back, a selective fillet refresh and a native STEP Save dialog on macOS
arm64. Tauri Windows/Linux and remaining architecture runs remain unverified. The offline
`cad_view` artifact is still available.

## Connect the native service

Build or unpack a native preview, then add this stdio server in your host's MCP
settings with absolute paths appropriate to that installation:

```json
{
  "mcpServers": {
    "agent-3d-cad": {
      "command": "/absolute/path/to/bundle/bin/agent-3d-cad",
      "args": ["serve", "--workspace", "/absolute/path/to/cad-workspace"]
    }
  }
}
```

Use `agent-3d-cad.exe` on Windows once that build has passed native validation.
The workspace stores editable documents and view context. Each independent chat
should use a distinct `view_id`; IDs are shared by connections to one workspace.
No host configuration is overwritten automatically. The app is delivered through
the MCP host rather than a web server: there is no browser URL or listening port.
All JavaScript/CSS is embedded in the executable. No Python, Node or compiler is
needed in the installed product.

The bundle contains `share/agent-3d-cad/skills/native-cad/SKILL.md`. Install that
skill using your host's normal local-skill mechanism, or point the agent to it.
It explains document authoring, current selection handling and viewer reuse.
The existing connected text-to-cad plugin is independent and is not modified.

## Standalone window and connected chats

Install the desktop archive, then run:

```sh
agent-3d-cad viewer --workspace "/absolute/path/to/cad-workspace" --view main
```

The Tauri window starts its own stdio connection to the native service. It has no
HTTP listener and opens no browser tab. The agent’s MCP connection or CLI calls
must use the **same workspace and view ID**. Workspace paths are shown in the
Models menu (the top-left menu button). Use **Open workspace**, **Recent workspaces** and project search to
reopen earlier projects; their editable documents stay outside the app install.
Opening a workspace lists its saved models; choose one to display it.

Select a face or edge, then tell the connected agent, for example:

> Read `cad_context` for view `main` and round the selected edge to 1 mm.

Picks are saved automatically with the document, revision, evaluation, feature
and entity identity. A separate agent connection can read and resolve them; it
does not need to inspect a screenshot or guess an edge number. Select the
geometry, return to your main chat and enter the request there. Agents read the
current selection using this view ID; the viewer has no separate composer.
Separate chats should use distinct view IDs and pass that ID to `cad_context`
and `cad_show`; `main` is the default. Old picks explicitly become stale after
geometry changes, and the refreshed view clears them.

**Export** uses the displayed committed revision and an asynchronous native job.
STEP/STL save through the OS file dialog; PDF/SVG create a default A4 drawing
with standard views. DXF writes the four 1:1 view files into a new folder under
the chosen directory. The agent can customize dimensions, sections and layouts
through `cad_drawing`. Cancelling a Save dialog leaves the generated export in
the workspace. Exports include the complete model even when parts are hidden.

## Use the live loop

Ask the agent to create or reopen a design. For a concrete first session:

1. Create the simple plate from `examples/live-plate.create.json` using `cad_create`.
2. Call `cad_open` with `{ "document_id": "live_plate", "view_id": "plate_review" }`.
   The host should render the app; saved models appear in the Models menu.
3. Click a visible edge (there is no Faces/Edges switch), return to your main
   chat and type “Round this edge to 1 mm.” The viewer saves the qualified pick
   immediately, without sending a chat message.
4. The agent reads `cad_context` with the view ID returned by `cad_open`, checks
   that the pick is current, reads the document and resolves the pick with `cad_resolve_selection`,
   then uses its selector in a fillet and commits with `expected_revision`.
5. The same viewer follows the new revision automatically. It retains orbit,
   zoom and pan, and clears the obsolete selected edge.

The model library switches documents in this view. Design Details lists feature
names, types and the model output without raw source JSON. Components and
assemblies expand to show pinned source identity, part IDs, source features and
parent mate relationships; selected geometry identifies its owning part.
Placement and mate edits use the shared CAD tools. Assembly part controls
hide or show individual instances, isolate one part, and show all parts again.
Hidden parts are excluded from rendering and picking; hiding a selected part
clears the pick. These controls change this view's presentation, while the saved
model, exact measurements, exports and bill of materials retain every part.

Visibility is saved per `view_id` as `hidden_part_ids`, available to the agent in
`cad_context` and every ready `cad_viewer` sync response. It survives restarting
the service and follows same-document revisions: surviving part IDs retain
their visibility, and removed IDs are pruned when the new mesh is displayed.
Switching to a different document clears the mask. A stale saved selection still
reports `stale: true`; its old context cannot overwrite the current hidden IDs.
Selecting a hidden part or submitting visibility for an obsolete evaluation
fails without changing the saved view state. All visibility edits require the
current displayed evaluation, so a view that is loading or stale must synchronize
before changing its mask.

## Editing parameters

The Parameters card has a slider and a numeric field for every saved numeric
parameter. Release a slider, press Enter in a number field, or leave the field to
rebuild and save one revision through a bounded native `cad_apply` job. There is
one parameter mutation in flight, qualified by the displayed document and
`expected_revision`. Invalid geometry preserves HEAD and restores the saved
value; revision conflicts and uncertain replies are explicit and never trigger
an automatic mutation retry. Retargeting the viewer discards obsolete UI results.
Draft poses and read-only artifacts cannot use parameter edits.

Slider ranges are convenient local windows around the saved value, not model
constraints. Numeric fields accept the full finite ±1,000,000 scalar range; the
native model and kernel validate the resulting design. Values normally display up to three
decimal places, retain very small nonzero values, and expose the exact saved value
while the field has focus.

**Compare with** shows parameter differences against one of the last 50 saved
revisions. This compares editable values, without a geometry overlay. A changed
document or revision clears the comparison.

## Selecting

One cursor selects faces and edges; there is no mode switch. The entity under the
cursor highlights as you move: a face takes a soft blue wash, an edge a thicker
line. A visible edge within 6 px wins; if exactly one is nearest it is chosen,
otherwise the face under the cursor is. Click selects what is highlighted; clicking
empty space or pressing Esc clears it. Overlapping faces select nothing and say so.
In a read-only artifact review the same rule picks a curve, else a mesh group.
A compact indicator at the top shows the selection, a native measurement and
**Ask in chat**, with a button to clear it. Faces are inspectable; the current selective-filleting
operation uses edges. Selection references are unchanged: `cad_context` and
`cad_resolve_selection` see the same face or edge identity as before.

Hover repeats the click hit test on each pointer move. If a pick takes longer than
8 ms (very large models) hover backs off for four times that long and then follows
the latest pointer position; clicks always resolve.

## Navigation

| Input | Action |
|---|---|
| Left-drag | Orbit (a click that moves under 3 px still selects) |
| Right-drag | Orbit |
| Middle-drag, or Shift+drag with any button | Pan |
| Wheel or pinch | Zoom toward the cursor |
| Double-click an entity, or Space over one | Frame it |
| Double-click empty space | Fit the model |

Right-drag orbits (it used to pan); pan with the middle button or Shift.
The bottom toolbar provides **Iso**, **Front**, **Top** and **Right** views.
Enable **Orientation cube** from the Models menu for the cube near the lower-right
corner; it follows the camera. Click a face for that
standard view (Top, Front, Right, Left, Back, Bottom); click near a face's border
for an edge or corner view; drag the cube to orbit; double-click it for the default
isometric view. **Fit** frames the whole model. Fit, reset and framing centre the
model in the area the floating controls leave uncovered. Camera moves animate over
about 250 ms and are saved once at the end; `prefers-reduced-motion` makes them
instant. With the canvas focused: Home or 0 fits, digits 1–7 choose iso, front,
back, top, bottom, right, left; Esc clears the selection; arrows orbit,
Shift+arrows pan, +/- zoom.

## Layout and theme

The stage uses a light gray background, cool blue solid shading and translucent
floating cards. The viewer follows the host's light/dark theme, otherwise the
operating system's, and switches live. The bottom toolbar toggles the ground grid,
XYZ direction guide and exact edge outlines, opens measurement, and fits the model.
Grid and axes use the same orthographic camera as the model and appear in saved PNGs.
They are WebGL2 backdrop decorations, not selectable geometry or measurements;
WebGL1 retains its solid background fallback. Edge outlines start hidden; hover,
selection and picking still work. The XYZ guide shows world directions near the
model, rather than asserting that its anchor is the document origin.

| Width | Behavior |
|---|---|
| 900 px and up | Parts and available Parameters cards open |
| 560–899 px | Cards reopen from **Parts** and **Parameters** buttons |
| Under 560 px | One card opens at a time; the toolbar and compact selection indicator fit at 360 px |

**Assembled** restores all assembly leaves; each part tab isolates its occurrence.
Large assemblies use wrapped tabs with a bounded scroll area. **Design details**
retains the feature tree and detailed visibility controls. Card arrows collapse
contents; the Parameters close button leaves a reopen button. Resizing or opening
and closing Parameters recenters the usable viewport while preserving orbit and zoom.

The Models menu contains the library and optional orientation cube. The bottom
toolbar's **Export** dropdown offers STEP, STL, PDF, SVG and DXF exports of the
saved revision, plus PNG capture of the current view. It stays visible in compact
views while the camera and inspection controls can scroll horizontally.
Clicking geometry reveals the compact selection indicator. The bottom ellipsis
opens inspection tools (Visual inspection, Colors, Review notes, Saved views,
Exact section, Face and part clearance, Sequences, Motion and Source). An icon appears only while
its panel applies. Esc, an outside click or the close button dismisses the popover;
the dock supports Up/Down arrows.

Select geometry in the viewer and make requests in the main chat. Selection and
clearing publish native context immediately; camera movement is debounced. Agents
retrieve the current selection through `cad_context` with the retained view ID.
Optional host model context is also published when supported; its acknowledgment
does not block native persistence or revision polling. No chat message is posted
by selecting geometry. Save a PNG from Export when a separate image is useful.
Failed geometry preserves the last committed design. For a candidate that must
not commit, the agent can use the separate offline `cad_preview` workflow.

Read-only polling retries temporary lock contention, queue saturation and an
evaluation superseded during transfer. While waiting, the last rendered solid
remains visible and old picks are disabled. Persistent failures stay explicit.
Polling recovery does not retry context writes or modeling mutations.

## Clipping and exploded inspection

Visual inspection enables uncapped clipping, axis/offset controls, kept-side
reversal and exploded leaf separation. Settings persist per view and appear in
agent context. Picking follows the displayed parts while exact measurements,
references and saved source retain their original coordinates. See
[PRESENTATION.md](PRESENTATION.md) for the contract and limits.

## Measurements and exact source checks

The bottom-toolbar ruler measures two picked points, A then B, directly on the
model. It shows their 3D distance and a camera-tracked line without opening a
panel. Nearby visible edges attract the pick, even with outlines hidden. Click
an A/B marker to replace that endpoint or another location to start a new pair.
Escape/the ruler button exits; re-enabling restores the current points. Picks
use displayed tessellation and polylines, so their accuracy is approximate.
Points remain while orbiting/zooming and retire when source or presentation
changes. They are local viewer state and also work for read-only artifacts.

The separate Face and part clearance inspection tool chooses two leaf parts
or uses current face/edge picks for A
and B. Pair or assembly checks run in native jobs and show saved-pose distances,
closest points and material overlap. Clipping, explosion and hiding do not alter
the source measurements. Reopening restores the qualified result; a revision
change retires it. See [MEASUREMENTS.md](MEASUREMENTS.md).

The independent Exact section panel calculates a native planar cut for the
current clipping plane and exploded placement. Filled surfaces retain actual
bores; native curves, point/curve tangencies, exact area and boundary length
have explicit feature/all-leaf/subset coverage. Hiding changes the displayed
caps while preserving the report's scope. Kept-side reversal reuses the same
section; plane/placement/source changes retire it. Section picks return review
feedback, never original topology references. Reopening restores its qualified
job, and shared agent context carries that identity. See [SECTIONS.md](SECTIONS.md).

## Mechanism motion

Articulated assemblies expose a Motion panel with independent joint controls,
read-only driven coordinates, named poses, Reset, and Save pose. Editing a
coordinate asks a native worker for draft geometry. The saved revision stays
unchanged until Save; an optional name saves the independent values as a preset
in that same atomic revision. Reset restores the saved geometry. A concurrent
external revision retires the draft and refreshes to the new HEAD.

Draft context carries `draft: true` and `preview_operations` so the agent can
identify the exact unsaved pose. Draft picks and exports are disabled. Camera
and part visibility persist across pose changes; superseded workers cannot
replace a newer preview or reset. Requests are serialized and lost replies are
reconciled by polling rather than automatically repeating a save.

## Disk retention

Each displayed revision is evaluated once into `views/<view_id>/evaluations/`
(the frozen mesh, up to 64 MiB) and `evaluations/<evaluation_id>.json` (its
metadata, used to resolve picks). Retention keeps both bounded:

- A view keeps exactly one frozen evaluation: the one it displays. Publishing a
  new revision deletes the others under the view lock, and a result that can no
  longer publish (HEAD or the view moved on) is deleted at once. Switching the
  view to another document, or retrying a failed show, deletes them immediately.
- Publishing a new revision also deletes the previous display's metadata: that
  revision is superseded, so its picks already fail as stale.
- After a publication, at most once per 60 seconds per workspace, a sweep of up
  to 4,096 entries in `evaluations/` deletes metadata that is at least 60
  seconds old, displayed by no view, and whose document HEAD has moved past its
  revision. This includes `cad_query`, `cad_view` and `cad_preview` metadata.
  Metadata for a current HEAD revision is never deleted by age, so offline
  `cad_view` picks stay resolvable until the document changes.

Five successive revisions in one view leave one frozen file and one metadata
file for it (plus one metadata file per other view still displaying an older
revision). Readers that lose a deleted evaluation get an explicit
`stale_selection`; the viewer treats that as a temporary wait and re-syncs.
Live job records under `jobs/` follow the job retention policy in
[PROTOCOL.md](PROTOCOL.md): finished jobs are deleted after 7 days or beyond the
newest 256. Each can hold a mesh result of up to 64 MiB, so the worst case is
bounded but large; a workspace that renders many large models can keep up to
256 results on disk until they age out.

## Verification

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

When Node is available during development, CTest includes the bridge/state,
renderer and real MCP loop checks. Node is not shipped or needed by the native
bundle. `tests/live_mcp_flow.mjs` starts the real native stdio process and runs
the actual app bridge/controller through a protocol harness, including a real
visible-edge calculation, fillet, revision refresh and failed-edit rollback.
It does not mount a browser or claim GPU/host interaction evidence.

The app implements [MCP Apps 2026-01-26](https://github.com/modelcontextprotocol/ext-apps/blob/main/specification/2026-01-26/apps.mdx).
Exact tool contracts and limits are in [PROTOCOL.md](PROTOCOL.md), with recorded
platform evidence in [HANDOFF.md](HANDOFF.md).
