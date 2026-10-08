# Viewer shell redesign (sub-project A)

Date: 2026-10-08. Status: approved in conversation; implementation in progress.

## Goal

Make the embedded and standalone CAD viewer look and behave more like Shapr3D:
an edge-to-edge canvas with floating controls, a light studio look, one cursor
that selects faces or edges without a mode toggle, and Shapr3D-style navigation.

This is sub-project **A** of three. Out of scope here and specified separately:

- **B** — contextual edit toolbar: direct fillet/chamfer with live preview,
  hole placement, everything else handed to the agent. A reserves a slot in the
  selection bar for it; nothing fills the slot in A.
- **C** — face push/pull (extrude): needs persistent face selectors and a native
  feature. `PROTOCOL.md` states faces have no editing operation yet.

No protocol change. `PROTOCOL.md` is untouched and `cad_context` returns the
same selection references as before.

## Non-goals

Perspective projection (the renderer is orthographic and the shader, picking,
annotation layout and clipping assume it), multi-select or loop selection (the
saved context holds exactly one selection), multi-touch gestures, any direct
geometry editing.

## Layout (chosen: floating cards + tool dock)

| Today | New home |
|---|---|
| Wordmark, title, revision, connection | Project pill, top-left; name opens the Models popover |
| Model library, search, workspaces | Models popover |
| Features, Parameters | Scene card (left, collapsible) |
| Parts | Parts tab in the Scene card (assemblies only) |
| Inspect measurements and reference | Selection bar Details |
| Quick Edit, Send, Copy request | Selection bar prompt field |
| Faces/Edges toggle | Removed (unified selection) |
| Fit / Iso / Top / Front / Right | Orientation cube, shortcuts |
| Export, Save PNG | Export menu, top-right |
| Visual inspection, Colors, Review notes, Saved views, Exact section, Measurement, Sequences, Motion, Source | Right-hand dock; one popover at a time |
| Footer status, dimensions | Toast, status dot, small corner readout |

Existing section elements are **re-parented with their IDs intact**, so the
`app.js` wiring and the state/bridge logic are unchanged. A dock icon is shown
only while its panel is not `hidden`; `app.js` already hides Motion for
non-articulated models and Source for editable ones.

### Selection bar (bottom-centre)

Idle: a compact "Ask the agent…" pill for whole-model requests. With a selection
it expands to kind and subtype, the key measurement (length, area or radius), a
Details chevron, the prompt, Send, Copy request and clear. Details opens upward
with remaining measurements, part, feature, revision, reference JSON and the
"Include this view" toggle. Draft-pose and read-only text variants are kept.

### Responsive behavior

| Width | Behavior |
|---|---|
| ≥ 900px | Scene card open, dock shown |
| 560–900px | Scene card collapsed to a chip, dock as icons |
| < 560px | Dock folds into one Tools button, selection bar full width with the prompt on a second row, cube shrinks |

The viewer keeps working down to 360px.

### Accessibility

Every floating control is a labelled button. Popovers are non-modal dialogs;
focus moves in on open and returns to the opener. The dock is a toolbar with
arrow-key navigation. The orientation cube is CSS 3D transforms with six real
buttons, not a canvas. `aria-live` status regions are preserved. Secondary text
must meet 4.5:1; the mockup grey #76828f on white (about 3.9:1) is replaced by
about #5f6b78. Focus rings are a visible 2px blue. Motion honours
`prefers-reduced-motion`.

## Unified selection

`CadRenderer.mode` gains `auto`, the default. The UI no longer exposes mode;
`setMode('face'|'edge')` stays for compatibility with existing tests and callers.

Resolution in `auto`:

1. Run the existing visible-edge hit test with a **6px** screen tolerance
   (edge mode keeps 9px). An edge wins only if exactly one visible edge is
   nearest; the existing occlusion rules apply.
2. Coincident or ambiguous edges fall through to the face test.
3. The existing face trace runs; overlapping faces select nothing and report the
   existing "Geometry overlaps here" message.
4. Section-surface hits keep their existing message.

Hover highlights on pointer move while idle (one pick per frame, skipped while
dragging; none on touch). A face gets a soft blue wash, an edge a thicker light
blue line, and the cursor becomes a pointer. Click selects the highlighted
entity; clicking empty space or pressing Esc clears it. The selection keeps
`kind: face|edge`.

Read-only artifacts follow the same rule across curves and mesh groups.

Hover reuses the click's CPU hit test, bounded by `PICK_BUDGET`. Frame time is
measured on the largest example; if it is too slow, hover runs after a short
pause (about 80 ms) instead of every frame.

## Navigation

| Input | Action |
|---|---|
| Left-drag | Orbit (a click that moves under 3px still selects) |
| Right-drag | Orbit |
| Middle-drag, or Shift+drag with any button | Pan |
| Wheel or pinch | Zoom toward the cursor |
| Double-click an entity, or Space over one | Frame it |
| Double-click empty space | Fit the model |

