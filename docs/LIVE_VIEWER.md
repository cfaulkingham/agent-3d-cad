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
editable source intent; there are no assembly or hide/isolate controls yet.
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
