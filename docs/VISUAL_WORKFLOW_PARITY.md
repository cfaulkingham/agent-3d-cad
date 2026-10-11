# Visual workflow completion

Authorized 2026-10-11: implement the four priorities from the local text-to-cad
comparison. The four tracks below are implemented in the native preview; their
local integration evidence and remaining limits are recorded here. The service
remains native, local and runtime-free for end users; source
documents, committed revisions, geometry qualification and bounded workers keep
their existing guarantees. Existing release/host/signing gates remain separate.

## 1. Agent screenshots and headless rendering

- Capture the actual currently displayed viewer frame through an agent-visible
  `cad_screenshot` tool. Return PNG image content in MCP and a usable independent
  artifact through the shared Service/CLI contract.
- Qualify the frame by native revision/evaluation or captured artifact hash,
  camera, appearance, presentation and displayed pose. Changed state, hidden or
  disconnected viewers and unavailable WebGL must fail explicitly.
- Keep MCP responsive while a screenshot waits for the app. Bound pending
  captures, transfer chunks, image sizes, waits and capture lifetimes.
- Add `cad_render` for native headless PNG generation with explicit dimensions,
  camera and shared display settings. Support native saved geometry and qualified
  captured external reviews without requiring a browser or language runtime.
- Verify real independently decoded pixels, silhouettes, occlusion, clipping,
  exploded placement, projection, materials and source identity. Exercise MCP
  image delivery, asynchronous jobs, cancellation and relocated packages.

## 2. Richer native drawings

- Multi-page drawing sets, one multi-page PDF, per-page SVGs and independent
  per-view DXFs; preserve single-page defaults.
- A0–A4 ISO sheets, portrait/landscape and explicit checked manual placement.
- Back/bottom/left and explicit orthogonal direction/up projection frames.
- Geometry-checked hole callouts: count, diameter, blind depth/THRU,
  counterbore/countersink and caller-declared thread/fit labels.
- Preserve exact projection cache independence from page layout and labels.
- Independently inspect PDFs/SVG/DXF and geometry evidence. Test empty/ambiguous
  bores, invalid depths, unfittable pages, history, jobs and download delivery.

## 3. Rendering and portable visual exports

- Shared closed `render` settings for Solid/Render/X-ray/Hidden-line/Wireframe,
  orthographic/perspective projection, lens, material finish/opacity, lighting,
  floor, background, grid, axes and edges.
- Render the requested effects in the actual viewer and native headless images;
  preserve correct picking under every supported projection/display mode.
- Static GLB with assembly hierarchy, placements and explicit appearance.
- Animated GLB from qualified declarative native motion, with correct coordinate
  conversion and independent glTF/FK verification.
- Portable animation image output from the same render contract; video/image
  outputs and any unsupported export effect retain explicit scope and errors.
- Validate actual rendered effects, captured frames, GLB binary/accessor bounds,
  transforms, materials, motion endpoints/intermediate samples and source history.

## 4. External-file fidelity

- DXF blocks/inserts, bulges, splines, text, dimensions and hatches with faithful
  source-qualified geometry/labels and explicit unsupported cases.
- GLB embedded materials/textures, animations, skins and morph targets with
  faithful displayed geometry; unsupported extensions/channels fail explicitly.
- URDF/SDF joint/pose review, paired SRDF semantics and supported relative frames.
- Keep original bytes and references captured, hashed and independently movable;
  verification must reparse and reproduce the accepted representation.
- Exercise actual viewer labels/materials/animations/robot poses and independent
  format oracles, stale pose/capture behavior and unchanged source history.

## Integration acceptance

Each track needs closed published schemas, shared Service/CLI/MCP behavior,
native and meaningful rendered tests, agent guidance, source-stamped portable
package evidence and a HANDOFF entry with exact executed scope. Full completion
requires checking every row above against current artifacts; a passing narrow
test or parser that drops accepted semantics does not establish fidelity.

Adaptive LOD, viewport sketch annotations, manufacturing workflow expansion and
additional host entry points were comparison findings outside the four ordered
priorities. They are not silently substituted for required work here.

## Delivered behavior and remaining differences

