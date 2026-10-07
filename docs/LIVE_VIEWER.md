# Live CAD viewer preview

The native service now includes an MCP App for the core create–view–select–edit
loop. The app opens a model library, source feature tree, WebGL viewport and
Quick Edit panel. Committed edits appear automatically, retaining the camera.
Selections name an exact evaluated revision; updated geometry clears old picks.

This is an implementation preview. Native integration, real MCP SDK, revision
races and renderer mathematics are tested. The actual Codex MCP App on macOS
arm64 has rendered a plate, accepted a human edge pick, and followed an agent's
selective fillet to revision 2 in the same viewer, retaining the camera and
clearing the old pick. Quick Edit handed the reference-qualified request to
the host's chat composer; this host requires the user to press Send there.
Windows and remaining architecture runs remain unverified. The offline
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

## Use the live loop

Ask the agent to create or reopen a design. For a concrete first session:

1. Create the simple plate from `examples/live-plate.create.json` using `cad_create`.
2. Call `cad_open` with `{ "document_id": "live_plate", "view_id": "plate_review" }`.
   The host should render the app; saved models appear in the left library.
3. Choose **Edges**, click a visible edge, then type “Round this edge to 1 mm”
   in **Quick Edit**. **Send to chat** carries the exact reference. If the host
   places it in the chat composer, press Send there to submit it to the agent.
   If the host does not support messages, **Copy request** provides the same context.
4. The agent reads the document, resolves the pick with `cad_resolve_selection`,
   then uses its selector in a fillet and commits with `expected_revision`.
5. The same viewer follows the new revision automatically. It retains orbit,
   zoom and pan, and clears the obsolete selected edge.

The model library switches documents in this view. Features expand to show
editable source intent. Assembly features also list their part IDs, source
features and parent mate relationships; selected geometry identifies its owning
part. Placement and mate edits use the shared CAD tools. Assembly part controls
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

Drag to orbit, Shift/right drag to pan, wheel to zoom, and use the view buttons
for orthographic directions. Keyboard arrows orbit; Shift+arrows pan; Home resets.
Selected faces/edges display their native measurements. Faces are inspectable;
the current selective-filleting operation uses edges.

Quick Edit hands a request to the host; it does not directly mutate geometry or
guarantee that the host posts a message automatically. Complete any composer
Send step before expecting an agent response.
Optional “Include this view” adds a PNG only when the host supports image messages.
If a send times out, check the chat before sending again: delivery may have occurred.
Failed geometry preserves the last committed design. For a candidate that must
not commit, the agent can use the separate offline `cad_preview` workflow.

Read-only polling retries temporary lock contention, queue saturation and an
evaluation superseded during transfer. While waiting, the last rendered solid
remains visible and old picks are disabled. Persistent failures stay explicit.
Polling recovery never automatically resends a Quick Edit request.

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
cmake --build build-package --parallel 4
ctest --test-dir build-package --output-on-failure
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