Right-drag changes from pan to orbit; `LIVE_VIEWER.md` is updated.

Orientation cube (top-right, replaces the view buttons): rotates with the
camera; clicking a face snaps to Top, Front, Right, Left, Back or Bottom;
edges and corners give 45° and isometric views; dragging it orbits;
double-clicking returns to the default isometric view. Reachability of Bottom
and Back depends on the pitch clamp in `camera()` and is verified in
implementation.

Transitions: snaps, fit and frame animate over about 250 ms with ease-out; any
user input cancels; `prefers-reduced-motion` makes them instant; the camera is
saved to view state once, at the end.

Keys with the canvas focused: Home or 0 fits; digits 1–7 give iso, front, back,
top, bottom, right, left (Shapr3D documents 4–7 as top, bottom, right, left;
back on 3 is ours); Esc clears the selection; arrows and +/- are unchanged.

## Theme and rendering

All colours, radii and shadows are CSS custom properties. Light is the default.
A dark variant applies when the host reports `theme: dark` in its context, or
`prefers-color-scheme: dark` when no host theme is reported. `data-theme` on the
root carries the result, so a `host-context-changed` update switches it live.
The host-context `theme` field is checked against the MCP Apps spec text
during implementation; the OS fallback is kept regardless.

Renderer: the background becomes a vertical gradient **drawn in WebGL** (a tiny
extra pass driven by theme uniforms) so Save PNG matches the screen with no
compositing step. Line, selection and hover colours become uniforms set through
`renderer.setTheme()`. A selected face blends a blue tint into its own colour
instead of flat teal. Ambient rises and diffuse softens for a studio look. The
stored default model colour is user and agent data and is **not** changed.

Floating surfaces: white, 1px hairline, soft shadow, 12px card radius, pill bars,
subtle backdrop blur with a solid fallback. System font stack, sentence-case
labels. The CSP permits only inline CSS/JS and `data:` images, so icons are
inline SVG and there are no web fonts.

## Files

- Rewritten: `web/viewer.html`, `web/styles.css`.
- Edited: `web/app.js` (layout wiring; logic kept, export code stays so
  `tests/export_ui_tests.mjs` still qualifies it), `web/renderer.js` (auto pick,
  hover, `setTheme`, background pass, camera animation, zoom-to-cursor, frame,
  standard-view table, new mouse mapping).
- New: `web/shell.js` — popovers, dock, Scene card, selection bar, theme
  resolution, orientation cube DOM, icons.
- The asset list is hard-coded in `cmake/EmbedViewerApp.cmake`,
  `tests/embed_viewer_smoke.cmake`, `tests/app_protocol_tests.cpp` and
  `desktop/build.rs`; all four learn `shell.js` and `@VIEWER_SHELL@`. The
  canonical viewer-app hash changes; record the new local hash and do not claim
  cross-platform equality until CI rebuilds.

## Verification

Existing Node suites must pass unchanged (`viewer_renderer_tests.js`,
`live_ui_tests.mjs`, `playback_ui_tests.mjs`, `export_ui_tests.mjs`,
`artifact_retarget_ui_tests.mjs`, `desktop_bridge_tests.mjs`,
`webgl_renderer_tests.mjs`), as do the CTest embed and protocol tests.

New tests, in the existing `vm` style:

- `auto` pick: edge within 6px wins, ambiguous edge falls to the face,
  section-surface message preserved, artifact curve-vs-mesh rule.
- Hover never mutates the selection.
- Zoom-to-cursor keeps the world point under the cursor fixed within epsilon.
- Animation endpoints and cancellation.
- Standard-view table and cube hit mapping.
- Theme resolution and palette-to-uniform mapping.
- Responsive breakpoint decisions as pure functions in `shell.js`.

`app.js` and the layout have no Node coverage today. A dev-only loopback harness
(the built viewer plus a mock bridge, driven through the browser tooling)
produces screenshots at 360, 560, 900 and 1280px in light and dark. No new
product dependency and no Playwright install without asking.

Hover-pick frame time is measured on the largest example and recorded in
`HANDOFF.md`. `LIVE_VIEWER.md` and `HANDOFF.md` are updated. Per
`RELEASE_1_0.md`, passing tests does not establish host readiness; the handoff
states exactly what was verified.

## Done when

1. No Faces/Edges toggle; hover highlights and click selects a face or edge.
2. The layout renders at all four widths in both themes with no overlap or
   horizontal scroll.
3. Every existing panel is reachable and works, against a per-panel checklist.
4. The new navigation, cube and animation work.
5. Existing and new tests pass; hover timing is recorded.
6. Docs and `HANDOFF.md` are updated.