| Track | Implemented behavior | Practical limits |
| --- | --- | --- |
| Agent images | `cad_screenshot` captures the displayed WebGL frame and overlays; `cad_render` produces headless PNGs. Both return qualified files, downloads and bounded MCP image content. | Screenshots require a connected visible viewer. CPU JPEG textures and non-ASCII glyphs fail explicitly. |
| Drawings | Multi-page PDF, per-page SVG and per-view 1:1 DXF; ISO sheets/orientations, checked manual layout, additional/custom view frames and geometry-checked hole callouts. | Thread/fit designations are caller-declared labels. Geometry qualifies the bore dimensions and termination; it does not certify a manufactured thread. |
| Visual exports | Five display modes, projection/material/studio settings and persisted presets; hierarchical static/animated GLB and native streamed GIF share qualified source/sequence data. | GIF uses a fixed 256-color palette. MP4 remains unsupported. Native GLB carries sampled rigid transforms; clipping is explicitly rejected. |
| External fidelity | DXF blocks/curves/text/hatches, GLB appearance/skin/morph/animation, robot joints/relative frames and paired SRDF semantics; actual posed viewer geometry and source-hash verification. | Only documented supported subsets are accepted. Additional PBR texture channels, simulator behavior and unsupported drawing semantics fail explicitly. Review does not convert these files into an editable native feature history. |

Exact contracts and bounded-work limits are in [PROTOCOL.md](PROTOCOL.md),
[RENDERING.md](RENDERING.md), [DRAWINGS.md](DRAWINGS.md) and
[ARTIFACT_REVIEW.md](ARTIFACT_REVIEW.md). The native implementation is original
C++ and viewer code; the comparison repository remains a reference, not a
runtime dependency. These changes do not establish complete text-to-cad parity
or either target host's installation readiness.

## Local acceptance evidence — 2026-10-11

All four tracks passed the integrated **81/81 CTest** run in **107.72 seconds**.
The final warning-free native build has executable SHA-256
`dc157a25ba3cbbc84fcd1c9f0d99c64420fb6bbba1dc4efa17ca9774c2a79ac4` and production
source SHA-256
`72327b71e1d402dd4579005123eddd50319fd78ad4682a7441ae484ccdb669e3`.

| Evidence | Executed result | Local artifact |
| --- | --- | --- |
| Shared Service/CLI/MCP | 81 suites; 847 independent schema checks across 33 tools; official MCP SDK 2.3.0: 2,636 general and 369 visual assertions | `.local/evidence/visual-workflow-ctest-complete.log`, `visual-workflow-schemas-complete.log`, `visual-workflow-sdk-complete.log`, `visual-workflow/visual-sdk.json` |
| Actual screenshot and CPU/GPU pixels | 57 browser/native checks; silhouette IoU 0.987804878, zero interior RGB error, all differences within 2 px; both explicit lock-retry paths verified | `.local/evidence/visual-workflow/browser-native/` |
| Portable animated images | Independent Pillow: 65,664 native and 65,660 relocated empty-PATH checks; exact fixture PNG/GIF frame pixels, analytic moving clip, delays/loop/source/jobs | `.local/evidence/visual-workflow-oracle-complete.log`, `visual-workflow-relocated-visual.log` |
| Drawings | Independent strict PDF parse and Poppler raster; two ISO pages, four views, 24/24 bore-circle witnesses, exact 1:1 DXF, unchanged revision | `.local/evidence/visual-workflow/drawings/oracle-report.json` |
| External rendering and poses | Native review/pose and independent schemas; actual GLB skin/morph and SDF joint movement, exact resets, stable source framing, no browser faults | `build/artifact-flow-final-external-evidence.json`, `.local/evidence/visual-workflow/browser-poses/` |
| Portable delivery | Verified relocated 368-file bundle, empty-PATH native/plugin smoke, 367 documentation links, 391 isolated same-build replacement/reopen checks | `.local/evidence/visual-workflow-bundle-complete.log`, `visual-workflow-package-lifecycle.log`; native/plugin/Claude artifacts in `build/packages/visual-workflow-20261011/` |

Developer runtimes and independent readers are test tools only. Product outputs
come from the packaged native service. The browser protocol fixture uses the
actual shipped renderer and native MCP service; it does not emulate a target
host's installation, reload or save/download UI. Reproducible commands are in
[DEVELOPMENT.md](DEVELOPMENT.md), with exact limits and next release tasks in
[HANDOFF.md](HANDOFF.md).
