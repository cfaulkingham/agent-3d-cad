# Implementation handoff

Updated: 2026-10-10. **M0–M3, M4 live viewing, M5 drawings, and M6 assemblies are native previews.**
Actual Codex-host rendering and select–edit–refresh are demonstrated on macOS
arm64. Earlier preview sources passed all five macOS/Linux/Windows CI lanes and
independent Arch Linux x86_64 validation. Each increment below records its own
validation scope. The first public prerelease is `v0.1.0-preview.1`; publisher
signing/notarization and remaining host validation are still open.

## Modeling gap closure — active integration, 2026-10-10

The comparison target is the actual sibling `../build123d/`. The user authorized
native shell/offset/thicken, sketch operations and mirror/split/intersection,
captured text/SVG/DXF authoring, richer extrusions/sweeps, followed by separate
sheet-metal and freeform-surface implementation phases. Work is on
`codex/modeling-gap` with isolated managed worktrees; no sibling runtime or source
dependency is added. This goal remains active and does not claim full library
parity or host installation readiness.

The current integration branch implements controlled extrusions and sweeps in
`RICHER_MODELING.md`: signed directed/bidirectional travel, exact contour taper,
finite target first/last termination, native frame/guide/transition controls,
varying sections and holes. Dependencies include termination targets in cache,
components and provenance. Varying sections must occupy unique ordered path
stations including both endpoints. Complete profile coverage, solid validity and
positive volume are required; failed edits preserve the committed record.

The dedicated native test executable passed **43 checks** on macOS arm64 /
OCCT 8.0.1, including analytic volumes/bounds, invalid and collapsed geometry,
sloping target termination, curved auxiliary guides, varying hollow sections,
cache invalidation, worker rollback, cold reopening and STEP export. Testing
exposed an OCCT medial-axis crash for a full circle inset beyond its center;
exact circle construction and a preflight collapse error replace that case.
Parallel straight auxiliary guides use their exact constant frame instead of an
unnecessary native law approximation. Summary mass/area use adaptive exact
surface integration for native rational sweep surfaces. The native build and
`git diff --check` pass. A combined full-suite run is still pending integration;
no current Windows/Linux or installed-plugin verification is claimed.

Other agents have individually tested shell/offset/thicken, derived sketches,
and text/SVG/DXF authoring in separate worktrees. Their APIs remain pending on
this branch until their commits are integrated and checked together. Sheet metal
and freeform surfaces are separate authorized phases still being implemented.
Next: integrate those tested core commits, check schema/discovery and shared
classification/cache/component behavior, finish the two larger phases, then run
the complete native and independent live-schema suites.

## Carousel workflow: STEP diagnosis, periodic meshes and native 3MF — 2026-10-10

Investigated the actual failures in Codex chat `01a12666-30df-7f70-93d1-6b1df443ca91`
("Fix shaft tolerances"). Preserved the existing STEP-size-limit changes below.
The user's original STEP has 51 solids and one invalid lower-shaft solid; a later
revision with that shaft fixed still contains valid roller faces that OCCT's
ordinary triangulator cannot mesh. These are separate failures.

Implemented:

- `cad_inspect_step` is a read-only, worker-backed tool (also through `cad_job`)
  reporting source hash, per-solid validity, tessellation, bounds and named BRep
  diagnostics. The actual original source identifies solid 31, face 106
  `UnorientableShape` and wire 118 `SelfIntersectingWire`. Diagnostic face/wire
  indices are local to that checked shape. New explicit `cad_import.solid_indices`
  requires `expected_sha256`; extraction retains complete original bytes and hash,
  requires complete STEP root transfer and validates every selected solid. Source
  solid indices are qualified by the exact source hash and pinned kernel.
- Valid periodic faces that fail triangulation get a private tessellation retry,
  with original face identity retained for picking. Exact saved B-reps and source
  revisions are unchanged. For print meshes, narrowly eligible full conical or
  cylindrical walls get a private seam normalization and sewn mesh boundary, with
  validity, area, volume and bounds checked. STL now rejects incomplete/open meshes
  instead of letting the old writer silently skip missing face triangulations.
- Native geometry-only 3MF export preserves separate source solids. Optional
  rectangular-bed layout packs across at most 64 plates with explicit margins,
  spacing and quarter-turns. Complete explicit placements support XYZ orientation
  and layout reuse across edits. Actual mesh vertices rest on Z=0. Each plate has
  standard OPC/3MF metadata, a hash and source IDs; `layout.json` records placement
  and source identity. Publication is staged, locked and content-qualified.
  STEP/STL/3MF exports support `feature_id`; the embedded Export menu offers 3MF.
- Installed skill references now resolve beside `SKILL.md` in native and plugin
  bundles. Skill guidance uses native inspection/extraction/print layout and the
  existing embedded PNG action. No build123d/Python dependency was added to the
  product. The standalone adapter accepts 3MF, but its installed app was not updated.

Validation on macOS arm64 / OCCT 8.0.1:

- Native build succeeded. A complete serial CTest pass ran all 58 suites in
  **210.92 s**: 56 passed; two test harness failures were corrected (bounded retry
  for the documented transient job publication lock, and applying the same
  **15 KiB/tool** discovery budget to 29 tools). Both reran successfully in
  **6.62 s**. All 58 have passing evidence across those runs; no single clean
  full-suite run is claimed. Earlier timeout failures in purchased-part/STEP
  suites passed in the final serial run. The exact periodic-cone fixture covers
  display face coverage, unchanged source bytes/geometry, closed meshes, layout,
  extraction/hash rejection, and asynchronous inspection.
- Independent live schema validation: **907 checks / 29 tools**; actual artifact
  MCP schema validation: **209 checks**. Rust/native integration tests: **3/3**,
  including 3MF export. Packaged-plugin smoke passed provenance, skill references,
  empty-PATH startup, create/edit/history, native STEP inspection and all exports.
- The actual **9,965,224-byte** revision before roller replacement renders every
  original face and exports **all 51 solids on four 256 × 256 mm plates**, with
  8 mm margins and 3 mm spacing. Independent ZIP/XML, mesh-edge, signed-volume,
  bed-contact and clearance checks pass. The official lib3mf reader reports
  **zero warnings**, with every mesh manifold and consistently oriented.
- Real browser harness testing displayed that complete carousel and completed
  Export → 3MF. Screenshots are `viewer-carousel.jpg` and `viewer-3mf-export.jpg`.
  This is browser harness evidence, not a claim of reloaded host-session behavior.

Installed the tested working tree over `1ce76d5`, without changing VERSION or
publishing a release. Active local plugin source is
`~/Applications/Agent3DCAD/chatgpt-plugin-1ce76d5-carousel-workflow-20261010`;
all **342 files** match provenance, all **102 saved model files** and unrelated
settings were preserved, and the previous source/configuration backup remains.
Fresh ChatGPT `0.144.0-alpha.4` and Codex `0.162.0-alpha.17.2` runtimes discover both
skills and all **29 tools**, including STEP inspection and the 3MF schema. The
installed binary, with PATH empty and development loader overrides removed,
diagnosed the actual invalid source, explicitly imported its 50 valid solids,
then imported/rendered all 51 solids of the corrected revision and exported the
four plates. Independent lib3mf and mesh/layout checks passed on those installed
outputs too; saved models were rechecked unchanged after execution. The
packaged binary SHA-256 is
`55586814d0c69ed2b4687137593defe3f5eb4f6d32e07126d96d58ab7a6cec8d`.
Evidence and reproducible local probes/install scripts are in
`.local/evidence/carousel-workflow-20261010/`.

Limitations/next work: invalid source solids still require an explicit modeling
correction; this does not silently heal self-intersecting thread geometry or
recover STEP design history. Compound `solid-N` print IDs are revision-local;
check correspondence after topology changes. Rectangle packing is conservative,
not optimal; 3MF contains geometry, not slicer profiles/supports or print approval.
Restart the host to use the new installed server in an existing chat. Remaining
1.0 host/platform/signing gates and broader build123d feature parity are not closed
by these workflow fixes. Add cross-platform regression evidence for this increment
before making a new platform compatibility claim.

## STEP importer rebuilt and installed in ChatGPT — 2026-10-10

Rebuilt the working tree over `1ce76d5`, installed a fresh portable core bundle,
and packaged Agent CAD without changing VERSION (`0.1.0-preview.1`) or publishing
a release/tag. Standard packaged-plugin smoke passed complete provenance,
empty-PATH MCP/viewer discovery, create/edit/reopen/history and
STEP/STL/PDF/SVG/four DXF exports.

Installed via `/Applications/ChatGPT.app/Contents/Resources/codex` into the existing
`agent-cad@agent-cad-local` registration. Active source is
`~/Applications/Agent3DCAD/chatgpt-plugin-1ce76d5-step-import-20261010-chatgpt`;
cache is `~/.codex/plugins/cache/agent-cad-local/agent-cad/0.1.0-preview.1`.
The contained modern launcher is `./bin/agent-3d-cad-step-import-20261010`.
All **310 installed package files** match the prepared provenance inventory;
all **56 saved model files**, the existing `~/Documents/Agent3DCAD` workspace
argument and unrelated parsed settings are preserved. Previous sources remain;
the original configuration backup is mode 0600.

ChatGPT's bundled runtime (`0.144.0-alpha.4`) needed legacy `.codex-plugin/plugin.json`
and `.mcp.json` metadata, plus an absolute installed-source launcher path in that
legacy MCP file. Its initial generic install synthesized incomplete metadata and
could not discover the server; relative legacy launch also produced zero tools.
The final install-specific compatibility files are included in provenance. The
portable modern manifest/launcher remains alongside them. Repository release
packaging has not been changed to promise legacy-host portability.

Final verification:

- Fresh ChatGPT `0.144.0-alpha.4` and current desktop `0.162.0-alpha.17.2` runtimes
  both discover the enabled plugin, both skills and **28 tools**. Their actual
  importer descriptions advertise removal of the fixed source byte cap.
- The installed contained binary, with PATH empty and no development loader
  overrides, imported a **2,435,390-byte STEP**, preserved its exact source/hash
  and **80 mm³** solid volume, then reopened, rebuilt and replayed the committed
  import after deleting the original. The workspace was an isolated temporary
  directory; actual user model files were rechecked unchanged afterward.
- Native binary SHA-256:
  `7d5837fee4b2e808f0dec0ce26db38070b2a01fc67cf503bca624c79063c18c4`.
  Embedded viewer SHA-256:
  `8c327ffb7c2be9305cc22e002a347213d0cb73d5600a32c2388ade5ae9f5e860`.
- `git diff --check` passed. Build, packaging, installer, discovery and large-import
  evidence/scripts are in `.local/evidence/step-import-install-20261010/`.

The current host session's existing connection was not restarted. Quit and reopen
ChatGPT before validating this build in the actual embedded session. Fresh runtime
checks do not establish that reload or any remaining 1.0 host/platform/signing gate.

## STEP imports without a fixed source byte cap — 2026-10-10

Removed the 512 KiB STEP restriction from native import, feature validation,
discovery schemas and read-only artifact review. STEP source bytes remain embedded
unchanged with their verified SHA-256. Model-bearing internal JSON now budgets
metadata separately from `import_step.content`, including component snapshots,
worker inputs, revision storage, receipt recovery and durable job requests/results.
This also removes the hidden 1 MiB save/rebuild and 64 MiB job-result ceilings for
STEP content. Artifact capture, ledgers and verification likewise exempt the
original STEP bytes from file/package caps; other formats keep their existing bounds.

Worker memory budgets can exceed 4 GiB (native signed 32-bit MiB representation);
defaults remain 2,048 MiB / 30 seconds, with larger explicit budgets via `cad_job`.
UTF-8, source hash, complete-root transfer, geometry validity, cancellation and
atomic publication checks remain. Model metadata, geometry/display and transport
request limits remain: large STEP files are supplied by path, not inline in MCP
requests. Source strings are still held in memory, sometimes in multiple copies;
this is not streaming/out-of-core import. Other export/package limits still apply.

Validation (macOS arm64, pinned OCCT 8.0.1):

- Native build passed. New `step_import` CTest passed in **11.07 s**, using valid
  STEP solids with legal comment padding above **2 MiB** and **65 MiB**. It covers
  exact volume/bytes/hash, cold reopen/rebuild, edit/export/restore, failed-geometry
  rollback, receipt-index recovery, portable pinned components, 8 GiB job admission,
  durable large-result read/replay, artifact capture and portable verification
  after deleting the original. Padding isolates byte limits from geometry complexity;
  this does not establish performance for arbitrary complex vendor models.
- Independent schema conformance passed **927 checks across 28 tools**, including
  actual large CLI import/read results and the expanded memory-budget schema.
- The other **56 CTest suites** ran with two-way parallelism: **54 passed**;
  playback and annotation hit wall-time limits (both reported roughly 253 s
  despite 120 s test limits). Both passed unchanged in an isolated serial rerun:
  playback **3.54 s**, annotation **2.43 s**. All **57 suites** therefore have
  passing evidence across these runs; no single clean full-suite run is claimed.
- Independent actual artifact MCP schema validation passed **199 checks**, including
  acceptance of STEP sources above 64 MiB and retained limits for other formats.
  `git diff --check` passed.

Implementation evidence: `.local/evidence/step-import-20261010/`. The installation
follow-up above records the rebuilt and deployed plugin. Existing 1.0
platform/host/signing gates still apply.

## Export dropdown on the bottom toolbar — 2026-10-09

Moved Export from the Models menu to the right end of the bottom toolbar. Its
dropdown retains STEP/STL/PDF/SVG/DXF saved-revision exports and PNG capture.
Export stays visible at compact widths while camera and inspection controls
scroll horizontally. Escape closes the dropdown and returns focus to its toolbar
button. Existing draft/read-only restrictions and native export behavior are
unchanged; no tool or document contract changed.

Validation (macOS arm64): native build succeeded; **7/7 relevant CTest suites
passed in 0.80 s** (embedded_assets, desktop_assets, artifact_retarget_ui,
desktop_bridge, viewer_shell, live_ui and export_ui). JavaScript syntax and
`git diff --check` passed. Real browser checks at 900 and 360 px verified direct
toolbar access, the format/PNG options and Escape focus restoration. At 360 px,
Export occupied x=286.26–354, fully inside the viewport. Clicking Export saved
revision produced a valid 30,427-byte STEP file from the temporary bracket
workspace. Standard packaged-plugin smoke checks passed complete provenance,
empty-PATH discovery, create/edit/reopen/history and all supported CAD/drawing
formats. Evidence and screenshot: `.local/evidence/export-toolbar-20261009/`.

Installed the working-tree build through the bundled desktop CLI; VERSION stays
`0.1.0-preview.1`. Current source is
`~/Applications/Agent3DCAD/chatgpt-plugin-68fadab-export-toolbar-20261009`, using
contained command `./bin/agent-3d-cad-export-toolbar-20261009`. All **308 installed
files** match the prepared inventory, all **42 saved model files** are unchanged,
and the workspace override and unrelated settings are preserved. Native SHA-256:
`9a9db364da0b2f5e78e6bf9567d9355cb2e6df02272a58a5b04034b0d8497572`.
Embedded viewer SHA-256:
`8c327ffb7c2be9305cc22e002a347213d0cb73d5600a32c2388ade5ae9f5e860`.
A fresh bundled runtime discovers the enabled plugin, both skills and **28
tools**; the installed native service serves the exact toolbar viewer with PATH
empty. Earlier versioned sources remain; no public release/tag was created.
Current-session embedded reload acceptance remains pending: fresh-runtime and
browser-harness checks do not establish it. Restart the host and reopen the
viewer to verify that gate. Remaining 1.0 host/platform/signing gates still apply.

## Select in the viewer, request edits in the main chat — 2026-10-09

Removed the embedded Quick Edit composer, Send/Copy request controls, request
details/reference drawer and Models-menu Ask agent entry. A compact top
indicator shows the selected face/edge, its native measurement, **Ask in chat**
and Clear selection. PNG export and exported-path copying remain available.
Picks and clears publish native context immediately; camera movement remains
debounced. Entering the point ruler clears a prior native geometry selection so
the main chat cannot accidentally reuse it. No selection click posts a chat
message or edits geometry. Optional host model-context delivery remains separate
from native persistence and revision polling.

The packaged native-cad skill now directs agents to read `cad_context` with the
retained `cad_open` view ID when users refer to selected geometry in the main
chat, check stale/draft/read-only state, resolve a native reference and use the
current revision for edits. Specification, protocol, roadmap and viewer guide
describe this interaction. Native tools/documents and legacy optional prompt
fields are unchanged; legacy controller message helpers retain their existing
coverage but have no viewer UI entry point.

Validation (macOS arm64):

- Native build succeeded. **13/13 relevant CTest suites passed in 23.82 s**:
  embedded_assets, app_protocol, live, live_mcp_flow, webgl_renderer, viewer_shell,
  live_ui, playback_ui, export_ui, artifact_retarget_ui, offline_renderer,
  desktop_bridge and desktop_assets. Three added real MCP checks verify qualified
  pick retrieval without posting a message, clearing a pick, and selection
  handoff without messaging or optional host-context capabilities. Existing
  native select/resolve/fillet/revision/stale-reference checks still pass.
- After updating stale-context wording, rebuilt and repeated embedded_assets,
  desktop_assets, desktop_bridge and live_ui: **4/4 passed in 0.68 s**. JavaScript
  syntax and `git diff --check` passed.
- Real browser/native harness verification selected face-10 and edge-16 on the
  bracket, then read each exact qualified reference through `cad_context` with
  `stale:false`. Entering ruler mode cleared the native selection. DOM inspection
  found no composer or Send button. The compact indicator retained its pick and
  stayed inside the 360 px viewport (187.21 px wide); final preview is 900 px.
  This used an isolated temporary workspace and did not edit user models.
- Standard packaged-plugin smoke checks passed complete provenance, empty-PATH
  discovery, create/edit/reopen/history and STEP/STL/PDF/SVG/four DXF exports.
  Evidence, screenshots, context captures and scripts are under
  `.local/evidence/main-chat-selection-20261009/`.

Installed the updated working-tree viewer and skill through the bundled desktop
CLI. VERSION remains `0.1.0-preview.1`; no public release/tag was created. Current
source is `~/Applications/Agent3DCAD/chatgpt-plugin-42a19c1-chat-context-20261009`,
with contained command `./bin/agent-3d-cad-chat-context-20261009`. Previous
versioned sources remain. All **308 installed files** match, all **42 saved model
files** are byte-identical through installation, and the workspace override and
unrelated settings are preserved. Native SHA-256 is
`4925bef29a35fa7146a374fd3723b3cb8ed0cebc9709ca01b476d282dfe311ba`;
embedded viewer SHA-256 is
`54ad07ec11fda31ee0bcb5770c3cdc0b2b5d23ccb5a59a596cf24e83faae17a3`.
A fresh bundled runtime discovers the enabled plugin, both skills and **28
tools**. The installed native service serves the exact final resource with PATH
empty; its HTML has no composer/Send control and the installed skill includes
the main-chat workflow. Actual current-session embedded reload acceptance is
still pending as described below; these checks do not establish it. Restart the
host and reopen the viewer to verify that gate. Remaining 1.0 host/platform and
signing gates still apply.

## Design Details without raw native feature JSON — 2026-10-09

Removed raw source JSON from the native Design Details feature list. Ordinary
features show their names, types and output marker as compact rows, without empty
expanders. Components and assemblies retain expandable pinned-source identity,
part/source IDs and mate relationships. Editable source and native tool/document
contracts are unchanged. Read-only artifact Review data and Quick Edit selection
references are outside this change.

Validation: native build succeeded; **5/5 relevant CTest suites passed in 0.73 s**
(embedded_assets, desktop_assets, viewer_shell, live_ui, artifact_retarget_ui).
Real in-app browser inspection found four compact bracket feature rows and zero
raw JSON blocks; expanding the spacer assembly retained all four part/mate rows,
also with zero raw JSON blocks. The packaged-plugin smoke checks passed complete
provenance, empty-PATH discovery, create/edit/reopen/history and all supported
export formats. `git diff --check` passed. Evidence, screenshot and scripts:
`.local/evidence/design-details-20261009/`.

Installed the updated working-tree build through the bundled desktop CLI, keeping
VERSION `0.1.0-preview.1`. Current source is
`~/Applications/Agent3DCAD/chatgpt-plugin-42a19c1-design-details-20261009`, using
contained stdio command `./bin/agent-3d-cad-design-details-20261009`. All **308
installed files** match the prepared inventory, all **42 saved model files** are
byte-identical through installation, and the existing workspace override and
unrelated settings are preserved. Native binary SHA-256:
`a1c7d272ed9e7d90ea961cebb524cc9f9d65364b7c2b65451ccff5086816cfda`.
Embedded viewer SHA-256:
`cb8915cc00d934a7907837429812ebc618922525b4f6ed2eac274ccb85c421d7`.
A fresh bundled runtime finds the enabled plugin, both skills and **28 tools**;
the installed native service serves that exact viewer hash with PATH empty and
reads the existing library. Earlier versioned sources are retained; no public
release/tag was created. Actual current-session embedded reload acceptance
remains pending as recorded below; fresh-runtime and browser-harness checks do
not establish it. Remaining 1.0 host/platform/signing gates still apply.

## Panel-free A/B ruler and visible-edge snapping — 2026-10-09

The bottom Measure button now enables direct point picking without opening a
panel. Click A, then B; the canvas shows both markers, their connecting line and
the straight-line 3D distance in mm. Clicking a marker replaces that endpoint;
clicking another location after completing a pair starts a new pair. Escape or
the ruler button exits the mode; re-enabling restores the points. Orbit/zoom
preserve them and PNG capture includes the active ruler.

Picks within six CSS pixels of visible displayed edge polylines snap to the
nearest edge point or endpoint, even with edge outlines disabled. Occluded,
hidden and clipped edges do not attract picks; exploded edges follow their
displayed placement. Other clicks use the frontmost displayed surface, including
section caps. Accuracy is limited by tessellation and edge polylines. This is
local, ephemeral viewer state for native models and read-only artifacts; it does
not submit native jobs or change saved geometry/history. Source/evaluation,
artifact hash, visibility or presentation changes invalidate points. Draft poses
must be saved or reset first. Existing exact native measurements remain in the
inspection tools as **Face and part clearance**; tool/document contracts are
unchanged.

Executed macOS arm64 evidence:

- Native build succeeded. **13/13 relevant CTest suites passed in 13.01 s**:
  embedded_assets, app_protocol, live, live_mcp_flow, webgl_renderer, viewer_shell,
  live_ui, playback_ui, export_ui, artifact_retarget_ui, offline_renderer,
  desktop_bridge and desktop_assets. Counts include **362 live UI**, **118
  renderer**, **37 artifact/retarget**, **72 real MCP**, **269 native live** and
  **6,613 app-protocol** checks. After shortening the completed-pair hint, rebuilt
  and repeated embedded_assets/desktop_assets: **2/2 passed in 0.59 s**.
- Real in-app browser verification in an isolated native-service harness showed
  no measurement dialog, sequential A/B picks and snapping from clicks about
  three pixels inside a visible edge with outlines disabled. Replacing A
  retained B, orbit retained distance, and Escape/re-enable restored the pair.
  Final source screenshot `preview.png` shows the snapped **8.9366741 mm** pair.
  State/renderer tests also cover zero distance, full 3D distance, miss retention,
  visibility/clipping/explosion, lifecycle cleanup and PNG ruler capture.
- Unmodified packaged-plugin smoke checks passed complete provenance,
  empty-PATH MCP/viewer discovery, create/edit/reopen/history and
  STEP/STL/PDF/SVG/four DXF exports in an isolated workspace.
- Installed the working-tree viewer over source `42a19c1` through the bundled
  desktop CLI. VERSION stays `0.1.0-preview.1`; no release/tag was created.
  Current source is
  `~/Applications/Agent3DCAD/chatgpt-plugin-42a19c1-points-runtime-v2-20261009`.
  Cache remains `~/.codex/plugins/cache/agent-cad-local/agent-cad/0.1.0-preview.1`.
  Its stdio command uses the contained executable
  `./bin/agent-3d-cad-viewer-points-20261009`, a byte-identical copy of the packaged
  binary, to distinguish this runtime configuration. Previous versioned sources
  are retained. All **308 installed files** match; native SHA-256 is
  `7838f2b6ecfacf377fab763d515f1c506c2121a8c260a4e4a2183b15ee10b768`;
  embedded viewer SHA-256 is
  `c77d01eb0a0cfac4188d335f8d186f15628f5dbf0b3fd37978cf24d84af3e296`.
  Existing Documents/Agent3DCAD override and unrelated settings are preserved;
  **42 saved model files** across both Documents folders stayed byte-identical
  through installation. A fresh bundled runtime discovers the enabled plugin,
  both skills and **28 MCP tools**, serves the final viewer hash with PATH empty
  and reads the existing library.

**Current-session refresh remains pending.** After installation, `cad_open`
opened the active bracket in `chat_01a12354_points_runtime_verify`. The user
expanded that actual MCP App; inspection still found the old Measure dialog and
no `point-measure-overlay`. This is current-host evidence, not a successful
reload. Available plugin tools provide no connection restart, and native host
control was rejected by computer-use safety during the preceding reload task.
Quit and reopen the host to load the installed runtime, then verify the actual
embedded A/B interaction. Do not count fresh-runtime or developer-harness checks
as that acceptance gate. Evidence/logs/scripts are in
`.local/evidence/point-measurement-20261009/`; latest installation and cached-host
DOM/screenshot are under `runtime-reload-contained/`. Remaining 1.0 host,
platform and signing gates still apply.

## Screenshot-reference plugin installed; running host refresh pending — 2026-10-09

Packaged committed viewer source `42a19c1` using the existing native build and
installed it through the bundled desktop CLI. VERSION stays
`0.1.0-preview.1`; no public release/tag or standalone installation changed.
Source is `~/Applications/Agent3DCAD/chatgpt-plugin-42a19c1-viewer-20261009`;
cache remains `~/.codex/plugins/cache/agent-cad-local/agent-cad/0.1.0-preview.1`.
The previous versioned source is retained. The existing
`~/Documents/Agent3DCAD` workspace override and all unrelated parsed settings
are preserved; the configuration backup is mode 0600.

Validation:

- Unmodified `tests/plugin_smoke.py` passed complete provenance, empty-PATH
  MCP/viewer discovery, create/edit/reopen/history and STEP/STL/PDF/SVG/four DXF
  exports in an isolated temporary workspace.
- All **307 installed files** match the prepared package. Installed binary
  SHA-256 is `f1ed645ca99a54ec8308702ef93ee9751e24484f6ae9f4a5733e430de02b502d`;
  embedded viewer SHA-256 is
  `4da4b39e6399f4c5c23b1dae122aa37295f5396b9482077d6fba13d288784db5`.
- A fresh bundled runtime discovers the enabled plugin, its two skills and
  **28 MCP tools**. The installed service serves the exact new viewer hash
  with PATH empty and reads the existing library. All **38 saved model files**
  across both Documents project folders remain byte-identical after checks.

The current ChatGPT session's existing MCP connection still serves the previous
viewer, confirmed by the actual MCP App DOM after `cad_open` reopened the active
bracket in `chat_01a12354_viewer_reload`. Native control of ChatGPT
(`com.openai.codex`) was rejected by computer-use safety, so the agent could not
press a host reload control. Quit and reopen ChatGPT to load the new package.
The separate daemon proxy does not control this desktop app's stdio connection;
its probe did not establish a live reload. Do not count fresh-runtime checks as
current-session rendering acceptance. Evidence and installation/check scripts:
`.local/evidence/plugin-reload-viewer-20261009/`.

## Screenshot-reference viewer and direct parameter controls — 2026-10-09

Reworked the shared embedded/Tauri `web/` viewer around the user's screenshot:
light gray full-canvas stage, translucent Parts and Parameters cards, compact
Models menu, point-selection hint and a fixed bottom toolbar. Existing model
library, exports, design details, Quick Edit and inspection tools remain available.
No native tool/document contracts or OCCT geometry implementation changed.

Implemented behavior:

- Parts tabs isolate actual assembly occurrences; Assembled restores all leaves.
  Multiple tabs wrap inside a bounded vertical area, avoiding a horizontal
  scrollbar covering their hit targets. Single solids show their output name.
- Parameters provide sliders and numeric fields. Slider release, numeric blur or
  Enter submits one bounded `cad_apply` job with captured document identity and
  expected revision. Native failures preserve HEAD and restore the displayed
  value. Unknown/nonfinite/out-of-range, draft and read-only edits cannot submit;
  concurrent edits are gated, obsolete responses are discarded and uncertain
  mutations are never resubmitted. Numeric fields expose exact values on focus.
- Compare with reads parameter differences against up to 50 recent revisions;
  changing source or revision clears the comparison. This is a value comparison,
  without a geometry overlay.
- Iso/Front/Top/Right buttons follow the actual camera. The bottom toolbar toggles
  grid, XYZ guide and edge outlines, opens measurement and fits geometry. Hover
  and qualified selection work while outlines are hidden. Grid and XYZ guide are
  camera-aligned WebGL2 backdrop decorations included in PNG capture; the guide
  is anchored near the model and does not claim to mark the document origin.
- Cards collapse and Parameters can close/reopen. Widths below 900 px use reopen
  buttons, with one card at a time below 560 px. Layout changes preserve orbit and
  zoom while adjusting pan for the uncovered viewport.
- Real GPU inspection found a stale vertex-array issue during hide/restore:
  the attribute-less backdrop could inherit arrays pointing to deleted geometry
  buffers. It now disables all geometry arrays before drawing. A regression
  checks isolation/restoration, and the real browser subsequently restored the
  assembly and switched theme without blanking the canvas.

Executed evidence (macOS arm64):

- Final `cmake --build build --parallel 4` succeeded. **13/13 relevant CTest
  suites passed in 12.97 s**: embedded_assets, app_protocol, live, live_mcp_flow,
  webgl_renderer, viewer_shell, live_ui, playback_ui, export_ui,
  artifact_retarget_ui, offline_renderer, desktop_bridge and desktop_assets.
  Counts include **109 renderer**, **344 live UI**, **6 shell**, **72 real MCP**,
  **269 native live**, and **6,613 app-protocol** checks. The full unrelated native
  geometry suite was not repeated. Syntax checks and `git diff --check` passed.
- The dev-only MCP Apps harness used the real native service in an isolated
  temporary workspace. Codex's in-app browser demonstrated numeric commit,
  slider keyboard commit, exact revision comparison, a rejected negative width
  retaining geometry, part isolate/restore, grid/outline toggles, Top view, model
  library retarget, panel reopen and widths 360/560/1280, including live dark mode.
- Evidence is in `.local/evidence/viewer-reference-20261009/`: build/CTest logs,
  a valid 35 × 40 × 35 mm native bracket record and `preview.png`. The temporary
  harness remains open for interactive review; no user Documents workspace,
  installed plugin or standalone app was replaced.

Limits/next: package and install the reviewed UI for actual embedded-host and
standalone delivery, then complete remaining 1.0 host/platform/signing gates.
This increment validates the developer harness and built native resource, not a
new installation or release. Projection remains orthographic; WebGL1 retains
its solid backdrop fallback. Slider ranges are UI windows, not design constraints.

## Current local plugin build and in-app library — 2026-10-09

Built committed `main` source `9f79efa` with the retained OCCT 8.0.1 v2 HLR SDK
and packaged the native engine, MCP App and both agent skills. VERSION remains
`0.1.0-preview.1`; no public tag/release was created. Outputs stay under
`build/packages/`: the portable core, ordinary plugin ZIP and a separate
installation source configured for the existing Documents/Agent3DCAD workspace.
The ordinary generated plugin keeps the product default workspace.

Installed through this app's bundled Codex CLI (`0.162.0-alpha.2`), preserving
the old versioned source installation. New source:
`~/Applications/Agent3DCAD/chatgpt-plugin-9f79efa-20261009`; installed cache:
`~/.codex/plugins/cache/agent-cad-local/agent-cad/0.1.0-preview.1`.
The CLI requires removing the existing same-name marketplace registration before
registering a different local source; only that registration was replaced.
The plugin is enabled, no duplicate standalone MCP entry was added, and parsed
comparison verified all unrelated configuration unchanged. The original config
is backed up with mode 0600. All **34 model files** across the two existing
Documents project folders stayed byte-identical through installation and host
verification.

Executed macOS arm64 evidence:

- Incremental native build and fresh portable installation succeeded. The clean
  build of the same native source passed **56/56 CTest suites** in the cleanup
  entry below; that full run was not repeated for this packaging-only task.
- Unmodified `tests/plugin_smoke.py` passed complete provenance, empty-PATH
  MCP/viewer discovery, create/edit, restart/reopen/history and STEP/STL/PDF/SVG/
  four DXF exports in an isolated temporary workspace.
- Prepared source, installed source and actual plugin cache passed their complete
  **307-file** inventories. Native executable SHA-256:
  `3281c67cc82abdcf678b7fa88e89ed992e38e689a241afeebdf47383c2840a35`.
  Both installed skills match their repository sources exactly.
- The app's bundled runtime independently returned the enabled installed plugin,
  `agent-cad:native-cad` and `agent-cad:setup`, and **28 tools** associated with
  `pluginId=agent-cad@agent-cad-local`. The installed service also passed actual
  workspace startup with empty PATH and loader overrides removed; its viewer
  resource SHA-256 matches provenance:
  `0d22ccf8034f22011c6c0dc2f8fb97b862311bd80da9053092b60eecf67dfdf3`.
- Actual connected tools in this chat returned `cad_list` and `cad_open`
  successfully. View `chat_01a1207b_install_9f79efa` rendered inside the app;
  DOM inspection and a screenshot show its Models menu listing all five saved
  projects at their prior revisions. This establishes live library rendering,
  not a new geometry selection/edit/export acceptance journey.

Evidence, config backup and exact verification scripts are under
`.local/evidence/plugin-install-9f79efa-20261009/`; `library.jpg` records the actual
embedded library. This is a local macOS build with ad-hoc signatures, not
publisher signing/notarization or additional 1.0 host/platform certification.
Continue using this chat's view for project selection; complete remaining host
journeys and delivery/signing gates before a stable release.

## Repository and local artifact cleanup — 2026-10-09

Removed all 13 historical root `build*` directories after preserving their
saved data, then configured and compiled one fresh native `build/`. Removed
superseded `.deps/hlr-sdk`, `.deps/portable-sdk`, `.deps/occt` and unpacked
`.deps/freetype-2.14.3`. Retained the exact patched `.deps/hlr-streaming-sdk`,
local JSON 3.12.0 source, pinned source archives and MCP SDK developer environment.
The ignored local CMake preset now selects that SDK and its FreeType prefix;
the previous preset is archived. Installed Applications/plugin copies and user
Documents workspaces were outside the cleanup scope.

Preserved **91 complete local workspaces with 221 document histories**, along
with historical logs, reports, screenshots, bundle provenance and installation/
configuration backups. Workspace contents were moved to `.local/workspaces/`
and evidence to `.local/evidence/`, each retaining its original relative path.
All **13,160 preserved files** passed SHA-256/size verification during the move
and again after deletion. The inventory, mappings and hashes are in
`.local/cleanup-20261009.json`; the procedure is archived at
`.local/evidence/cleanup-20261009.py`. `.local/` is owner-only (0700) and ignored
by Git. No model document, revision or saved workspace file was rewritten.
Historical absolute paths in derived files may require regeneration when
reopening a relocated workspace.

Removed the seven tracked temporary `.superpowers/brainstorm/` files; retained
its three HTML mockups in the local evidence archive. Added ignore rules for
temporary design sessions, `.local/` and accidental in-source CMake output.
Removed generated desktop UI/schema output and platform/Python caches. The
maintained [repository/documentation guide](README.md) maps source ownership,
local output and feature guides. Current development instructions and the viewer
harness use `build/`; historical paths in older handoff entries/plans continue
to identify the original runs and now map through the local archive.

Validation (macOS arm64 only):

- `cmake --preset local` configured exact OCCT 8.0.1 and FreeType 2.14.3.
  `cmake --build build --parallel 4` succeeded. The clean build emitted five
  existing `std::filesystem::u8path` deprecation warnings in `slicer_fixture.cpp`;
  no native source or geometry assertions were changed.
- `ctest --test-dir build --output-on-failure --parallel 4`: **56/56 passed in
  70.82 s**, including performance geometry and desktop asset packaging checks.
- The actual viewer harness started with no executable argument, using its new
  `build/agent-3d-cad` default; its temporary service/workspace were cleaned up.
  Loopback startup required sandbox escalation. This is a developer-harness
  check, not new browser/desktop-host acceptance.
- **99 local Markdown links** in the updated navigation/development/viewer docs
  resolve; `node --check`, ignore-rule probes and `git diff --check` passed.
  Build/CTest logs are in `.local/evidence/cleanup-validation/`.
- Checkout allocation fell from **13.49 GiB to approximately 0.84 GiB**, including
  the rebuilt native tree, retained dependencies and preserved local data.

Next: use `build/` for native work and keep lasting workspaces/evidence outside
it. Product next tasks and the outstanding 1.0 host/signing gates remain those
recorded below and in RELEASE_1_0.md; this cleanup adds no platform certification.

## Shared standalone/plugin UI and installed update — 2026-10-08

The user required the redesigned UI in both the embedded plugin and standalone
app. Both already assembled the same `web/` sources, but the standalone release
binary and generated frontend were still dated October 7. Rebuilt the Tauri
release and installed a verified native/desktop bundle at
`~/Applications/Agent3DCAD/desktop-shared-ui-02598ad-20261008`, with
`~/Applications/Agent CAD.app` pointing to its app. Prior bundles remain intact.
VERSION remains `0.1.0-preview.1`; these are local working-tree builds, with no
new tag or public release.

Implemented a guard against mismatched UI packages: the standalone binary's
developer-only `--print-viewer-html` prints its compiled frontend without
starting a window/service. Desktop packaging now checks that frontend against
the reviewed current source and native bundle's embedded-resource hash before
writing an output bundle. Only removal of the MCP CSP meta tag is permitted;
Tauri retains its existing IPC-specific security policy. Desktop provenance
records both frontend and MCP resource hashes. A real native/Rust integration
test compares the service's `resources/read` HTML with the compiled standalone
frontend; CTest `desktop_assets` rejects stale native assets, stale desktop
assets and unavailable frontend diagnostics before bundle writes.

Installation and verification:

- Warning-free native build and offline locked Rust release build succeeded.
  **3/3 Rust integration tests passed** in 2.18 s, including UI parity,
  saved-project reopen and all desktop export formats.
- **7/7 existing UI CTest suites passed** in 5.15 s. The final combined run
  again passed those seven, but the new packaging fixture initially failed
  because macOS resolves `/var` to `/private/var`. Canonicalizing the fixture's
  temporary root fixed the path assertion; unchanged packaging checks then
  passed **CTest desktop_assets 1/1**, with all four acceptance/rejection cases.
  `git diff --check` passed. No geometry assertion was changed.
- Complete provenance verification passed for **549 desktop-bundle files**
  and **307 installed plugin files**. Installed native executables are identical
  (SHA-256 `71a9c0dce111f472fa54f603a52521134f317adc0e4811c018c72ca554057177`).
  Both independently returned 28 tools and identical MCP HTML (SHA-256
  `0d22ccf8034f22011c6c0dc2f8fb97b862311bd80da9053092b60eecf67dfdf3`). The actual
  installed desktop binary returned the same HTML with only its host CSP
  difference (SHA-256
  `bd36960dd3e20fb7e0197d284a113080ce9144b1b066a70afb8219e2772ff29d`). Its executable
  hash is `589918875133e6bf462cd097ef2f61b24fe9596c9be54a71c692d194ac166a72`.
  The installed app passed `codesign --verify --deep --strict` for its ad-hoc
  signature; this is not publisher signing/notarization.
- Removed only the disabled legacy `[mcp_servers.agent-3d-cad]` table, after
  preserving the exact config with mode 0600. Parsed comparison confirmed all
  unrelated settings unchanged. A fresh desktop runtime subsequently discovered
  **28 Agent CAD tools with `pluginId=agent-cad@agent-cad-local`**, resolving the
  name-masking diagnosis below. The running chat's already-loaded tool catalog
  still needs reconnection/restart; no embedded-host rendering claim is made.
- Actual standalone GUI displayed the redesigned floating project pill, Scene
  card, tool dock, cube and selection bar, connected to the bundled service,
  opened Documents/Agent3DCAD, and followed a committed edit. Installation itself
  preserved all **27 existing model files** across both user project folders.

While checking the live standalone UI, the user requested a selected edge round
and explicitly chose **2 mm**. Applied the packaged native-cad skill through the
installed native CLI because the current chat lacked the refreshed MCP catalog.
Read `viewer_loop_plate` revision 2, resolved its actual evaluation-qualified
`edge-4` to a unique 39 mm line, previewed a 2 mm selective fillet, and committed
`side_round` with `expected_revision:2`. **Revision 3** is valid, one solid,
60 × 40 × 12 mm within tolerance, 10 faces/22 edges, volume
28,743.33745933181 mm³. The actual open GUI refreshed to revision 3 and
`cad_context` confirmed the obsolete selection cleared. Of the original model
files, only this document's `HEAD.json` changed; a new revision was added and
prior source history remains intact.

Evidence and configuration backup are in `build/shared-ui-20261008/`; build/test
logs are `build-app-protocol/shared-ui-*`. Remaining scope: reconnect the current
chat and verify embedded rendering in the real host. This increment was macOS
arm64 only; Windows/Linux and the broader 1.0 host/signing gates remain open.

## Latest local desktop plugin installation — 2026-10-08

The user requested the latest Agent CAD plugin in this desktop app. GitHub
`main` and the checkout both resolved to
`02598add39edda596f3f227785e4ab7d4fe1a9d9`, including the viewer-shell redesign.
Rebuilt `build-app-protocol` with the existing `.deps/hlr-streaming-sdk`
(OCCT 8.0.1 / FreeType 2.14.3), installed a fresh portable core, and packaged
the native Agent Plugins bundle. VERSION remains `0.1.0-preview.1`; this is
a local build of current source, with no new release or tag.

Installed and enabled `agent-cad@agent-cad-local` through the Codex plugin CLI.
Its preserved source is
`~/Applications/Agent3DCAD/chatgpt-plugin-02598ad-20261008`; the CLI returned
`~/.codex/plugins/cache/agent-cad-local/agent-cad/0.1.0-preview.1` as its installed
copy. Both trees passed their complete **307-file** provenance inventory.
Installed executable SHA-256:
`71a9c0dce111f472fa54f603a52521134f317adc0e4811c018c72ca554057177`.

The local installation adds an explicit `--workspace` override for the existing
`~/Documents/Agent3DCAD` folder and updates that local package's `mcp.json`
provenance hash. The ordinary generated archive retains the product's default
workspace configuration. The prior standalone MCP entry is disabled, with its
command/arguments retained for rollback. The pre-install configuration is backed
up with mode 0600; parsed comparison verified that unrelated settings were
preserved. Previous native installations and both project folders remain intact.

Executed macOS arm64 checks:

- Warning-free `cmake --build build-app-protocol --parallel 3` succeeded.
- **13/13 CTest suites passed in 15.72 s**: installer, embedded_assets, notices,
  cli_smoke, app_protocol, live, live_mcp_flow, webgl_renderer, viewer_shell,
  live_ui, export_ui, artifact_retarget_ui, and performance_geometry. The last
  suite passes with this selected streaming SDK; this does not diagnose the
  other SDK's failure recorded in the viewer-shell section below.
- Unmodified `tests/plugin_smoke.py` passed on the ordinary generated package:
  complete provenance, empty-PATH native MCP/viewer discovery, create/edit,
  process restart/reopen/history, STEP/STL/PDF/SVG and four DXF outputs.
- The actual installed copy, using its actual configured workspace and empty
  PATH with loader overrides cleared, passed initialize, 28-tool discovery,
  embedded resource read, `cad_list`, and `cad_open` for a library view.
  All five existing projects were listed at their prior revisions. The viewer
  contains the new shell, with SHA-256
  `0d22ccf8034f22011c6c0dc2f8fb97b862311bd80da9053092b60eecf67dfdf3`.
  All **27 existing project files** across Documents/Agent3DCAD and
  Documents/Agent CAD remained byte-identical; verification created only a new
  live library view in the configured workspace.
- The desktop's own bundled runtime (`0.162.0-alpha.2`, app
  `26.1002.52244`) independently reports the plugin installed/enabled, recognizes
  `agent-3d-cad`, and loads `agent-cad:native-cad` and `agent-cad:setup`.

Evidence, install/config backup, runtime reports and exact commands are in
`build/chatgpt-install-20261008-02598ad/`. The installer verification uses the
cache path returned by the CLI; it is versioned here rather than the `local`
cache path described in public documentation. A first desktop-runtime check
was blocked by the filesystem sandbox, then succeeded with installation-scoped
escalation. No auto-review rejection occurred.

Next: restart this desktop app and start a fresh chat, then run the plugin setup
to open saved projects. The running chat retained its old MCP tool catalog.
Computer Use denied access to `com.openai.codex`; neither its safety restriction
nor the host UI was bypassed. CLI/package/runtime verification above does not
establish in-chat rendering or the complete 1.0 host journey. Existing
ChatGPT/Claude release gates in RELEASE_1_0.md remain open.

Follow-up UI/connection diagnosis (2026-10-08): the desktop restarted at
22:48 UTC, but this chat still has no Agent CAD tools. Current desktop logs
repeatedly report `mcp_extension_server_tools_empty` for `agent-3d-cad` with
`pluginId=null`. A fresh desktop-runtime `config/read` and
`mcpServerStatus/list` independently show the disabled October-7 standalone
entry, the enabled local plugin, and an Agent CAD inventory with zero tools and
no plugin ID. The installed plugin executable still matches the SHA-256 above.
No Agent CAD service process was running during the check. This establishes a
host connection/configuration problem; restarting alone did not resolve it.
The same-name legacy entry may mask the plugin connection, but removal and
successful in-host discovery/rendering have not been tested. No host settings
or model files were changed during this diagnosis. Next: resolve the legacy
entry/plugin precedence and verify actual `cad_list`, `cad_open`, and the new
shell in the desktop host before claiming the installation journey complete.

## Viewer shell redesign — Shapr3D-style layout, selection and navigation — 2026-10-08

Sub-project A of three (spec `docs/superpowers/specs/2026-10-08-viewer-shell-design.md`,
plan `docs/superpowers/plans/2026-10-08-viewer-shell.md`). The embedded and Tauri
viewer's `web/` UI is rebuilt; **no protocol change** and no native code change.
`cad_context` and `cad_resolve_selection` see the same face/edge references as before.

Behavior now implemented:

- **Layout.** The canvas fills the viewer; controls float over it: project pill
  (Models menu, revision, connection), Scene card (Features, Parameters, a Parts
  tab for assemblies), Export menu, orientation cube with Fit, a right-hand tool
  dock (Visual inspection, Colors, Review notes, Saved views, Exact section,
  Measure, Sequences, Motion, Source) and a bottom selection bar that carries
  Quick Edit. Existing section elements keep their IDs and `app.js` logic; they
  are moved into one popover at a time. A dock icon shows only while its panel is
  not `hidden`. Breakpoints 900/560 px (`layoutMode`): Scene card open / chip,
  dock icons / a single Tools button; the viewer still works at 360 px.
- **Unified selection.** No Faces/Edges switch. `CadRenderer` mode defaults to
  `auto`: a visible edge within 6 px wins when exactly one is nearest, else the
  face. Hover highlights (face wash, thicker edge); click selects; empty click or
  Esc clears. `setMode('face'|'edge')` remains. Read-only artifacts use the same
  rule across curves and mesh groups.
- **Navigation.** Left or right drag orbits (right-drag used to pan), middle or
  Shift+drag pans, wheel zooms toward the cursor, double-click frames an entity or
  fits, Space frames the hovered/selected entity, digits 1–7 pick standard views.
  Camera moves animate ~250 ms (instant under `prefers-reduced-motion`) and save
  once at the end. Fit/reset/framing centre in the area the floating chrome leaves
  uncovered (`setInsets`). The orientation cube is six real buttons in a CSS 3D
  scene that follows the camera (`onView`); click a face or its border for
  face/edge/corner views, drag to orbit, double-click for iso.
- **Look.** Light studio by default; dark when the host context reports
  `theme: dark`, else `prefers-color-scheme`, switched live. The gradient
  backdrop is a WebGL2 pass (attribute-less, `gl_VertexID`) so Save PNG matches the
  screen; WebGL1 keeps a solid theme-coloured clear. Edge lines are dark in both
  themes because the model is light in both. Stored default model colour is
  unchanged.

Exact evidence (macOS arm64, Node v26.8.1, build `build-package`, branch
`viewer-shell-shapr3d`):

- Node suites: `webgl_renderer` **106 checks** with the native binary (26 new
  renderer tests: camera math, auto pick, hover, hover back-off, navigation input,
  animation, theme, insets, onView), `viewer_shell` **5**, `live_ui` **325**,
  `playback_ui` **51**, `export_ui` **110**, `artifact_retarget_ui` **34**,
  `offline_renderer` **30**, `desktop_bridge` exit 0. The unmodified baseline
  counts were 325/51/110/34/30.
- CTest: **55 suites, 54 pass** (73.10 s, `-j4`); this includes `embedded_assets`,
  `app_protocol` (byte-for-byte embed check including `shell.js`) and the new
  `viewer_shell`. After merging `origin/main` (`a173265`, the CI portability repairs
  below) the rebuild was warning-free and the result identical: **55 suites, 54 pass
  in 81.20 s**, same single failure, `web/` untouched by the merge.
  The failure is `performance_geometry`. It is not caused by this work: it fails
  identically (exit 1, same output) when built from the base commit `8a8c668` in a
  clean worktree, in this build environment (`.deps/portable-sdk`). The CI Linux lanes
  and the local `build-app-protocol` run recorded in the next section ran all 54
  native tests at that point, so this looks specific to this SDK or environment. It
  is a native drawing/HLR test whose output says streaming-root checks need the v2
  patched SDK and only projection regressions run otherwise; this SDK may not be
  that one. The failing assertion was not investigated.
- Embedded `viewer.html` SHA-256 (local macOS arm64 build):
  `0d22ccf8034f22011c6c0dc2f8fb97b862311bd80da9053092b60eecf67dfdf3`. This changed
  because the viewer changed; no cross-platform equality is claimed until CI rebuilds.
- Hover cost, `pick(…,'auto')` at 1000×700, 1,000 positions per model: plate
  (1,060 tris) mean 0.23 / p95 0.39 ms; assembly (872 tris) 0.29 / 0.42;
  duplex-35-sprocket (5,264 tris, 1,210 edges) **1.94 / 2.34, max 5.19 ms**. The edge
  test dominates. Past 8 ms a pick makes hover back off for 4× that time (tested
  with an injected clock; not exercised on a model that large).
- Real-browser evidence (dev-only harness `tests/viewer_shell_harness/`: the
  assembled viewer in an MCP Apps host page backed by the **real native service**,
  real GPU, the Claude desktop in-app browser). Seen working: light and dark (live
  switch through `host-context-changed`); widths 360, 560, 790 and a scaled 1280;
  hover and click selection of faces and edges (edge pick returned
  `edge-18`, 30 mm; face pick Area 1,060.365 mm²); the selection bar and Details;
  Scene card and Parts tab; Models, Export, Visual inspection (clip toggled),
  Motion and Source popovers; Esc closing with focus returned to the dock icon;
  the compact Tools button; a real STEP export through the job service (path shown
  in the request-text card); read-only artifact review (STEP) with Source,
  `{kind:"mesh_group", entity_id:"artifact-1"}` and Export hidden; cube face
  click to Top; real wheel zoom; double-click-to-fit. The harness caught a real
  bug the mocks could not: edge lines were invisible with the first light palette.

Limits, stated plainly:

- **Not verified in real ChatGPT or Claude Desktop hosts**; the host-context
  `theme` field is read defensively and the OS fallback is kept. The Tauri window
  was not built (`desktop/build.rs` lists `shell.js`; `cargo` was not run).
  Windows/Linux not run.
- Right-drag, middle-drag, Shift+drag, Space and the digit keys are covered by
  tests with synthetic events only; they were not driven with a real mouse. The
  Colors, Review notes, Saved views, Exact section, Measure and Sequences popovers
  use the same mechanism as the ones seen but were not individually opened; keyboard-only
  traversal and screen-reader behavior were not exercised. Width 900 was not
  viewed directly.
- The offline `cad_view` viewer (`src/viewer.cpp`) is a separate page and was not
  restyled. Single selection only; no perspective projection; no multi-touch.
- Compact-mode Tools list scrolls under a height cap rather than reflowing.

Decisions: `shell.js` is the only new asset (the asset list is hard-coded in four
places; all list it). `tests/export_ui_tests.mjs` anchors its end marker on the
statement after the export handler because the previously-anchoring mode-toggle
loop was removed; the sliced handler text is byte-identical to the previous commit.
Popover wrappers are separate from panels because `app.js` toggles each panel's
`hidden` on every update. Uncommitted browser mockups live in `.superpowers/` (not
ignored).

Next: sub-project B (contextual edit toolbar: direct fillet/chamfer with live
`cad_preview`, hole placement, agent handoff for the rest; the selection bar keeps a
slot for it) and C (persistent face selectors plus a native face push/pull feature,
so Extrude becomes direct). Perspective projection and multi-select are separate.

## CI portability repairs — 2026-10-08

Investigated [run 37833296104](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37833296104)
at `8a8c66866b0283d4f2ba6999db88ee8f68ef1015`. Both macOS lanes stopped compiling
`artifact_common.cpp`: floating-point `std::from_chars` requires macOS 26, while
CI explicitly targets macOS 15. A local macOS-15 compile reproduced that error
and the same problem in `gcode.cpp`. Both now use one strict decimal parser with
a private C numeric locale, POSIX/Windows conversion implementations, complete
token consumption, finite-range validation and rejection of underflow rounded
to zero. Representable subnormals and signed zero remain accepted. Artifact
coordinate-list tokenization explicitly uses the classic locale. Dependency
pins, the macOS deployment target and public tool contracts are unchanged.

Both Linux lanes passed all 54 native tests, then failed the relocated-package
history check. In the standalone CMake script, unset CMP0054 made the quoted
literal `"angle"` resolve to a variable from an earlier fixture. Setting the
script's minimum CMake version to the project's existing 3.24 baseline fixes
the comparison without changing its assertions. The original script reproduces
the exact CI failure under CMake 3.31.6; the repaired complete package workflow
passes under both CMake 3.31.6 and 4.3 with empty PATH and cleared loader overrides.

The original Windows lane subsequently passed native tests but failed the
independent schema fixture's slicing-package inventory check. Slicer run manifests
used native backslashes for recursively discovered nested paths. The writer now
uses generic UTF-8 paths with forward slashes; native execution/output paths
keep their existing OS representation. The native slicer verifier now rejects
backslashes and explicitly requires nested profile/reviewed-plan/G-code entries.
The independent Python inventory assertion is unchanged.

Executed macOS arm64 validation:

- At `c4f029d`, warning-free `cmake --build build-app-protocol --parallel 3` and **54/54 CTest
  suites passed in 204.56 s**, including **135 artifact** and **79 G-code** checks.
  New regressions cover decimal syntax, locale independence, signed/fractional
  G-code words, overflow, underflow, signed zero and representable subnormals.
- **899 JSON Schema checks across 28 tools** and **2,931 official MCP SDK 2.3.0
  interoperability checks** passed with the explicit native slicer fixture.
- Full `cad_core` compilation in `build-ci-macos15` with
  `CMAKE_OSX_DEPLOYMENT_TARGET=15.0` passed without warnings. Direct syntax checks
  of all three changed parser sources passed for macOS 15 arm64 and x86_64.
  This is compilation evidence; the local SDK is not a macOS-15 runtime trial.
- Complete relocated bundle smoke passed with each CMake version above;
  `git diff --check` passed. Logs are `ci-fix-*` in `build-app-protocol` and
  `build-ci-macos15/ci-fix-build.log`.
- After the Windows manifest repair, the warning-free rebuild and **304 native
  slicer checks** passed in 17.19 s, along with **883 schema checks across 28
  tools**, **3,268 MCP SDK checks** and CMake 3.31.6 relocated bundle smoke.
  A concurrent local schema run hit a motion-job deadline; an isolated rerun
  passed with unchanged assertions. Logs are `ci-fix-slicer-*`.

The repair is in [draft PR #4](https://github.com/cfaulkingham/agent-3d-cad/pull/4)
on `codex/fix-ci-macos15-parsing`. [PR run 37834814433](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37834814433)
at `c4f029d` passed both macOS and both Linux lanes; Windows reproduced the
manifest inventory failure. A new five-platform run must validate the Windows
repair before merging. Earlier public
release/host-installation gates remain as recorded in RELEASE_1_0.md.

## Combined implementation acceptance — 2026-10-08

All seventeen software increments in COMPOSITION_FABRICATION_REVIEW.md are now
merged locally: nested/shared assembly composition and components, dependency
caches, manufacturing/process/purchased-part/G-code/slicer/printer workflows,
and live presentation/measurement/sections/colors/notes/playback/external review.
Combined software acceptance is complete on macOS arm64, including the final
rendered browser and compressed-artifact relocated-package checks.
The entries below are historical checkpoints; their active-goal and next-task
statements are superseded by this current section.

Final macOS arm64 executable SHA-256:
`ff9b5c479721dbea18517b76015aea69765846531889dc8211b9ea529c4cf6ef`.
It uses the selected patched OCCT 8.0.1 SDK and FreeType 2.14.3. Clearing cached
FreeType locations fixed an initial loader-path conflict with an older SDK;
the final executable has only the selected SDK on its build loader path.
The final build emitted no warnings. No dependency version was relaxed.

Executed combined acceptance:

- All **54 CTest suites pass in 113.24 s**, including 115 artifact-native checks,
  58 real artifact MCP/controller checks, 34 cross-feature retarget checks,
  68 printer checks and 39 POSIX publication-failure checks.
- **929 independent general JSON Schema checks across 28 tools**, with the
  explicit native slicer fixture, and **197 actual artifact schema checks** pass.
- Official MCP SDK 2.3.0 passes **3,317 general** and **498 artifact** checks,
  including automatic restart and legacy lifecycle behavior.
- Relocated package smoke passes with empty PATH and cleared loader overrides;
  pinned TinyXML2 source/license bytes and dependency provenance are verified.
  The relocated binary parses an actual deflated 3MF, preserves centimeter and
  nested-component transforms, reparses a moved package after original deletion,
  transfers read-only mesh/context, rejects native actions, and restores the
  unchanged native source. No developer runtime is used by these workflows.
- Actual compact discovery is **427,829 bytes**, below the unchanged 430,080-byte
  gate. **27,169 independent compaction checks** compare the actual before/after
  catalogs, preserve all constraints and literals, and protect recursive graphs,
  reference siblings, pointer targets and schema scopes. No tool was removed.
- The bundled native-cad skill passes skill-creator quick validation.

Exact commands/logs are `final-acceptance-*` in build-app-protocol; CTest per-suite
output is Testing/Temporary/LastTest.log. The durable independent compaction
check is tests/discovery_compaction_check.py. `final-acceptance-evidence.json`
records exact argv, binary/catalog/log/image hashes and the uncommitted source
manifest. Independent package review verifies all 299 listed file hashes,
29 Mach-O closure files, pinned parser sources/notices and matching FreeType/HLR
code despite the package loader/signature rewrites. The initial combined run failed
only the catalog gate (two suites) and stale SDK loader selection; the final
run above resolves all three failures without weakening acceptance assertions.

The actual final embedded viewer runs from a pinned copy at
`http://127.0.0.1:61032/`, isolated from earlier demos. A long right-edge label
leaves numbered pin 5 visible. Reload produces an identical PNG and restores
the camera, four retired notes and one current evaluation-qualified note.
Fitted example `build/final-review-demo-20261008/capture-003.png` is
1,648 × 1,248, 118,563 bytes, SHA-256
`a1e71fe1f349160d12987244adc1cec5b3938386449fe6425c8739b117683e0d`.
Labels shorten to 80 characters; complete text remains in structured context.
A separate actual browser view admits 23 presentation-clock seeks, then artifact
adoption stops further seeks, clears native notes/sequences/playback/picks and
uses full-hash review-local picks. Returning through the model library restores
native controls. Both copied source HEADs stay unchanged at
`3e6695b526b0e0e47d2fea0313f56108e540fd3eb799d6e3302f25727fdcb7fa`.
The final browser report is `build/final-review-demo-20261008/ACCEPTANCE.md`.
Earlier playback evidence separately covers coordinated native joint motion;
the final transition sequence exercises presentation frames only. Images and
requests went solely to a local test host, never another recipient.

Preserved review deliverables also include the 9 mm assembly example at
`http://127.0.0.1:65469/`, exact sections at `http://127.0.0.1:56183/`, appearance
at `http://127.0.0.1:62122/` and the original annotation phase at
`http://127.0.0.1:58352/`. Their earlier binaries/workspaces/evidence are unchanged.
The final fitted example is marked as a browser deliverable. The compiled
software increment is complete; the next product work is the separate release
acceptance below, not another planned feature from this implementation scope.

Printer plans remain offline; no hardware was contacted. Package publication can
precede durable result storage: a storage failure may leave an inspectable package
and a new request may publish a duplicate. The deterministic POSIX fixture
re-verifies that package and preserves source/prior outputs; it does not establish
coordinator-crash or Windows fault-injection recovery. Unsupported process checks,
external format features and physical readiness remain explicitly unknown or
unsupported. New Windows/Linux execution, external cross-vendor STEP qualification,
ChatGPT/Claude installation trials and publisher signing/notarization remain
release gates in RELEASE_1_0.md. The user subsequently requested committing and
pushing this validated increment on `main`; no public release is part of that
publication.

## Printer handoff and combined review integration — active larger goal, 2026-10-08

The fourteenth increment implements `cad_printer_handoff`, the 26th tool, through
the shared Service/MCP/CLI and durable jobs. Explicit checksummed plain G-code,
resolved profile snapshots and printer/review assumptions produce a portable
offline plan. Re-verification checks the contained byte ledger and recomputes
native findings/readiness. Source association remains caller declared; native
upload/start are unsupported, physical readiness remains fail or unknown, and
no hardware is contacted. Planning, failure, running cancellation, timeout and
historical/relocated verification preserve source and prior outputs.

Executable SHA-256
`c39917d26946d4442565b9537e2d59f1c31d28e98e2ff984d26b20cc356af4b9`
passed **68 dedicated printer checks**, **807 independent schema checks across
26 tools**, **3,552 official MCP SDK 2.3.0 checks**, and the relocated empty-PATH
bundle including moved-package re-verification. The full 46-suite run passed 45
suites in 114.98 s; its sole failure was the old live fixture's 25-tool assertion.
After correcting that assertion to require 26 tools and explicit printer
presence, live passed in 11.18 s. Logs are `printer-final-*`,
`printer-native-final.log` and `printer-live-final.log` in build-app-protocol.
The bundled native-cad skill passes skill-creator's quick validation.

Independent review identified an artifact-only recovery limitation: package
publication precedes job-result storage, so storage failure or interruption can
leave a package without a successful job result. Inspect exports and re-verify
before retrying; source-mutation receipts do not recover these offline packages.
PRINTER_HANDOFF and PROTOCOL document the possible duplicate retry. A deterministic
POSIX publication-barrier fixture is now integrated; three isolated repetitions
passed 40 checks each in 6.59 s against private pinned executable
`dad0c70e4fe2e4637754715b724457245fc6664cae8cb3449f78812f6c07be5c`.
That fixture injects result-storage failure, not a coordinator crash; Windows
fault injection was not executed. The next main regression run includes it.

Annotations and declarative playback are now merged locally. Their combined
native/controller/renderer/actual-MCP tests pass, including retirement of an
original note when actual joint playback changes the pose. Combined discovery
currently measures 436,020 bytes and fails the unchanged 430,080-byte gate;
lossless schema compaction is in progress. Full combined schema/SDK/package
acceptance remains pending. External-artifact integration remains isolated.

Actual browser tab 18 at `http://127.0.0.1:58352/` runs pinned executable
`dad0c70e4fe2e4637754715b724457245fc6664cae8cb3449f78812f6c07be5c`
against isolated `annotation_review`. Native leaf bounds and an annular-face
inspection center produce numbered pins; hidden owners omit their pins while
retaining notes. Reopening restores them. A deliberate radius edit creates r2
and retires all three original anchors without rebinding. Historical text edits
preserve the original anchors; a new r2 pin restores after reload. Rendered PNGs
and all current/retired source context were captured by the local test host.
The r2 PNG is 1,314 × 1,364, 105,084 bytes, SHA-256
`46c23bdfff0e5ead481bbcf6f190d9b13bbdc581c9a020034e331a69e86f0f0e`.
Evidence and exact native contexts are in `build/annotations-demo-20261008`.
No request went to a real chat or another recipient. This local developer
preview does not certify desktop-host installation or other platforms.

The larger goal remains active. Finish schema compaction, combined acceptance,
playback browser evidence and external-artifact integration/review. No printer
contact, physical start, public release or new commit/push occurred.

## Appearance, saved views and rendered image context — active larger goal, 2026-10-08

The thirteenth increment implements opaque default/per-leaf RGB colors, native
saved camera/presentation/visibility/appearance presets, a local PNG download
request and source-qualified image context. Settings remain review metadata;
they never change source geometry, physical materials, exact measurements or
manufacturing exports. Current owner checks are strict, removed leaves prune on
refresh, retarget resets, and delayed responses cannot restore an older local
choice or source. Preset apply clears picks and retires sections only when their
plane/explosion changes. A stale historical context remains observable while a
current preset response uses current revision/evaluation/feature identity.
See APPEARANCE.md, PROTOCOL.md and the bundled agent guidance.

Main executable SHA-256
`353e7380eed2eb5140f00f36493a6b2efa8f36b2f65969b9d2fb89ce8458b650`
passed **all 45 CTest suites in 102.66 s**, **786 independent JSON Schema checks
across 25 tools**, **2,976 official MCP SDK 2.3.0 checks**, and a relocated
empty-PATH runtime package. Controller coverage is **285 checks**. The existing
real-MCP/controller suite now includes 12 additional color/preset/section/draft/
retarget assertions. All discovery constraints and the 430,080-byte size gate
remain unchanged. Logs are `appearance-main-*` in build-app-protocol.

Actual browser tab **17**, `http://127.0.0.1:62122/`, runs the compiled embedded
app with a pinned private executable and isolated `appearance_review` source.
A blue default and red left housing render with distinct orange native caps.
After resetting colors, changing camera and hiding the left subassembly, applying
“Colored exploded section” restores all four choices and retains the same section
job/area. A cold native-host restart restores the settings, preset and qualified
section. Saving the same name replaces its fitted camera without adding a second
preset. Review operations preserve raw source HEAD.

The actual WebGL PNG was captured through Include this view into the **local
test host**, together with source/evaluation/job/color context: 652 × 1,364 pixels,
42,097 bytes, SHA-256
`93ec42e3ff8356796545bc2101110d3b866d5cd78d178a146a2462b9596e0da2`.
Requests, native contexts, screenshots, PNG and `evidence.json` are under
`build/appearance-demo-20261008`. No message went to a real chat or another
recipient. Save PNG image requested a download, but its browser download observer
timed out; an actual saved download file is not established. One browser
MutationObserver TypeError had no identified source. This local development
exercise does not certify desktop-host installation or other platforms.

The larger goal remains active. Printer Service/jobs/package integration is now
in progress; annotations, declarative playback and external-format review remain
in isolated sub-agent workspaces. No printer was contacted and no new commit,
public release or hardware start occurred.

## Exact sections and rendered review — active larger goal, 2026-10-08

The twelfth increment now includes native hole-preserving caps, source-qualified
section jobs, bounded cap rendering and read-only picking, and the live exact
section controls. Native plane/explosion changes retire the result; reversing
the kept side, camera changes and hiding parts retain its measured coverage.
Distance measurement remains independent. Failed admission feedback now survives
empty sync until an explicit retry, clear or source change; uncertain mutations
are never repeated automatically. Existing source geometry and editable history
remain authoritative. See SECTIONS.md and the section-inspection example.

Executable SHA-256
`b5ecefc219156f47678f33fe036147b2d0d450c970f9ac17503ddca201806bc3`
passed **all 45 CTest suites in 110.00 s**, **757 independent schema checks across
25 tools**, **2,736 official MCP SDK 2.3.0 checks** and the relocated empty-PATH
bundle. The final admission-feedback fix produces executable
`52ee8a9fe9e421945e51e9628d3ecd87d718df6b132c84a96713dfe81e29ece9`:
**224 controller checks**, the four affected app-protocol/live-controller/real-MCP/
WebGL suites (**9.56 s**) and the relocated empty-PATH bundle pass. Renderer
verification includes **70 checks**; native section coverage includes **2,099**.
The bundle's first final poll encountered documented transient `workspace_busy`;
the fixture now retries only that read-only poll and preserves every assertion.
It never retries section admission. Logs use `section-delivery-*` and
`section-feedback-final-*` in build-app-protocol. Bundled skill validation and
`git diff --check` pass. Temporary Python/schema tooling is developer verification,
not a dependency of the packaged native runtime.

Actual browser tab **16**, `http://127.0.0.1:56183/`, uses the compiled embedded
app and a pinned executable in `build/section-demo-20261008`. Two nested annular
housings show filled orange cuts with open bores. A cap click gives read-only
feedback; a click through a bore selected the original left/base face. Native
clearance stays **16 mm** while reviewing sections. Kept-side reversal and hidden
subassembly restoration retain full area; plane/explosion changes retire it.
The **6.8 mm** exploded view aligns caps with the displayed plane. A deliberate
isolated `outer_radius:12→13` source edit commits revision 2, retires both old
results and changes exact annular area from **216π = 678.5840131753955 mm²** to
**266π = 835.6636458548853 mm²**. Revision-1 job history remains unchanged. Browser
reload restores camera, presentation and revision-2 section; copied request text
contains its source/evaluation/job identity. Raw HEAD stays unchanged by review
operations. Requests, native results, hashes, screenshots and `evidence.json`
are in the demo directory. The earlier user clearance example on tab 15 remains
separate and untouched.

This is local development rendering, not desktop-host installation acceptance.
No image was delivered to a real chat host in this section demo. A browser
console MutationObserver TypeError had no identified source; no origin or fix
is inferred. Windows/Linux execution and distribution/1.0 installation gates
were not repeated. The larger goal remains active. Appearance/presets and printer
handoff validation are staged separately; annotations, declarative playback and
external-format review are being implemented by explicitly authorized sub-agents.
No hardware action, new commit or release occurred.

## User-facing clearance example and initial section prototype — 2026-10-08

The requested example has its own `build/skills-demo-20261008/workspace`,
document `skills_demo` and view `skills_demo`. Two occurrences reuse one editable
30 × 20 × 3 mm plate source, with a separate radius-2 mm pin. Native clearance
measures **7 mm** at revision 1. A deliberate `upper_z:10→12` edit commits
revision 2, retires the old measurement and yields **9 mm**. The browser shows
that source-qualified result with **12 mm** visual explosion, no material
intersection, and a passing caller-defined 1 mm requirement. Native STEP/STL,
PDF/SVG/DXF drawings and a BOM (two plates, one pin) were generated at revision 2.
Raw HEAD remains unchanged by exports and final view operations. Original user
workspaces are untouched. Requests, results, HEAD snapshots, native provenance
and screenshots are in that demo directory; `evidence.json` records the actual
result, and `exploded-gap-9mm.jpg` is the final screenshot.

The retained browser tab is **15**, `http://127.0.0.1:65469/`; preview host session
**35157** serves actual embedded resources through the development loopback
harness. A private copy of executable SHA-256
`fd982a854317d1c9f30d1d7f3951d64e2bb7a954871f6ebe44cc09cbdf4e536b`
keeps its coordinator and geometry workers consistent during further builds.
This demonstrates local development behavior, not desktop-host certification.

The larger goal remains active. Native **section geometry is work in progress**:
`cad_measure` now has a closed `section` query for an explicit displayed-world
plane, optional exploded leaf offsets and optional leaf coverage. Per-solid
native intersections retain analytic curves, tangent contacts and material cap
faces with interior wires. Exact areas/perimeters accompany bounded derived
meshes; interfering solids retain separate caps and explicitly summed areas.
Source/evaluation/build qualification and durable jobs use the existing service.
This is not yet wired into a live section action, cap renderer or controls.

The current focused native run passes `live`, `measurement` and `section`,
including oblique cylinders, annular holes, point/curve tangency, nested paths,
exploded planes, unchanged source and historical jobs. Initial failures exposed
a wrong edit-fixture operation and the general scalar reader's 1e6 limit applied
to a declared 1e12 plane offset; both were corrected without narrowing the
contract. Distance-only live schemas/runtime are kept separate from unfinished
section presentation. Schema interning now handles identical nullable/union
subtrees and estimates actual reference costs, retaining all constraints.
**The discovery-size gate still fails: 435,022 compact bytes exceed the unchanged
430,080-byte limit.** `section-focused-third.log` records three passing suites and
the `app_protocol` failure. An independent schema-check attempt lacked
`jsonschema`; no schema conformance or complete 45-suite pass is claimed for this
prototype. The previous measurement increment's final 44-suite evidence below
remains historical evidence for its own binary.

Next: finish lossless discovery compaction, qualify section output with independent
schema/MCP checks, then implement revision-safe live section state, native caps,
hole-aware rendering/picking and actual browser evidence. Appearance/presets,
annotations, declarative sequences, external-format review and intentional printer
handoff remain in the full authorized scope. No printer action, auto-review
rejection, commit or release occurred in this increment.

## Exact measurements and live clearance review — active larger goal, 2026-10-08

`cad_measure` is implemented locally as the **25th tool**. It uses current native
source B-reps for face/edge/leaf pairs and up to 253 unordered pairs of 2–23
assembly leaves. Reports distinguish explicit pair, all-leaf and subset coverage;
they pin source/revision/evaluation/feature/build, provide closest-point witnesses,
supported acute analytic angles, common solid material volume and caller-defined
clearance findings. Contact does not imply positive-volume interference; containment
does. Clipping, explosion and visibility do not change source measurements.
MEASUREMENTS.md defines the closed schemas, tolerances and supported coverage.

Private evaluation metadata now qualifies the actual native build and source
hash. Missing, draft, changed-HEAD and mismatched source/build evaluations fail.
Face/edge recovery uniquely matches recorded native geometry descriptors within
the same qualified evaluation; enumeration indices are not stable names.
Missing or ambiguous matches fail at feature level. Computation stays in bounded
serial native workers, with cancellation/deadlines and source recheck under the
document writer lock. Qualified historical job results remain historical.

The embedded Exact measurement panel submits native durable jobs, chooses leaf
parts or current picks, polls results, and exposes the job/query in agent context
and Copy request. Reopening restores the current result; clear cancels pending
work and removes its view reference. New evaluations retire results/endpoints.
Version guards reconcile delayed sync and late replies. Polling an existing job
preserves newly chosen endpoints instead of resetting the next query's inputs.

The actual contact demo exposed a finite native candidate whose paired point did
not agree with the zero distance. Returned witnesses now independently qualify
finite coordinates, distance agreement and exact point-to-target support within
1e-7 mm. At most 16 raw candidates are examined; inconsistent candidates are
omitted with explicit rejected/truncation counts, and no qualified witness fails
the operation. No geometry is repaired or changed to manufacture evidence.
Translated plate/cylinder contacts and controller rejection fixtures cover this.

Executed locally on macOS arm64 with OCCT 8.0.1:

- **All 44 CTest suites passed against the final binary in 101.00 s**, including
  **260 native measurement checks**, **162 native live checks**, **5,449 app
  protocol checks**, **158 controller checks**, **40 native MCP live-loop checks**
  and **56 renderer checks**. Analytic fixtures cover diagonal distances,
  contact/containment/overlap, planes/lines/cylinders, nested paths, all 253 pairs,
  subset bounds, ambiguity, cold-cache recovery, cancellation, deadlines, source
  retirement and replay. Counts include variable polling.
  `build-app-protocol/measurement-ctest-delivery-final.log` records the full run.
- Independent **705 schema checks across 25 tools** and **2,458 official MCP SDK
  2.3.0 checks** passed (`measurement-schema-delivery-final.log`,
  `measurement-sdk-delivery-final.log`); these counts also include polling.
  Compact discovery is **426,626 bytes**, under the unchanged **430,080-byte**
  bound. Skill validation and `git diff --check` passed.
- The relocated **empty-PATH** native bundle measures the analytic spacer gap
  while presentation is exploded, preserves raw HEAD, ships MEASUREMENTS.md and
  embeds the reviewed controls. Existing manufacture/review/slicing and all
  earlier runtime smoke checks pass (`measurement-bundle-delivery-final.log`).
  Windows/Linux execution and desktop-plugin host acceptance were not repeated.
- Actual browser tab **14**, `http://127.0.0.1:61910/`, uses its own three-leaf
  workspace. Pair clearance is **7 mm** at revision 1, unchanged by a **12 mm**
  explosion. Two picked planar faces measure **10 mm / 0°**; a picked 3 mm edge
  against a plane measures **0 mm / 90°**. All three leaf pairs, including a hidden
  plate while clipping is enabled, yield **0 mm** minimum, **no material
  interference**, and fail the caller's 1 mm requirement because of contact.
  Two inconsistent native contact candidates are explicitly rejected; retained
  witness coordinates agree with the reported distance. Reopen restores the
  result, Copy request carries its native job/query, and clear removes only view
  metadata. These operations preserve raw revision-1 HEAD. A deliberate demo-only
  `upper_z:10→12` edit produces revision 2, retires the old result/endpoints and
  rejects direct old queries. The new result is **9 mm**; historical revision-1
  jobs remain qualified at 7 mm. Final view operations preserve raw revision-2
  HEAD. Existing user demos were untouched.
  `build/measurement-demo-qualified/evidence.json`, qualified contexts/job reports,
  copied requests, edit/rejection records and screenshots retain this evidence.
  `live-measured-gap-r2.jpg` shows the final source result beside exploded parts.
  Preview host session **44950** serves actual bundled HTML through a loopback
  protocol harness; it is not desktop-host certification. One accepted start
  encountered temporary workspace contention before its reply; polling restored
  the saved job without resubmission. Two MutationObserver TypeErrors had no
  source URL; neither app nor harness contains a MutationObserver, and their
  origin was not established. Two obsolete measurement preview hosts were stopped.

Earlier verification failures were corrected rather than weakening bounds: the
deadline fixture now expects the native `job_timeout` code, symmetry uses a valid
circular pattern, and build/test symbols use the actual private evaluation and
worker contracts. A broad run during the controller correction detected stale
embedded bytes; the final rebuild and final 44-suite run establish exact reviewed
asset provenance. The endpoint-retention regression failed before its fix and
passes afterward. `measurement-verification.json` records the final evidence.

Final executable SHA-256:
`c7ec086b5d13bc79ffbab37486cb898803f3b891d0028f2d453147db34d4e7b4`.
This is completed local progress within the still-active larger goal. Exact
section geometry/caps, appearance/presets, annotations, declarative time sequences,
external-format review and intentional printer handoff remain required by
COMPOSITION_FABRICATION_REVIEW.md. The optional printer-backend preference remains
unanswered and does not block viewer work. No hardware action or auto-review
rejection occurred. Release signing/install/host gates remain independent; these
increments are uncommitted and unreleased. Next increment: exact source-qualified
section curves/caps with native jobs and live revision-safe inspection.

## Live clipping and exploded inspection — active larger goal, 2026-10-08

The native live-view contract now accepts closed `presentation` settings through
`cad_viewer` context and returns current settings through ready sync and
`cad_context`. The embedded Visual inspection panel controls uncapped clipping,
axis/offset/kept side, exploded distance and reset. Explicit unit-vector direction
overrides target current leaf paths. Native state is independent of editable
geometry/history, preserves surviving settings across revisions, prunes removed
overrides and resets on retargeting. No additional MCP tool was added.

CPU picks and GPU rendering share displayed coordinates and the clipping half-
space. Clipped surfaces do not occlude picks; edge segments are clipped before
testing. Shared source vertices are separated by occurrence only when needed for
explosion, preserving original measurements and references. Adaptive depth bounds
support large exploded displacements. Clipping-only changes update uniforms and
picking without rebuilding vertex buffers. Context loss restores settings.
Ordered persistence and version guards cover delayed sync, failed-write retries,
external same-evaluation updates and Quick Edit snapshot invalidation.

Executed locally on macOS arm64 with OCCT 8.0.1:

- **All 43 CTest suites passed in one final run, 168.29 s**, including **162 native
  live checks**, **5,381 app protocol checks**, **144 controller checks**, **40
  native MCP live-loop checks** and **56 renderer checks**. Analytic renderer
  fixtures cover clipped occlusion/edges, shared vertex ownership, source
  immutability, extended depth, malformed settings and graphics recovery. Native
  fixtures cover atomic rejection, revisions, restart and retargeting.
  `build-app-protocol/presentation-ctest-final.log` records the complete run.
- Independent **656 schema checks across 24 tools** and **1,684 official MCP SDK
  2.3.0 checks** passed (`presentation-schema-final.log`,
  `presentation-sdk-final.log`); SDK counts include variable polling. Compact MCP
  discovery is **406,442 bytes**, under the unchanged **430,080-byte** bound.
  Skill validation and `git diff --check` passed.
- The relocated **empty-PATH** bundle passed native presentation save/read across
  separate processes with unchanged raw HEAD, shipped presentation guidance,
  exact embedded-resource provenance, and all existing native fabrication/slicing
  smoke checks (`presentation-bundle-final.log`). Windows/Linux execution and
  actual desktop-plugin host acceptance were not repeated for this increment.
- Actual browser tab **13**, `http://127.0.0.1:56227/`, uses a separate copied
  six-part curved-linkage workspace. Exercised explosion, clipping, reversal,
  axis/offset, Fit, native source-qualified face selection, Copy request,
  reopen/restoration, external revision refresh, visibility and reset. The picked
  `base` face resolves natively to **528 mm²** within numeric tolerance and source
  center **[30,-30,4] mm** while exploded. Copy request includes the original
  reference, camera and presentation. View changes preserve the copied source;
  an intentional demo-only thickness edit produces revision 2, retains settings
  and retires the prior pick. Original user demo HEAD remains revision 2 and its
  earlier draft is untouched. `build/presentation-demo/evidence.json`,
  `browser-selected-context.json`, `resolved-face.json` and edit records retain
  the evidence. Screenshots: `assembled.jpg`, `selected-exploded.jpg` and
  `live-inspection.jpg`. Preview host session **34989** serves the actual bundled
  app through a loopback protocol harness, not a certified desktop host. One
  console MutationObserver TypeError had no source URL; neither viewer nor
  harness contains a MutationObserver, and its origin was not established.

The first focused test run encountered stale compiled assets and an incorrect
mocked-context-loss buffer count; rebuilding assets and accounting for implicitly
invalidated buffers corrected those fixtures. An SDK assertion initially compared
timestamps across a valid context update; its baseline now follows that update.
The final focused six-suite run and the full run both pass. Runtime bounds and
source/history invariants were not weakened.

Final native executable SHA-256:
`fd2dac17d6b19fd867e9940a3c090819e35bfceb2d0d0f13b91b0318bdc159d8`.
This is verified local implementation progress; the full goal remains active.
Clipping is tessellated and uncapped, with no exact new section surface/edge.
Exact sections and topology-pair measurements/clearance, appearance/presets,
annotations, declarative time sequences and external-format review remain in
COMPOSITION_FABRICATION_REVIEW.md. Intentional printer handoff is also pending;
an optional printer-backend preference is awaiting a response and does not block
independent viewer work. No hardware action or auto-review rejection occurred.
Existing release signing/install/host gates remain independent. These increments
have not been committed or released.

## Native installed slicing — active larger goal, 2026-10-08

`cad_slice` is implemented locally as the **24th tool**, with `plan` and `run`
actions in the shared native service, strict runtime schemas and durable jobs.
The current driver supports **OrcaSlicer 2.4.2**, explicit **High Temp Plate**,
self-contained compatible Marlin FFF machine/process/filament JSON profiles and
one committed source solid. Executable and profile inputs require absolute
regular non-symlink paths and actual raw SHA-256. Missing tools, unresolved or
incompatible profiles, scripts, host post-processing, changed hashes and wrong
CLI versions fail explicitly. Native core CAD retains no slicer/runtime dependency.
`SLICING.md`, protocol, agent guidance and shipped bundle docs define the contract.

Planning captures source, a native scoped STL, three exact profile snapshots,
fixed argv/placement and a portable hash ledger without executing the slicer.
Run requires the exact plan hash, committed revision/model/build identity and
complete saved source. It retains the original self-contained plan under
`reviewed-plan/`, regenerates its executed STL from the same committed native
source, numerically checks exact volume/area/COM/bounds and topology counts,
probes actual version, executes fixed native argv and checks actual effective
profile IDs, bed/firmware/FFF semantics and disabled post-processing. Actual
mesh/profile bytes must remain unchanged during execution. Native G-code review
retains failed/unknown findings. Every published file except the manifest itself
has a portable relative size/hash ledger; no hardware is contacted or approved.

The first actual native run failed a byte-equality test between cold-plan and
warm-cache STL exports before executing Orca. Cache-restored triangulation/
serialization can differ for the same exact curved source. The corrected driver
executes a newly generated native mesh, retains both original plan and actual
mesh hashes, and verifies exact-source measurements within
`1e-6 + 1e-9 * max(abs(values))`, with matching solid/face/edge counts. It never
executes a plan-supplied replacement mesh or claims triangle/byte stability.
Cold/warm curved-source and altered plan/summary regression fixtures now cover
this behavior. The independent actual exported mesh check found **404 finite
triangles / 606 welded edges**, every edge incident to two triangles at 0.00001 mm.
This is edge-incidence evidence, not global self-intersection certification.

External execution uses the existing four-worker admission slots and a private
native supervisor with direct argv, restricted stdio, deadlines, aggregate
memory/staging/log limits, cancellation and coordinator-only atomic publication.
POSIX descendants share a process group whose leader remains unreaped until the
coordinator kills descendants, avoiding a reaped-PID reuse window. Parent-death
monitoring terminates orphaned supervision. Windows uses restricted inherited
handles and Job Objects with per-process and aggregate memory limits and
kill-on-close. These controls are for explicit trusted local tools; they are not
a hostile-binary sandbox. Memory/disk sampling can overshoot between 10 ms
samples. Hard coordinator death may leave private staging files; recovery marks
the job interrupted and never promotes an incomplete package.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The strengthened native slicing suite passes **292 checks**, including actual
  native child/descendant execution, spaces/UTF-8 paths, source-feature scoping,
  profile snapshots after originals disappear, independently checked complete
  package hashes, durable replay, contradictory plan execution/geometry,
  changed inputs, wrong version/effective settings, exit/missing-output failure,
  cancellation, deadlines, memory/log limits and actual supervisor/coordinator
  hard death. It observes live processes before cancel/kill and verifies they
  stop without published results or source mutation. The fixture only tests
  process/contract plumbing; it is not a slicer-accuracy simulation. Windows
  fixture paths/argv use wide APIs, but remote Windows/Linux execution was not
  performed for this increment. Streaming SHA-256 known-answer/padding/chunk
  checks cover the incremental implementation moved out of jobs.
- **All 43 CTest suites passed in one final run, 168.42 s**, including native
  geometry/cache/assembly/fabrication/G-code, jobs, live viewer, robot export and
  mocked renderer suites (`build-app-protocol/slicer-ctest-final.log`). Final
  independent **635 schema checks across 24 tools** and **1,800 official MCP SDK
  2.3.0 checks** passed, including native plan/run fixture jobs and both direct
  and job result schemas (`slicer-schema-final.log`, `slicer-sdk-final.log`).
  SDK counts include polling and may vary. Skill validation and `git diff --check`
  passed. Structurally identical schema subtrees now share local definitions,
  retaining every constraint; compact discovery is **403,361 bytes**, below the
  unchanged 420 KiB / 430,080-byte bound. Pretty CLI `tools` output is larger;
  that is distinct from the compact MCP discovery payload.
- The relocated bundle passed with **empty PATH**, shipped slicing guidance,
  explicit missing-tool rejection and actual native fixture plan/run execution
  using an externally supplied absolute fixture path. Independent raw plan/
  G-code and every package size/hash were verified (`slicer-bundle-final.log`).
  The test fixture is not installed or presented as a slicer. CI commands now
  pass it into schema/SDK checks, but remote CI was not executed here.
- Final actual **installed OrcaSlicer 2.4.2** integration ran through native
  durable plan/run jobs on saved `curved_linkage_demo`, revision 2, feature
  `arm`. The executable's raw identity remains
  `f22fe59f43f167503f11802d49a984787c896bd8a0ebc0c0af6cc6912b33a801`.
  Captured profiles are the explicitly resolved stock Prusa MK3S 0.4 nozzle /
  0.20 mm Quality / Generic PLA profiles sourced in the earlier external demo.
  Actual effective settings independently confirm 0.20 mm layers, 220°C nozzle,
  60°C High Temp Plate, profile IDs and no post-processing. Native slicing
  returned 0, **1,441 ms / 56,018,168-byte sampled peak group memory**, and
  produced **447,303 bytes / 30 layers**. Orca estimates **22m 9s / 5.57 g PLA**.
  G-code SHA-256 is
  `f2823a6ae29d9707276ecd480c5118e20b94992147b926946691082320a53f9d`.
  All **20 package artifacts** were independently size/hash verified and every
  file accounted for. Native review counts **15,330 moves / 12,842 deposition
  commands** and retains **unknown** full swept bounds/firmware behavior; passing
  heater targets do not approve a printer. Source HEAD remains revision 2 and
  existing model-viewer drafts/packages are preserved.
  `build/native-slicer-demo/{plan-request,plan-job,run-request,run-job,evidence}.json`,
  `independent-mesh-settings.json` and `slicer-real-final.log` retain exact calls
  and evidence. The first failed attempt remains in `slicer-real-first.log`.
- The installed CAD Viewer package still lacks its documented `agent:start`
  script, confirmed by unchanged package metadata. The required G-code handoff
  is unavailable for that concrete reason; it is not presented as a successful
  CAD Viewer review. A separate localhost diagnostic loads a copied complete
  native package and depicts programmed XY extrusion segments with the native
  **unknown** physical-motion coverage visible. Actual browser first/final layer
  seek, full-bed/part toggle, playback through the loop and pause were exercised.
  Final screenshot: `build/native-slicer-demo/live-toolpaths.png`; browser tab 12,
  `http://127.0.0.1:54537/`, preview host session 53968 (12-hour lifetime).
  It is a diagnostic example, not completion of native external-artifact viewing
  or declarative presentation sequences.

Final native executable SHA-256:
`96ec42c335249ad95aa2fde83bf4708c378cc9c9970a4fedf387c2ccec6d2956`.
This is **verified local implementation progress**; the full goal remains active.
Next required work is intentional printer handoff with explicit setup/dry-run
and separate physical-start authorization, then all remaining presentation
controls in COMPOSITION_FABRICATION_REVIEW.md. Other slicers/bed types, inherited
profiles and multi-solid plates remain unsupported. No blocker or automatic
approval rejection occurred. Existing 1.0 host/install/signing gates remain
independent. These changes have not been committed or released.

## Native static G-code review — active larger goal, 2026-10-08

`cad_gcode_review` is now implemented locally in the shared native service and
published as the 23rd tool. It accepts an existing absolute plain `.gcode`, its
expected raw SHA-256, explicit Marlin machine/material/initial-state assumptions,
and a caller-associated committed document/revision/feature. The package retains
original bytes, the complete saved source, source-qualified findings and a
portable relative hash ledger. Its association is explicitly not proof of
toolpath/geometry equivalence; it never executes G-code or starts hardware.

Native inspection tracks relative/absolute modes, mm/inches, Marlin's reset of
M82/M83 overrides by G90/G91, G92 coordinate offsets/E resets, caller-declared
home endpoints and known linear sweeps. G17/G18/G19 and G2/G3 use analytical
quarter extrema for relative centers, signed-radius minor/major arcs, full
circles and orthogonal helical travel. Numeric words/comments/optional raw XOR
checksums, heater targets and commanded extrusion have bounded witnesses.
Unsupported firmware commands, unknown coordinate frames, volumetric/tool/unit
changes and unmeasured sweeps remain unknown. Unknown temperature units retain
programmed values without labeling them Celsius; an unknown arc start does not
imply measured deposition. This is static inspection under caller assumptions,
not firmware, thermal, deposition, collision or printer-compatibility simulation.

The operation uses existing isolated bounded native worker slots, Windows/POSIX
containment, cancellable durable jobs and coordinator-only atomic publication.
An additional parser bound is 64 MiB / 4,096 bytes per line / one million lines /
30 seconds. A hash mismatch publishes no package. Failure/cancellation/deadline
preserves source HEAD/history and previous exports. Durable replay works after
the caller removes its original file. Runtime schemas, protocol, native guidance,
GCODE_REVIEW.md and relocated bundle checks describe the implemented contract.
Identical mutation result branches are deduplicated in the job schema without
changing admission or output contracts; the discovery size limit is unchanged.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The native G-code suite passes **74 checks**, including analytical relative/
  inch/G92 envelopes, all three arc planes and interior extrema, minor/major/full
  circle/helix paths, malformed/checksummed text, unknown firmware/state/unit
  semantics, original bytes/raw hashes/portable package, unchanged HEAD, failed
  publication, actual worker cancellation, deadline and durable replay. Initial
  test corrections added a missing required model `parameters` field and captured
  exports before starting the cancellation fixture, rather than treating its
  temporary staging directory as a published result. Runtime limits were not
  weakened.
- **All 42 CTest suites have passed against the same final native binary.** The
  broad run passed 41 in 143.52 s (`gcode-ctest-final.log`); its only failure was
  the live suite's expected tool count of 22. After updating that fixture for the
  required 23rd tool, live passed **145 checks / 11.71 s**
  (`gcode-live-final.log`). This is combined evidence, not a claim that the first
  broad run was green. `build-app-protocol/gcode-verification.json` records the
  complete suite/evidence audit. Final **589 schema checks across 23 tools** and
  **1,665 official MCP SDK 2.3.0 checks** passed (`gcode-schema-final.log`,
  `gcode-sdk-final.log`; polling affects counts). Skill validation and
  `git diff --check` passed. Compact discovery is **429,316 bytes**, below the
  unchanged 420 KiB bound (430,080 bytes).
- The relocated bundle passed with **empty PATH**, including native relative-
  motion failure, unchanged original G-code bytes, independent raw report hashes
  and shipped review guidance (`gcode-bundle-final.log`). No slicer or language
  runtime is needed for this operation. Remote platform CI and installed desktop
  client acceptance were not executed for this increment.
- The final native service reviewed the actual **447,662-byte Orca output** from
  the prior demo and independently verified all three package artifacts. It
  counts **15,320 moves / 12,837 deposition commands**, with 220°C/60°C targets
  passing the explicitly assessed ranges. Overall status is **unknown**: G28,
  the G80 leveling macro, M115 and M862.1/M862.3 do not establish full firmware
  behavior or a known physical frame. Complete swept bounds remain unknown;
  the earlier helper's permissive pass is not promoted to native approval.
  The exact G-code SHA-256 remains
  `8be83dc552c0bfece39096ed9f8602b0cd43dd4613539a6b99067eb2faa4085d`.
  `build/slicer-demo/native-review-{arguments,result,evidence}.json` and
  `gcode-real-review-final.log` retain the actual native call/result. The curved
  linkage's saved revision 2 and existing viewer drafts remain unchanged.

Final native executable SHA-256:
`2a432fb3bc3d6c51d12e74e484840701a2b842b6b94b14c331b4e7b4f6ec285d`.
This turn is **verified implementation progress**. The full authorized objective
is still active: the next increment must integrate installed slicers with
explicit executable/profile identity, bounded cancellable native execution,
actual output/provenance inspection and atomic publication. Intentional printer
handoff and all remaining presentation controls in
COMPOSITION_FABRICATION_REVIEW.md stay required. No blocker or automatic approval
rejection occurred; these local changes have not been committed or released.

## Real slicer workflow example — larger goal still active, 2026-10-08

The requested skills example now takes the curved linkage's saved revision 2
arm through an actual installed OrcaSlicer 2.4.2 CLI. This is **verified external
workflow progress**, not an implemented native service slicing/printer tool.
Production native/viewer code and the full acceptance scope are unchanged.
The next implementation task remains bounded, cancellable native slicer jobs,
explicit printer handoff, and the remaining presentation controls.

The preferred backend was initially absent. Homebrew installation completed
after its sandbox DNS/write restrictions were resolved by the allowed retry.
Discovery and actual `--help` confirm the executable and version. The demo uses
explicit bundled Prusa MK3S 0.4 nozzle, 0.20 mm Quality and Prusa Generic PLA
profiles, with original files/hashes and resolved inheritance retained. No user
machine was selected. Original source STL hash, finite triangles and welded
edge counts were independently checked: 404 triangles; the mesh's bounds differ
from exact curved B-rep bounds by the existing export tessellation approximation.

The first CLI execution correctly failed process compatibility (exit 239).
Inspection of the matching upstream source showed its CLI checks an explicit
compatible-printer list rather than the bundled condition. The demo evaluates
that unchanged condition for the exact MK3S 0.4 profile and materializes only
that one matching printer in its derived profile, retaining the originals.
Fabrication settings and native validity checks were not weakened. The first
successful output exposed Orca's default Cool Plate at 35°C; that diagnostic
file is kept separately. The final reviewed dry-run and execution explicitly
select High Temp Plate, matching the stock 60°C value. A private `--datadir`,
direct argv, 120-second time bound and 2 MiB log bound apply to this developer
example. These do not establish the required cross-platform native resource
containment, process cancellation or production job integration.

Executed locally:

- Final real slicing returned 0 in 0.21 s. Its **447,662-byte plain G-code**
  contains **30 layers at 0.20 mm**; slicer estimates are **22m 7s / 5.57 g PLA**.
  Raw SHA-256 is
  `8be83dc552c0bfece39096ed9f8602b0cd43dd4613539a6b99067eb2faa4085d`.
- The installed G-code validator passes its checks with unknown firmware-command
  warnings. Independent inspection matches effective machine/process/material/
  plate settings, finite explicit G0/G1 coordinates and 220°C/60°C targets.
  Recorded motion includes the stock Y=-3 purge position and Z=55 end lift.
  No arcs or relative XYZ occur. Initial units, incomplete coordinates, homing,
  leveling, firmware checks, thermal behavior and physical printing have explicit
  coverage limitations. No printer was contacted or print started.
- A standalone diagnostic browser preview renders **12,837 actual XY extrusion
  segments**. Actual browser execution verifies first/final layer seek, part/bed
  views, playback advancing through looping, pause and restoration to layer 15.
  `build/slicer-demo/live-toolpaths.jpg` captures the rendered result; the review
  is at `http://127.0.0.1:57503/` with a 12-hour loopback host lifetime. Existing
  CAD drafts/previews remain untouched. This preview is not a native viewer feature.
- `build/slicer-demo/` retains exact STEP, STL, drawing, editable source, native
  profile originals, effective settings, dry-run argv, logs, static coverage and
  a **25-artifact hash ledger**. Each recorded size/hash was independently checked;
  the saved source revision/model still match the original package.
- CAD Viewer startup failed because its installed package lacks the documented
  `agent:start` script. Explicit final and diagnostic G-code paths are recorded
  in `review.json` with the failed handoff. The standalone diagnostic preview is
  the fallback; no CAD Viewer integration claim is made.

At that external-example stage no production source changed; its evidence did
not claim new native/platform acceptance. The later native inspection increment
above records the current implementation and its separate validation.
`git diff --check` passed. No automatic approval rejection or repeated goal
blocker occurred. The full goal remains active with no scope reduction; native
slicer integration and the other pending acceptance items are still required.

## Verified sourced-part identity — active larger goal, 2026-10-08

`cad_import` now accepts optional `expected_sha256` and `purchase`. It hashes
unchanged local STEP bytes and binds caller supplier/part/source identity to
that measured artifact. A supplied expected/purchasing hash must match, otherwise
`artifact_mismatch` publishes no revision. Saved sourced `import_step` features
require the exact matching purchasing hash; ordinary imports remain compatible.
No URL fetching, catalog code execution or manufacturer/history inference is
introduced into native CAD. Discovery/download remains an explicit agent workflow.

Unchanged imports and pure transform/instance chains derive purchase identity
into rolled-up and hierarchical BOMs, including revision-pinned component reuse.
Geometry-changing features do not imply the unchanged purchased part. Explicit
BOM purchase must agree with its verified source; contradictions fail validation
and preserve HEAD/history. Purchasing-only edits reuse the same geometry keys,
while BOMs always read current intent. Item/material/process claims are not guessed.

Manufacturing packages now include an unchanged sourced leaf's original
`source.step` alongside independently exported geometry, with relative
`source_artifact` path/hash/source-feature identity and the ordinary hash/byte
ledger. This works for standalone scoped solids and nested assemblies. Component
captures remain self-contained after source-file/library removal or source HEAD
updates. Existing worker, package, cancellation and publication bounds apply;
imports retain the 512 KiB/UTF-8/complete-root/valid-geometry requirements.
PURCHASED_PARTS, protocol, examples, native guidance and relocated docs define
the extended contract. Application discovery still has 22 tools.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The new native suite passes **78 purchased-part checks**: independent OCCT STEP
  readback/volume, exact source bytes and actual hashes, malformed/mismatched
  provenance, no publication on failure, deleted-file receipt replay, nested
  quantities/occurrence paths, rigid copies versus modified geometry, contradictory
  BOM edits, current metadata on cached shapes, historical/source-free pinned
  components, portable package originals and independent artifact hashes, async
  import success/replay and failed checksum jobs. One initial assertion required
  bit-exact floating-point volume; it now uses the project's existing 0.000001 mm³
  integration tolerance, with independent STEP volume verification retained.
- The final broad run passed all **41 suites in 150.70 s**
  (`build-app-protocol/sourcing-ctest-final.log`). Final **557 schema checks**
  passed (`sourcing-schema-final.log`), including sourced imports, derived BOM,
  original-file packages and required raw-model hash contracts. **1,298 official
  MCP SDK 2.3.0 checks** passed (`sourcing-sdk-corrected-final.log`; polling affects
  counts). The initial SDK fixture attempted unsupported `cad_export.feature_id`;
  it was corrected to export the actual declared output, without changing the
  export contract. Compact discovery is **423,739 bytes**, below the unchanged
  420 KiB bound. Skill validation and `git diff --check` passed.
- The relocated bundle passed with empty PATH, including actual native export,
  sourced import with measured hash, preserved original STEP bytes in its package
  and shipped sourcing guidance (`sourcing-bundle-final.log`). Remote platform CI
  and installed desktop-host acceptance have not run this increment.
- A real step.parts catalog search/download was executed, with sandbox DNS failure
  resolved by the permitted network retry. Its M5 washer file is **14,999 bytes**;
  raw SHA-256 matches the published
  `51ff899874c1b231d8b120a3c92d5657faf50d085897c2b7fc6d92e27e10fe5a`.
  The current catalog asserts **1.1 mm** thickness, while the unchanged STEP spans
  **2.2 mm**, from Z=-1.1 to +1.1. Native volume **124.2515602458031 mm³** agrees
  with the independently computed annulus volume at that actual height. Source
  metadata is retained separately; no standards/seller claim or mesh substitution
  hides the discrepancy. Source records/evidence are in `build/sourcing-demo/`.
- The saved sourced-washer assembly **revision 2** has one editable plate and
  two occurrences of its pinned imported washer, with nested paths
  `hardware/left` and `hardware/right`. Measured stock review explicitly fails
  the catalog thickness comparison and retains unknown unsupported checks.
  Its native package contains **11 hashed artifacts / 245,152 bytes**, including
  original supplier STEP, independent leaf/assembly exports, complete editable
  source, BOM and failed review. Independent hashes, byte totals, source equality
  and unchanged export-time HEAD were verified. No physical supplier was selected;
  the recorded supplier label explicitly identifies the CAD catalog.
- Actual browser rendering and selection show three solids and identify the
  selected washer face as `hardware/left`, revision 2, with matching native view
  context (`selected-context.json`). `live-sourced-assembly.jpg` captures that
  view. The separate installed CAD Viewer launcher lacks its documented
  `agent:start` script, so that skill's handoff could not start; the native
  embedded viewer was used through the temporary loopback acceptance host.
  The new review is at `http://127.0.0.1:54937/`; existing user previews are untouched.
  Final native SHA-256:
  `e1697d2b7a2cdfdd9ec152277fbf8502dc2af877fab659a98a9add894b5cbd53`.

Next: installed slicer integration and explicit printer handoff, then the remaining
interactive presentation/review work in COMPOSITION_FABRICATION_REVIEW.md.
The larger goal remains active. These increments are not committed or released;
native and browser verification do not establish installed desktop-host readiness.

## Measured native fabrication review — active larger goal, 2026-10-08

`cad_fabrication_review` is the twenty-second tool. It measures an explicit
committed revision against caller-supplied FDM, CNC, sheet/laser or molding
profiles and publishes a checksummed JSON artifact with source/model/revision,
kernel and native build identity. Reports distinguish measured passes, measured
failures and unsupported or unevaluated checks. Successful execution can return
failed findings; no status implies production approval. Profiles require an
explicit build frame and never infer machine/material limits or allowances.

Native checks include exact oriented B-rep bounds, all-triangle welded mesh
topology, sampled exact inward wall chords, FDM overhang area, axial concave
cylinder radius and sampled CNC access, prismatic sheet form and explicit stock
thickness, and sampled signed mold draft/pull obstructions. Global minimum wall,
mesh self-intersection, swept cutter/fixture access, complete mold release and
actual slicing remain unknown. Checks record methods, coverage, skipped samples
and bounded witnesses; face/triangle identifiers are report-local evidence, never
persistent design references. `FABRICATION_REVIEW.md` defines these limits.

Unique assembly sources are reviewed in source coordinates, retaining quantities
and occurrence paths. Exact B-rep distance and common-solid volume use actual
saved occurrence transforms. Touching faces/edges contribute no material volume.
Assemblies of at most 23 leaves measure all pairs; larger assemblies require
explicit bounded pairs or report unknown. Per-source profiles are complete
overrides. Limits include 256 sources/pairs, 200,000 total triangles, 128 samples
per source and 4,096 total, within existing worker and 64 MiB report limits.

All geometry remains serial inside isolated workers. The coordinator publishes
only completed artifacts under the document lock and final cancellation check;
failed or cancelled work preserves HEAD/history and earlier exports. The same
options can join `cad_manufacture` as a hashed `review.json`, retaining failed or
unknown findings in the manifest. Omission still means `not_evaluated`.
Schemas, examples, native agent guidance and relocated runtime docs are updated.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The new analytic suite passes **78 checks**, including exact rotated bounds,
  plate/tube wall witnesses, overhang reorientation, cylindrical cutter limits,
  blocked tool access, sheet stock/form, independently imported tapered STEP
  draft/undercuts, nested/rotated gaps and overlaps, zero-volume touching,
  bounded pair subsets, mesh topology failure, overrides, raw hashes, historical
  replay, integrated package findings and cancellation after native work starts.
  Invalid inputs and failed/cancelled publication preserve source revisions.
- Initial checks caught the unchanged 420 KiB discovery bound, the separate job
  admission whitelist and the official SDK's explicit root object-type
  requirement. Shared schema definitions retain constraints while reducing final
  compact discovery to **422,015 bytes**; both native admission and root schema
  regressions are covered. The final broad run passed **39 of 40 suites in
  147.05 s** (`fabrication-ctest-clean-final.log`): the new mesh fixture used an
  invalid zero pattern step. Correcting it to valid touching plates, without
  weakening native validation or assertions, made the affected suite pass in
  **2.86 s** (`fabrication-affected-final.log`). This gives combined passing
  coverage of all **40 suites** on the unchanged final native executable.
- Final **543 schema checks across 22 tools** and **1,300 official MCP SDK 2.3.0
  checks** passed (`fabrication-schema-clean-final.log`,
  `fabrication-sdk-clean-final.log`; polling affects SDK counts). The relocated
  bundle passed with empty PATH, including native fabrication of the pinned
  component consumer, independent raw report hash/identity verification and
  packaged review guidance (`fabrication-bundle-final.log`). Skill validation
  and `git diff --check` passed. Remote platform CI has not run this increment.
- The actual curved-linkage demo reviews saved **revision 2**, six source parts,
  **162 exact UV samples** and all **15 saved-pose occurrence pairs**. Illustrative
  inputs are a 200 mm envelope, 2 mm sampled-wall limit, 45° FDM overhang and
  0.5 mm clearance. The spindle has **52.965 mm²** above that overhang angle.
  No pair has positive-volume interference; touching pairs fail the illustrative
  clearance limit. No global thickness or fabrication approval is inferred.
  The **49,690-byte** hashed report and a new **18-artifact / 710,293-byte** native
  manufacturing package preserve these failed findings. Independent hashes,
  byte totals, source/build identity, portable paths and unchanged HEAD/history
  were verified (`build/fabrication-demo/evidence.json`). The existing unsaved
  live pose and previous seven-sheet drawing package remain unchanged.
  Final native SHA-256:
  `bc88f0bec278b56bc734cb03ea54bb63ff33e9836def7a20adcb7bdd0e8ca2bd`.

Next: purchased-part sourcing/import, installed slicer and printer handoff, and
the remaining presentation work in `COMPOSITION_FABRICATION_REVIEW.md`.
The larger goal remains active. These increments are not committed or released;
native verification does not establish installed desktop-host readiness.

## Native manufacturing packages — active larger goal, 2026-10-08

`cad_manufacture` is the new twenty-first tool. An explicit committed revision
produces a movable directory containing complete editable source, unique leaf
STEP/STL/PDF/SVG/DXF files, drawing recipes and measured dimensions, saved
assembly geometry and occurrence transforms, BOM JSON/CSV and a relative-path
SHA-256 manifest. Source parts remain in their original feature coordinates;
assembly exports retain the saved pose. Scoped solid outputs are supported.
Repeated leaf sources export once with derived quantities and occurrence paths.

Saved BOM `purchase` metadata preserves caller supplier/part identity, an
HTTP(S) source URL and optional artifact hash through nesting and component
capture. CSV adds four purchasing columns and retains formula neutralization.
No supplier URL is fetched or independently verified by this metadata field.
Process/material assumptions and notes are explicit caller inputs; the package
records process review as `not_evaluated`. No slicing or hardware action occurs.

Geometry and drawing generation run serially inside an isolated native worker.
Only the coordinator publishes the complete private generation, under the
document publication lock and final cancellation check. Failed or cancelled work
removes its staging directory and preserves HEAD, history and previous exports.
Limits are 256 unique source parts, 2,560 artifacts, 64 MiB per file and 256 MiB
for the complete package, including its manifest. Generated part-directory names
keep Windows device identifiers out of filenames. `MANUFACTURING.md`, schemas,
protocol, examples, agent guidance and the relocated bundle define the contract.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The initial broad run passed 38 of 39 suites and exposed duplicate tool-schema
  expansion exceeding the existing 420 KiB discovery bound. Shared drawing,
  BOM, mate and identity definitions reduce final compact discovery to
  **420,191 bytes**, retaining all constraints and the unchanged bound.
  After the final build, all **39 suites passed in 145.27 s**
  (`build-app-protocol/manufacturing-ctest-clean-final.log`). Earlier verification
  overlapped a rebuild and is not the final build's acceptance evidence.
- The new suite records **409 manufacturing checks**. Independent OCCT STEP
  readback verifies valid shapes, solid counts and volumes; independent binary
  STL decoding verifies finite bounds and watertight fixture edges. Tests cover
  unique nested sources, placement, drawing dimensions, purchase metadata,
  hashes/bytes, movable packages, source-free pinned-component rebuilds, invalid
  recipes after staging, historical export/job replay, and cancellation after
  actual native STEP generation begins. Historical documents and exports remain
  unchanged throughout failed or cancelled generation.
- Final **526 schema checks across 21 tools** and **1,129 official MCP SDK 2.3.0
  checks** passed (`manufacturing-schema-clean-final.log`,
  `manufacturing-sdk-clean-final.log`; polling affects counts). The final relocated
  bundle passed with empty PATH, including historical pinned-component package
  generation, independent manifest hash/byte checks and complete editable source
  (`manufacturing-bundle-clean-final.log`). Skill validation and `git diff --check`
  passed. Remote platform CI has not run this increment.
- The actual curved-linkage demo exports saved **revision 2**: six unique source
  parts, **68 hashed artifacts**, **1,272,901 bytes**, six A4 part sheets and one
  A3 assembly/BOM sheet. Independent hashes and byte totals match the manifest;
  editable source equals the committed model and original HEAD/revision hashes
  are unchanged. All seven PDFs were rendered with Poppler and visually inspected.
  The arm drawing measures nominal 76 mm width, 6 mm thickness and 26.119 mm
  overall profile height. Evidence and PNGs are in `build/manufacturing-demo/`.
  Native SHA-256:
  `55ae203653c9123e8019d6a1ff95727f99c90f82eb651ee570c2e4506599b890`.
  Its existing live preview still shows the unsaved 45° arm / 8 mm spindle lift;
  that draft was inspected without saving or resetting it. Package geometry is
  explicitly from the saved revision, not that unsaved preview.

Next: measured fabrication/process review, sourcing/import, slicer/printer
handoff and the remaining presentation work in `COMPOSITION_FABRICATION_REVIEW.md`.
The larger goal remains active. These increments are not committed or released;
native package verification does not establish installed desktop-host readiness.

## Dependency-level native caching — active larger goal, 2026-10-08

Workers now restore unchanged exact features individually, using fingerprints
of each feature's geometry intent, referenced parameters, upstream keys and
native build/SDK/kernel identity. Complete model snapshots remain a fast path.
Assembly B-rep snapshots retain independently copied compound children;
restoration validates shapes and occurrence transforms, rederives hierarchy and
motion, and computes ownership from the actual restored children. No saved
face/edge enumeration becomes a persistent design reference. Private snapshot
format 2 invalidates old derived entries through the native build fingerprint.

Every feature is still validated, including branches outside the output. Unused
parameters, BOM/preset metadata and component source provenance do not invalidate
shapes; summaries always use current intent. Projection keys follow the output
closure, so a changed spare branch is validated without reprojecting unchanged
output. Failed native features/drawings publish no preceding worker stages.
Coordinators retain publication and cancellation checks. New feature stages have
a 32 MiB aggregate encoded limit per worker, alongside the existing 32 MiB model
snapshot, 64 MiB entry and shared 128-entry / 256 MiB cache bounds. Kernel state
remains serial and process-local; callbacks carry JSON only.

`DEPENDENCY_CACHE.md`, protocol, assembly guidance, development commands and the
relocated bundle describe the behavior. There are no new public tools, response
fields or runtime dependencies. The developer-only native benchmark is excluded
from runtime bundles.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The broad **38-suite** run passed 35 suites in **140.32 s** and exposed three
  assertions requiring bit-exact integration measurements after B-rep restore
  (`dependency-cache-ctest-final.log`). Diagnostic runs established that drawing
  files/dimensions and preview operations were exact, with only final-digit
  differences in volume, area and center of mass. Numerical comparisons now
  preserve exact IDs, metadata, ownership, counts and structure; drawing artifact
  checks remain byte-exact. Final `motion`, `cache` and `assembly_drawing` suites
  passed in **28.52 s** (`dependency-cache-affected-final.log`), giving combined
  passing coverage of all **38 suites**. Final detailed output records **840
  motion**, **44,662 cache** and **62 assembly-drawing** checks.
- The new dependency suite covers radius/thickness/profile/pose/placement edits,
  nested repeated ownership, current metadata on full hits, hidden-branch
  projection reuse, corrupt B-reps/occurrences/provenance/checksums, cache deletion,
  failed-build publication, independent cached STEP readback, pinned source
  updates and consumer history. Parallel projection checks verify cold and warm
  dependency decisions in actual separate processes, with aggregate limits and
  failure cleanup. Existing job/transaction suites verify cancellation and HEAD
  preservation.
- **501 schema checks across 20 tools** and **1,022 official MCP SDK 2.3.0 checks**
  passed (`dependency-cache-schema.log`, `dependency-cache-sdk.log`; polling
  affects counts). The relocated bundle passed with empty PATH, including a bound
  child pose edit that publishes only two changed assembly dependencies while
  retaining both imported-solid entry files and hashes. It also rebuilds the
  component in a workspace without its library. `dependency-cache-bundle.log`
  records the result; `git diff --check` passed. Remote platform CI has not run
  this increment.
- The native 128-hole panel/two-adapter benchmark independently compared three
  width edits: exactly five feature hits and two rebuilds each time, with matching
  cold geometry. Incremental times were **0.484 / 0.481 / 0.453 s**, against
  **0.508 / 0.503 / 0.505 s** cold (**1.04–1.12×**). Unchanged evaluation took
  **0.241 s**, against **0.502 s** initially. This fixture demonstrates selective
  work with a modest measured edit benefit, not a general large-speedup claim.
  Evidence: `build/dependency-cache-benchmark/.pending-XXMsOk/report.json` and
  `build-app-protocol/dependency-cache-benchmark-final.log`. The report includes
  intent, per-feature decisions, kernel/build identity and native SHA-256
  `93c98992f59403098ad274cbe8498213de41a0b5231edc1063db5e76829c306a`.

The user's existing curved-linkage preview and both assembly demos remain open;
the current linkage pose was inspected and captured without saving or resetting
it (`build/showcase-20261008/new-skills-example.jpg`). Their original browser
evidence remains qualified to the native builds recorded at their checkpoints.

Next: revision-qualified manufacturing packages, then measured process review,
sourcing, slicer/printer handoff and the remaining presentation work in
`COMPOSITION_FABRICATION_REVIEW.md`. The larger goal stays active; this increment
and the preceding composition work are not committed or released.

## Pinned editable components — active larger goal, 2026-10-08

`set_component`, `detach_component` and `remove_component` are ordinary atomic
edit/preview operations. Capture names an explicit saved source revision and
optional output feature. Its required dependency graph becomes editable local
features, with deterministic feature/parameter maps and explicit consumer scalar
bindings. The complete checksummed source snapshot stays in the consumer;
ordinary rebuilds, edits and exports require no source library. Source updates
are explicit, preserve the local root ID and reject local edits unless replacement
is requested. Summary/viewer provenance identifies the pin and changed local IDs.
Nested source provenance is bounded to four levels and 64 components, within
existing document, feature, parameter, geometry and worker limits.

`COMPONENTS.md`, runtime schemas, protocol, bundled native skill and paired
`component-assembly` examples define the implemented contract. No end-user
runtime or new tool was added. Shared job and model-identifier schema definitions
keep complete MCP discovery at **415,164 compact bytes**, below its unchanged
420 KiB regression bound.

Executed locally on macOS arm64 with OCCT 8.0.1:

- The broad 37-suite run passed 35 suites and exposed excessive schema duplication
  plus an invalid cancellation fixture that held the publication lock needed by
  cancellation (`build-app-protocol/components-ctest.log`, 136.14 s). Both were
  corrected without relaxing acceptance bounds. Final affected suites `model`,
  `protocol`, `app_protocol`, `component`, `service` and `live_mcp_flow` all passed
  in **21.65 s** (`components-final-checks.log`); combined coverage includes all
  **37 suites**. That final run records **56 component checks**, **4,852 app
  protocol checks**, **138 service checks** and **40 live MCP checks**.
- Component checks include exact STEP readback, copied consumers without source
  libraries, source/consumer history, local-edit conflicts and preview, final-batch
  parameter bindings, nested provenance limits, long-ID collisions, selector
  remapping, kernel failure, snapshot restore, request replay and cancellation
  during real native geometry work. Cancellation preserves the whole old model.
- Final **503 schema checks across 20 tools** and **1,012 official MCP SDK 2.3.0
  checks** passed (`components-schema.log`, `components-sdk.log`; polling affects
  SDK counts). The final relocated bundle passed with empty PATH, including
  pinned historical capture, parameter-driven child motion, a source-free
  workspace rebuild and packaged component guidance (`components-bundle.log`).
  `git diff --check` passed. Remote platform CI has not run this increment.

Actual browser demo: two imported hinge occurrences preview and save 75 degrees
as consumer revision 3 with preset `paired_hinges`. Reopening the verified build
retains that pose. The viewer shows its source pin at revision 1 and local edits,
while the library itself is revision 2. A refresh correctly returns
`component_modified`; an explicit replacement preview changes volume from 6,208
to 6,288 mm³ without changing saved revision 3. Exact STEP and BOM are exported.
`build/component-demo/evidence.json`, `browser-state.txt`, `component-review.jpg`
and the saved request/results preserve the evidence. This is local browser
acceptance through a test stdio host, not installed desktop-host acceptance.
The earlier curved-linkage and nested-module demos remain separate and open.

Next: dependency-level native caching, then manufacturing packages, measured
fabrication/process review, sourcing, slicer/printer handoff and the remaining
presentation work in `COMPOSITION_FABRICATION_REVIEW.md`. The larger goal stays
active. These composition increments are not committed or released.

## Composed motion and robot handoff — earlier checkpoint, 2026-10-08

The embedded Motion panel now selects reachable moving assembly definitions,
lists their affected occurrence paths and previews child joints in the complete
parent composition. Repeated occurrences share source pose values. Drafts from
different definitions combine; Save commits them atomically as one revision.
An optional preset belongs only to the selected definition. Reset also works
when the displayed parent has no moving joints of its own. Runtime summaries,
schemas, protocol and bundled agent guidance describe this scope explicitly.

Nested robot export now retains all physical leaves, parent and child joints,
cylindrical carriers and definition-scoped presets. Each subassembly attaches
through a physical grounded anchor, without invented empty bodies or inertias.
Length-encoded occurrence names avoid XML identifier collisions. Reused source
definitions export unit-ratio mimic relationships with explicit ledger evidence.
Independent URDF/SDF forward kinematics covers three levels, rotated datums,
multiple roots, parent motion, repeated and independently posed definitions,
authored mixed-unit couplings and all child presets. It also exposed and fixed
loss of rotation precision when serializing near a quarter-turn pitch.

Persisted views now qualify frozen data and pending read jobs by the native
build. Upgrade refreshes derived geometry without discarding draft intent or
admitted-save identity. The browser restores a same-document camera even when
its old geometry pick is stale; stale picks remain unusable. Regression tests
cover committed/draft refresh, obsolete completed reads, save reconciliation,
camera/visibility retention and unchanged historical source.

Executed locally on macOS arm64 with OCCT 8.0.1:

- All **36 CTest suites passed** in two complementary runs: six focused suites
  in **32.68 s**, then the other 30 in **96.18 s**. Logs:
  `build-app-protocol/composed-upgrade-focused.log` and
  `composed-upgrade-regression.log`. The focused details record **2,105 independent
  robot XML/FK/artifact checks**, **112 composition checks**, **765 motion checks**
  and **145 native live checks**; job polling contributes to some counts.
- **471 schema checks across 20 tools** and **852 official MCP SDK 2.3.0 checks**
  passed (`composed-upgrade-schema.log`, `composed-upgrade-sdk.log`).
- The final browser camera fix passed **130 UI bridge/state checks** and all
  three affected suites (`live_ui`, `app_protocol`, `live_mcp_flow`) in **5.70 s**.
  Log: `composed-camera-checks.log`. Native geometry sources did not change in
  that follow-up. The rebuilt relocated bundle passed with empty PATH, including
  nested child pose editing, historical intent and composed URDF/SRDF export;
  see `composed-camera-bundle.log`. Remote platform CI has not run this increment.

Actual browser evidence: the existing nested-module workspace exposes child
controls, previews/resets the shared pivot, saves `nested_review` at 30 degrees
as revision 2, reloads and recalls that preset. Three hidden leaf paths remain.
The first upgrade test exposed the camera reset; after its fix, a controlled
legacy-view fixture verifies re-evaluation with the recorded camera preserved.
`build/composition-demo/child-motion-evidence.json`, `camera-upgrade-fixture.json`
and `child-motion-review.jpg` record the result and current asset/native hashes.
The separate curved-linkage showcase remains open with its user's unsaved pose
unchanged. This is local browser evidence, not installed desktop-host acceptance
or an external ROS/Gazebo simulation claim.

Next: pinned cross-document component snapshots with explicit parameter mappings
and atomic updates, then dependency-level caching. Manufacturing packages,
measured fabrication/process review, sourcing, slicer/printer handoff and the
remaining presentation work in `COMPOSITION_FABRICATION_REVIEW.md` are still
pending. The larger goal remains active; no release or commit of this work has
been made.

## Nested composition foundation — earlier checkpoint, 2026-10-08

The owner authorized the remaining assembly-composition, fabrication-workflow
and presentation/visual-review work. `COMPOSITION_FABRICATION_REVIEW.md` retains
the full scope; this first increment does not complete that goal.

Assembly inputs can now reference earlier assemblies. Native evaluation retains
independent exact leaf solids, composes every parent placement with the child's
evaluated motion, and publishes occurrence paths such as `left/link` and
`right/link`. `assembly.tree` records hierarchy and owning definitions. Repeated
subassemblies share source dimensions and joint values. Expansion is bounded
before kernel work: eight levels, 1,024 leaves per assembly, 4,096 expanded leaves
across definitions, while direct 64-part and document 256-declaration limits stay.
Coincident occurrences retain distinct topology ownership; cached reconstruction
retains hierarchy and transforms. Solid operations still cannot consume assemblies.

BOM rows roll up leaf sources across repeated modules. Nested `structure` also
retains each owner's original metadata, including subassembly part numbers and
local item numbers. Conflicting descriptive metadata for one source fails.
Drawing explosions accept leaf or group paths; ancestor and child offsets add
in world coordinates. Balloon anchors stay leaf-local. Query, native visibility
and viewer schemas accept the same bounded paths. The Parts panel shows and
searches hierarchy and hides/isolates whole modules through their leaf paths.
The native bundle includes the new contract and the previously omitted robot
handoff document. `examples/nested-assembly.create.json` is a reusable fixture.

Executed locally on macOS arm64 with OCCT 8.0.1:

- Native build succeeded; **36/36 CTest suites passed (118.48 s)**. Evidence:
  `build-app-protocol/composition-ctest.log`.
- New assembly-composition suite: **111 checks**, including rotated repeated
  child kinematics, independent STEP readback, coincident ownership, cache
  restoration, expansion limits, BOM metadata, group explosion and revision
  preservation through preview, save, failed edits and reopen.
- **431 schema checks across 20 tools**, including a real nested BOM/drawing
  balloon and persisted leaf visibility; **877 official MCP SDK 2.3.0 checks**.
  Logs: `composition-schema.log` and `composition-sdk.log` in that build folder.
- **123 live UI state checks** and **45 pure/mocked WebGL checks** passed. The
  full CTest renderer run also exercises native payloads. Relocated `bundle-check`
  passed with empty PATH (`composition-bundle.log`); `git diff --check` passed.
  Remote platform CI has not been run for this increment.

The relocated executable also passed a dedicated nested create/BOM/mesh/STEP
workflow with empty PATH and loader overrides removed. Its five exact leaves,
288 mm³ volume, occurrence ownership and subassembly metadata were checked, and
both new workflow documents are present in the installed bundle. Evidence:
`build-app-protocol/composition-bundle-evidence.json`.

Observed in the actual embedded viewer through a loopback stdio test host:
searching `rod` shows both module ancestors and their link leaves; hiding `left`
hides two leaves; isolating `right` hides the other three; Fit frames the visible
module; reload restores camera and exact hidden paths. Native `cad_context`
independently confirms revision 1 and `left/foot`, `left/link`, `spare` hidden.
Evidence: `build/composition-demo/browser-evidence.json` and
`isolated-module.jpg`. This is browser evidence, not an installed desktop-host
acceptance claim. The user's curved-linkage showcase remains open separately;
its unsaved pose was preserved.

Child mechanism controls and complete robot graphs were the next tasks at this
checkpoint and are now implemented in the increment above. Pinned cross-document
components, dependency-level caches, fabrication and remaining presentation work
are still pending under the same active goal.

## Modeling flexibility and moving mechanisms — implemented and locally verified, 2026-10-07

This increment adds native symmetric chamfers, exact mixed
line/arc/Bezier/interpolating-spline profiles with holes, exact curved sweep
paths, and circular patterns. Curves retain exact geometry; disconnected,
degenerate and self-intersecting wires fail rather than becoming polygons.
The two new curved-model examples also pass schema/create/preview/edit/export
checks. Lofting a profile with inner wires is explicitly unsupported.

Articulated assembly mates now include revolute, slider and cylindrical types,
explicit limits, linear acyclic coordinate couplings and complete named poses.
Joint/pose operations use ordinary atomic edits; failed motion leaves HEAD and
history intact. Native forward kinematics applies parent/child datum frames,
offset, rotation and axial travel. Assemblies remain one level deep and have
single-parent acyclic mate graphs, without collision or dynamics solving.

The embedded Motion panel previews exact native geometry, shows driven
coordinates, applies named poses, resets and saves an ordinary revision (with an
optional preset name). Draft context includes its preview operations; draft or
superseded picks are rejected. Pending reset/save/external-revision races preserve
camera/visibility and prevent late workers replacing newer state. Preview output
schemas require either the complete HTML/data pair or native mesh/topology.

Native `cad_robot_export` produces paired URDF/SRDF or SDF 1.12, source STL
meshes, an explicit frame/coordinate/physical-data ledger and a portable hash
manifest. Exported zero reproduces the saved pose; limits, named poses and
couplings are converted to SI, including mixed angular/linear couplings.
Cylindrical joints preserve both coordinates through an explicit carrier link.
Effort/velocity are required caller data; SDF requires all part/carrier inertials.
No physical properties, controller, IK chain or disabled collision pairs are
invented. Exports publish complete directories after native worker success and
cancellation checking, leaving source/history unchanged. See `ROBOT_EXPORT.md`.

Executed locally on macOS arm64:

- Full **35/35 CTest suites passed (119.08 s)**, including motion transaction
  cancellation and independently parsed robot XML/FK. Log:
  `build-app-protocol/modeling-motion-robot-ctest.log`.
- Final preview-schema changes and expanded motion output checks: **4/4 suites
  passed (14.02 s)** (`motion`, `app_protocol`, `robot_export`, `live_mcp_flow`).
  The motion executable passed **269 checks**, including posed STEP readback,
  binary STL bounds and drawing dimensions. Logs: `modeling-motion-final.log`
  and `motion-final.log` in that build directory.
- Final independent robot run passed **928 XML/FK/artifact checks**, including
  relocated URDF/SRDF/SDF bundles, offset/rotated frames, named/sample poses,
  inertia/COM serialization, invalid physical inputs and historical async export.
  `final-robot.log` records the run; polling contributes to the check count.
- **406 schema checks across 20 tools**, **902 official MCP SDK 2.3.0 checks**,
  bundled-skill validation and `git diff --check` passed. Logs: `final-schema.log`
  and `final-sdk.log`; SDK polling counts vary by run.
- Relocated `bundle-check` passed with empty PATH, including curved STEP exports,
  motion preview/save/history and native URDF/SRDF/SDF generation. The workflow
  now runs independent robot validation on every native CI lane; these new
  sources have not yet been run on those remote platforms.

Observed in a real browser iframe host over the native stdio service: independent
joint changes update driven values and geometry; Reset restores the saved pose;
named poses save as revisions 2 and 3; reload retains revision 3, `final_review`
and hidden-part state. This is local embedded-view evidence, not an installed
ChatGPT/Claude-host claim. Evidence/workspace:
`build/motion-demo/final-browser-evidence.json` and `browser-workspace/`.

The installed external CAD Viewer could not start: its package lacks the
`agent:start` command required by its skill. `gz`, ROS `check_urdf`, MoveIt and
a dynamics simulator are unavailable, so no external consumer/simulation
certification is claimed. A reviewable URDF/SRDF bundle and ledger are under
`build/robot-demo/workspace/exports/`; `build/robot-demo/urdf-result.json` names
the exact files. The sample effort/velocity values are illustrative test data.

`MODELING_MOTION.md` records the requirement-by-requirement evidence audit. The
feature increment is complete locally; next work is the separate 1.0 host and
distribution acceptance below, plus cross-platform CI for this increment. No
version, release or installed host configuration was changed. On 2026-10-08,
the owner authorized committing and pushing this increment to `main`.

Completion audit rechecked the current implementation, test assertions and
actual log files against every requirement in `MODELING_MOTION.md`. The native
build is up to date; the executable and all twelve files recorded in
`build/robot-demo/verification.json` match their recorded SHA-256 hashes. All
four web assets match the observed browser evidence, and all seven artifacts in
the review robot bundle match its manifest. The final relocated bundle log
confirms successful completion. No additional implementation gap was found
within this feature contract; the platform/consumer limitations above remain.

## 1.0 narrowed to local desktop plugins — 2026-10-07

The owner chose ChatGPT desktop and Claude Desktop for 1.0, with CAD staying
local and the viewer embedded in chat. Tauri, CLI onboarding and other hosts are
deferred. The published preview remains historical development evidence; no new
version, tag or public release was created. `docs/RELEASE_1_0.md` defines the
actual install/create/select/edit/reopen/export/upgrade gates. README now leads
with installation and use; developer commands remain in `docs/DEVELOPMENT.md`.

Implemented native `serve --default-workspace`, selecting persistent
Documents/Agent CAD outside the plugin cache. Windows uses the OS Documents
known folder (including redirected folders), macOS the user's Documents folder.
Explicit existing folders remain supported; plugin-mode overrides must be
absolute. CLI commands outside plugin startup retain their explicit workspace
contract. Folder selection is tested without writing to the real user folder.

Claude's optional setting exposed two actual host behaviors in **Claude
2.26454.2 on macOS arm64**: its nested `${DOCUMENTS}` default reached the process
literally, and clearing that field left `${user_config.workspace}` literally.
The first attempt failed opening relative storage on a read-only filesystem.
The extension now has an empty default and uses dedicated
`--default-workspace --workspace-setting VALUE` startup. Only an empty value or
the exact unset setting marker selects the native default. Other relative paths,
including nested host placeholders, fail without creating storage. Repeated
workspace arguments are rejected even if the first value is empty.

The host ignores reinstalling an identical extension version. A local-only test
wrapper used manifest version `0.1.0-preview.1.1` to exercise its Update action;
this did not change VERSION, native binary version or any release/tag. The owner
approved the local extension access prompt. The installed executable matches the
tested native bundle by SHA-256. The installed local wrapper's SHA-256 is
`e5b3818e248c5a8356e8d75275da57eb367979191e811f64dab75079f58d2dae`;
the ordinary generated MCPB's is
`79fb2069dab40640d74e9717f13cd745882d8e28bec7ace4b85fe47cb378e6d8`.
Actual initialize, tools/list and resources/list
succeeded. Claude created `plugin_test_plate`, rendered the embedded viewer at
revision 1, and read a selected 80 mm edge with its exact document/revision/
evaluation reference through `cad_context`. The viewer created a **3,940-byte
PDF** with a valid `%PDF-` header in the default workspace. Quick Edit sent the
selected-edge request with its complete view/document/revision/evaluation
context. Claude added a parametric 2 mm `front_top_fillet`, committing revision
2: one valid solid, seven faces, fifteen edges and volume **23,931.327 mm³**.
The same embedded viewer refreshed to revision 2 and cleared the old selection.
This is actual host select–edit–refresh evidence, not just a protocol simulation.

The follow-up opened a library view and read revision 1 without restoring it;
HEAD remained at revision 2. Its first `cad_list` and STEP export calls timed out
in the host after four minutes each. The server continued answering viewer
requests. A single `cad_list` retry succeeded, returning `plugin_test_plate` at
revision 2 with `truncated: false`. The export retry exposed Claude's per-tool
approval prompts. With the local export calls approved, the actual host created
revision-2 **STEP (20,073 bytes), binary STL (2,084 bytes / 40 triangles), and
A4 PDF (6,883 bytes)**. Independent filesystem inspection verified STEP start/end
markers, STL byte count, PDF header, HEAD still at revision 2, and the viewer's
cleared selection. Drawing settings and a provenance manifest were saved too.
This proves file generation; the host still reports local paths rather than
delivering files to the user. The library widget rendered, but reopening a
project through its list, host restart and update/uninstall preservation remain
unfinished acceptance checks. Do not attribute the earlier timeouts to a
particular cause without evidence.
Computer-use scrolling failed with `noWindowsAvailable` despite readable Claude
accessibility state; some input was interrupted by user interaction. Doubled
pasted text was observed but its cause has not been established; do not claim a
clipboard implementation bug.

`packaging/make-plugin.py` creates native-only Agent Plugins packages with
portable plugin/MCP metadata, an OpenAI onboarding skill, the modeling skill,
icon and a local marketplace catalog. Full source/dependency notices and exact
file hashes are preserved. The package rejects incomplete inventories and
standalone desktop inputs. Main/PR CI now packages/tests these plugins on all
five lanes and produces Claude MCPBs on macOS/Windows without Rust or GUI build
dependencies. Legacy `v0.*` tags retain the old Tauri/draft-preview contract;
`v1.*` does not trigger automatic publication. ChatGPT's public directory local
MCP approval is a separate unfinished distribution gate. Its actual desktop host
journey has not been verified; computer use could not access the installed
ChatGPT app in this environment.

Local validation against the final startup change: native build succeeded;
CTest **6/6 passed** (`embedded_assets`, `notices`, `cli_smoke`, `jobs`,
`app_protocol`, `live_mcp_flow`, 21.98 s). All three packagers reject empty,
duplicate, missing and unlisted inventories; complete extension/plugin fixtures
pass. The packaged plugin passed full inventory/hash checks, empty-PATH native
MCP/viewer discovery, create/edit/reopen/history, STEP/STL/PDF/SVG/four DXF
exports with unchanged source. Portable metadata passes the published Agent
Plugins 1.0 plugin and MCP schemas. The setup skill passes Skill Creator's
validator. Tool schema conformance passed **295 checks** across 19 tools; official
MCP SDK 2.3.0 interoperability passed **932 checks**; version and all three client
configuration round trips passed without creating a workspace.
Evidence and packages: `build-desktop/plugin-check-host/`;
native test log: `build-desktop/plugin-check/ctest-host-fix.log`.

Implementation commit `6eefb7967dc60055a8228b343f90c0c5a367176a` passed all five
native jobs in
[run 37701114813](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37701114813).
Each lane passed **32/32 CTest suites**, official MCP SDK 2.3.0 interoperability,
packaging provenance/configuration checks, relocated bundles and the complete
empty-PATH plugin workflow, including history and all export formats. Both Linux
runtime-only containers passed. Plugin ZIPs were uploaded on all five lanes;
Claude MCPBs were uploaded on macOS arm64/Intel and Windows x64. Legacy Tauri and
the release job were skipped. The log records these platform-specific counts
(SDK counts include polling and are not fixed assertions):

| Platform | Schema checks / 19 tools | SDK checks |
|---|---:|---:|
| Linux arm64 | 293 | 502 |
| Linux x64 | 291 | 502 |
| macOS arm64 | 289 | 792 |
| macOS Intel x64 | 293 | 1,607 |
| Windows x64 | 305 | 747 |

CI log: `build-desktop/plugin-check-host/ci-main-37701114813.log`. A passing
package workflow does not prove either host's complete installation journey.

Next: finish both actual host journeys, resolve in-host file retrieval (current
exports report local paths), establish easy architecture selection, and implement
publisher signing/notarization and the approved install/update distribution
routes. Keep the native engine local; do not introduce a hosted geometry service
or tunnel requirement to bypass the product decision. Do not tag 1.0 before its
acceptance gates pass.

## First versioned preview — 2026-10-07

Commit `2065a692851604d29b72a4c2fc2ddd20960b1070` passed all five native jobs in
[run 37688014915](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37688014915):
macOS arm64/Intel x64, Linux arm64/x64 and Windows x64. Each job completed the
native suites, schema/MCP interoperability, installer/configuration checks,
relocated bundle tests, Tauri integration tests, release build and packaging.
Both Linux runtime-only container checks passed. This closes the Intel test and
Windows packaging acceptance gates described below; their earlier failure logs
remain useful regression evidence.

The owner authorized the first preview release. Annotated tag
`v0.1.0-preview.1` points to that tested commit; its
[release run 37691102208](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37691102208)
passed all five platforms and the draft-release job. Each platform passed
**32/32 native suites and 2/2 Tauri integration tests**, schema/MCP checks,
installer/configuration tests, relocated bundles and archive generation. Both
Linux runtime-only containers passed. The
[prerelease](https://github.com/cfaulkingham/agent-3d-cad/releases/tag/v0.1.0-preview.1)
was published on 2026-10-07 at 22:10:54 UTC, with ten archives, three Claude
extensions, two installers and `SHA256SUMS`.

The independent download audit caught an empty `files` inventory in all three
Claude extensions. CI's `cmake --install ... --prefix bundle` used a relative
prefix; the manifest's recursive glob requires an absolute root. The relocation
test had used an absolute prefix and therefore missed the problem. Core archives
from CPack had correct inventories; desktop packaging regenerated complete ones.
Every non-provenance file in each original extension was byte-identical to its
corresponding core archive. Before publication, rebuilt the three extensions
from those verified core archives with the tagged `make-mcpb.py`, regenerated
`SHA256SUMS` and replaced only those four draft assets. Each extension differs
only in `share/agent-3d-cad/provenance.json`; no runtime bytes or tag changed.
Downloaded the replacement assets again and verified exact byte equality.

All **15 release assets** matched SHA-256; all **13 packages** passed full file
inventory/hash, architecture, version, OCCT and license checks. macOS bundles
record the intended minimum macOS 15.0. Desktop Rust source archives also match
their dependency-index and Cargo.lock hashes. Evidence, full CI logs, manifests
and test outputs are under `build-desktop/release-v0.1.0-preview.1/` locally.

Downloaded macOS arm64 desktop smoke passed with empty PATH: version, create,
edit, reopen, historical MCP read, embedded viewer resource, three client configs,
STEP/STL/PDF/SVG and four DXFs, with unchanged source after export. The native
Tauri window rendered the saved plate, shared revision-2 `face-11` and `edge-22`
with a separate CLI process (`stale: false`), reopened the project from the
library, saved a valid 79,262-byte STEP through the native dialog, then followed
a thickness edit to revision 3 / 10 mm and cleared the old selection. This tested
archive is byte-identical to the published macOS arm64 desktop asset.
After publication, ran the release's unmodified `install.sh` against its public
GitHub URLs with only OS utilities on PATH, installing into a new directory with
spaces. The installed executable reports `0.1.0-preview.1` / OCCT 8.0.1, and all
**515 installed file hashes** match provenance. No client settings were changed.

Follow-up source fix: normalize the install root to an absolute path, reject an
empty generated inventory, use an absolute CI install prefix, and have both
Python packagers reject empty/duplicate/missing/unlisted file inventories before
writing outputs. The relocated-bundle test now deliberately uses a relative
prefix. It reproduced `Bundle provenance is empty` before the fix and passed the
full create/edit/reopen/exports/drawings/BOM/MCP smoke afterward. New packaging
fixtures reject all four inventory defects in both helpers and accept a complete
MCPB; CI runs these fixtures. Local CTest `installer` and `notices` passed **2/2**.
These packaging guards are a follow-up on main, separate from the immutable
release tag and its tested runtime binaries.

Remaining work: actual Claude extension installation and OpenCode/Grok/Muse host
trials, Windows/Linux GUI checks, publisher signing and macOS notarization.
The release is explicitly a preview; these remaining checks are not implied by
successful CI or by macOS GUI evidence.

## Windows desktop packaging encoding — 2026-10-07

The same run `37685717405`, Windows job `113012879023`, passed **32/32 CTest
suites**, schema/SDK checks, Windows installer/configuration checks, relocated
bundle validation, **2/2 Tauri integration tests**, and the Rust release build.
Packaging then failed decoding `cargo metadata`: Python 3.13 used Windows CP1252
for Cargo’s UTF-8 JSON, encountering undefined byte `0x81` in dependency metadata.

`packaging/package-desktop.py` now explicitly uses UTF-8 for Cargo/rustc output
and its text manifests/notices. Reproduced the original `UnicodeDecodeError`
locally against real Cargo metadata by forcing subprocess’s default decoder to
CP1252, then ran the complete corrected packaging helper under that same forced
default. It produced the desktop archive and all **515 provenance hashes**
verified. The output is under `build-desktop/packaging-utf8-check/`; source logs
are in `build-desktop/ci-intel-37685717405/windows-job.log`. This is a portable
encoding regression check on macOS, not a claim that the corrected Windows CI
packaging lane has already passed. No runtime or geometry changes.

## Intel CI resource-accounting regression — 2026-10-07

Run `37685717405` at `348adbd`, Intel job `113012878963`, failed only
`performance_geometry` among 32 CTest suites. Its exact projected curves passed:
140 left-thread curves / 9,100 samples and 258 right-thread curves / 16,770 samples,
maximum right-thread deviation 3.77814e-9 mm. The failure was the newly reported
`view_budgets[1].points`: **633 vs 634**, reflecting the already documented Intel
adaptive polyline resampling (22 vs 23 vertices) after B-rep restoration.

Reproduced the exact failure locally by downloading and replaying that job’s
`right-1.25.json`. The final front-view entities contain 607 vs 608 points; each
budget also charges the same 26 points removed by hidden-line clipping. Geometry
is equivalent under the existing checks; requiring equal accounting totals was
inconsistent with permitting geometrically equivalent adaptive sampling.

Only `tests/performance_geometry_tests.cpp` changes behavior. Its drawing
comparison checks one budget per view, nonnegative integer counters, existing
per-view/aggregate resource limits and coverage of emitted geometry. It compares
point-budget overhead after subtracting actual emitted points, requiring the
reported delta to match the verified sampling delta exactly. Entity/edge budgets,
other metadata, endpoint checks, exact curve trims/types/visibility and the
0.02 mm continuous polyline bound remain unchanged. Negative controls reject
under-counting, unexplained over-counting, malformed/missing budgets, altered
entity/edge counts, over-limit usage and changed geometry. Legacy archived evidence
without budgets still replays. Exact-curve checks now precede sampled comparison
in replay mode as they already did in fresh evaluation.

Validation: both actual Intel fixture replays passed; right-thread continuous
bound **0.00451555 mm**, left-thread bound 2.45015e-13 mm, with the same exact
curves/sample counts above. Local macOS arm64 rebuilt the regression executable;
`ctest --test-dir build-desktop -R '^(performance_geometry|cache)$'
--output-on-failure` passed **2/2 in 26.23 s**, including **69,402 geometry checks**,
exact physical occlusion, streaming roots and balloon visibility. Evidence lives
under `build-desktop/ci-intel-37685717405/`. No production geometry, tolerances,
SDK/cache identities or tool/document contracts changed. Fresh Intel CI remains
the acceptance gate for this correction.

## Installation and standalone Tauri viewer — 2026-10-07 (preview implementation)

The user requested installation-focused documentation, easy client setup,
versioned artifacts, a standalone viewer for CLI agents, export controls and
access to older projects. They chose Tauri over Electron and asked whether Qt
Quick would be simpler. Tauri reuses the existing WebGL renderer and selection
controller; Qt Quick 3D would require another renderer/picking path, and Linux
Qt WebView would require Qt WebEngine. No Electron dependency remains in source.

**Implemented:**

- README now starts with install/connect/use. Developer material moved to
  `docs/DEVELOPMENT.md`; `GETTING_STARTED.md` covers desktop/CLI/ChatGPT web
  differences, documented client configs and actual verification limits.
- `VERSION` is `0.1.0-preview.1`; native version/MCP identity/provenance/archive
  names share it. The Tauri build checks Cargo/config version alignment.
- Native `config --client claude|codex|opencode --workspace PATH` prints settings
  without changing host configs. `viewer --workspace PATH [--document ID]
  [--view ID]` opens the bundled Tauri app using direct process arguments.
- `desktop/` uses pinned Tauri 2.12.1, the OS webview and the same `web/` assets.
  A bounded Rust stdio client talks to the native service. Page IPC exposes only
  the required viewer tools/workspace dialogs/exports, checks the local sender
  and view, and exposes no arbitrary process or filesystem command. Startup and
  read-only status queries retry transient workspace locks; mutations are not
  automatically retried. Native workers still own all geometry.
- Standalone workspace picker, recent workspaces (12 paths), model search,
  revision following, face/edge picking and copied reference-qualified requests.
  Picks are shared across processes via `cad_context` in the same workspace and
  view. Copied requests include the workspace. Direct chat-composer delivery
  remains dependent on the host; Tauri cannot address an arbitrary chat.
- STEP/STL/PDF/SVG/DXF export from the displayed committed revision. Bounded
  `cad_job` work keeps the UI responsive; native Save dialogs copy validated
  workspace export artifacts. DXF creates a new destination folder for four
  views. Default drawings are A4 standard views; dimensions/layouts use tools.
- Bash/PowerShell installers detect the platform, verify release SHA-256, install
  into fresh version directories, and preserve workspaces and existing versions.
  Python packaging helpers assemble Tauri archives, binary Claude `.mcpb`
  extensions and complete release checksum manifests. Desktop provenance covers
  resolved Rust source archives with their full notices. The Windows shell also
  receives the bundled MSVC runtime beside its executable.
- CI retains five native lanes, adds adapter/installer checks and desktop builds,
  and prepares a **draft prerelease** only for a matching `vVERSION` tag after all
  lanes pass and all 15 expected assets exist. Version tags and public releases
  remain separate from pushing this implementation to main.

**Executed evidence (macOS arm64 only):**

- Release C++ build in `build-desktop` using `.deps/hlr-streaming-sdk` and pinned
  local nlohmann source. `ctest --test-dir build-desktop --output-on-failure -j3`:
  **32/32 passed**, 37.47 s, including the new Tauri JS bridge test and existing
  real MCP select/edit/refresh and WebGL suites.
- `cargo test --locked --offline --manifest-path desktop/Cargo.toml` with
  `CAD_SERVICE_EXE` set: **2/2 passed**, 2.10 s. Reopens an older project, exports
  STEP, validates all five desktop export paths (including four DXFs), checks
  nonempty files/PDF and STEP headers, unchanged source and recent-workspace state.
  The first combined run exposed a transient `workspace_busy` on job polling;
  bounded read retries fixed it. Release Cargo build then passed without warnings.
- `schema_conformance.py`: **295 checks across 19 tools**.
  Official MCP SDK 2.3.0 smoke: **837 interoperability checks passed**.
- `bundle_smoke.cmake` with fresh `build-desktop/bundle-check-final`: relocated
  create/edit/reopen/query/STEP/STL/drawings/BOM/balloons/MCP/app resource passed
  with empty PATH. Log: `build-desktop/bundle-check-final.log`.
- POSIX installer fixtures passed verified install, spaces, existing-version
  refusal, corrupt/duplicate checksum rejection, invalid version and cleanup.
  All three client configs round-tripped (including Unicode workspace path).
  `bash -n`, Python compilation and YAML parse passed. Release-manifest fixture
  accepted the complete 15-asset matrix and rejected missing/extra assets.
- Packaged macOS release core archive, Tauri archive and `.mcpb` under
  `build-desktop/release-packages/`. Desktop provenance verified **515 files**;
  `.mcpb` ZIP integrity, native executable permissions and manifest/version checked.
  These are local unsigned/ad-hoc-signed preview artifacts, not published releases.
- Actual Tauri WebKit window opened a saved bracket in isolated
  `build-desktop/review-workspace`. Native face and edge picks were read back by a
  separate CLI process. Resolving selected revision-1 `edge-24` produced one
  geometric selector; a 1 mm fillet committed revision 2. The same window refreshed
  to one valid solid, 11 faces/27 edges and cleared the obsolete pick. Native Save
  dialog wrote `angle_bracket-r2.step`. Evidence: `selection-evidence.json` and the
  isolated workspace under `build-desktop`. The packaged release app also rendered
  revision 2 with the new shared-selection guidance. Its CLI launcher populated
  the separate `release_review` view after macOS GUI access was allowed outside
  the execution sandbox.

- Connected-chat check: the existing Codex MCP configuration uses
  `~/Documents/Agent3DCAD`, whereas the first window used an isolated test
  workspace. Switched the packaged Tauri window through its native workspace
  picker to the configured folder, opened the existing `viewer_loop_plate` at
  revision 2, and clicked its top face. This chat’s actual connected
  `mcp__agent_3d_cad__cad_context(view_id="main")` returned the same current
  `face-5`, area 2,340 mm², center (30, 20.5, 12), normal +Z and `stale: false`.
  No document edit was made in the user's workspace. The window remains on the
  shared workspace, with the selected face available to this chat.

**Limits / next work:** Run the changed five-platform CI, exercise the Windows
installer and Linux/Windows GUI, validate actual Claude `.mcpb` installation and
OpenCode/Grok/Muse client integration, and arrange publisher signing/macOS
notarization. Linux needs GTK 3/WebKitGTK 4.1; Windows needs WebView2. The portable
installer does not install those OS components or add app shortcuts. ChatGPT web
requires an explicit remote/tunnel connection; no hosted endpoint is implemented.
The library reopens older projects at HEAD; a historical-revision browsing UI,
inline dimension editing and direct CLI-chat composer adapters remain future work.
No release or platform support claim follows from workflow source alone.

## License chosen: MIT — 2026-10-07

The owner chose the MIT License for the original code. Added `LICENSE` (copyright
holder: Colin Faulkingham, taken from the repository owner's identity; adjust if an
employer or other party holds the copyright), updated `NOTICE`, `README.md`,
`CONTRIBUTING.md`, `AGENTS.md`, `docs/DEPENDENCIES.md`, `docs/DISTRIBUTION.md` and
`packaging/THIRD_PARTY.md`, and made bundles install `LICENSE` and `NOTICE` beside
`THIRD_PARTY.md`. The `notices` CTest now requires the MIT text and agreeing
wording. Third-party components (OCCT LGPL 2.1 + exception, nlohmann MIT, FreeType)
keep their own terms. The owner also decided that previews with no outside users
need no strict-on-mutation handling of stored revisions: the stricter kernel checks
apply on every evaluation (see below). Signing/notarization remain open.

## Review remediation — 2026-10-07 (branch `fix/review-findings`, not yet in CI)

A four-area code review of `main` at `6acddef` (geometry kernel, job/document layer,
MCP and live viewer, build/CI/docs) produced the "Fix first" and "Important" items
below. Each was fixed test-first on `fix/review-findings`, then re-reviewed by two
independent read-only reviewers; their findings were also addressed (the
retroactive-evaluation point by documentation only, see below). **Nothing has run
on Linux or Windows CI yet** (see "Unverified" below), and no remote push was made
by this work.

**Behavior and contract changes** (details in `docs/PROTOCOL.md`, `SPEC.md`,
`DRAWINGS.md`, `ASSEMBLIES.md`, `LIVE_VIEWER.md`):

- *Kernel.* A STEP import must transfer every root (`kernel_failure` with
  `transferred_roots`/`total_roots`); a `hole` must remove more than 1e-6 of its own
  cylinder volume (`invalid_model`, with `removed_volume_mm3`/`hole_volume_mm3`); a
  pattern or assembly may replicate at most 4,096 solids / 65,536 faces per feature
  (`limit_exceeded`); fillet candidate IDs come from the input feature's own edges;
  BOM CSV text cells that spreadsheets read as formulas get a leading `'`; OCCT
  failures with empty messages are named by exception class. **These checks apply to
  every evaluation, including revisions committed by earlier builds**: such a
  revision stays on disk and readable with `cad_read`, but queries, views, exports
  and drawings of it fail with the feature-level error until `cad_apply` fixes the
  feature or `cad_restore` returns to an earlier revision. A strict-on-mutation or
  versioned rule set was considered and not built; revisit if previews have users.
- *Job store (`cad_job`).* `jobs/<id>/` now holds a small `state.json` (schema 2),
  `request.json` and `result.json`; the result is written before the state flips to
  succeeded and a valid result recovers a job whose final save failed. Admission and
  list never parse results. Damaged, symlinked or foreign records show as `failed`
  with `job_record_corrupt` and never block admission. Finished jobs are deleted
  after 7 days or beyond the newest 256 (rename to `.trash-*` first). Records from
  the previous layout are still read and migrated. Unknown job IDs return
  `not_found`; submit validates arguments with the service's own contract.
- *Workers.* Geometry workers and coordinators spawn the running executable, not a
  sibling found by file name (a renamed binary works; `renamed_worker` CTest). Test
  programs call `set_worker_executable`. Windows children inherit only the NUL
  handle (STARTUPINFOEX handle list).
- *Service/storage.* Mutation admission and publication wait up to 5 s (2–50 ms
  backoff, no lock held between attempts) before `workspace_busy`; viewer and job
  admission stay non-blocking. `Service::call` is the only place exceptions become
  errors (JSON → `invalid_argument`, filesystem → `storage_error`, else
  `internal_error`), so MCP, CLI and jobs report identical codes. Request replay uses
  `documents/<id>/receipts/` (O(1), falls back to a verified scan; old workspaces are
  backfilled). Non-UTF-8 STEP imports are `invalid_argument` with `byte_offset`.
  macOS commits use `F_FULLFSYNC`. New documents, job requests and views may not be
  named like Windows devices (CON, PRN, AUX, NUL, COM0–9, LPT0–9); existing
  documents with those names keep working.
- *MCP/viewer.* Blank lines, CRLF and stray client responses no longer produce
  replies; the tool catalog is built once and each schema carries only the `$defs`
  it references (compact `tools/list` 1,058,723 → 263,401 bytes as measured by the
  transport work; pretty-printed CLI `tools` 2.84 MB → 0.73 MB against an earlier
  local build). Viewer sync `error` states are delivered (`CadBridge.value` throws
  only on `isError`). Edge picking allows twice the mesh's linear deflection for
  occlusion in both the live and offline viewers (98% of truly visible nozzle edge
  samples pickable, from 73%). Frozen evaluations are bounded: one per view, plus a
  throttled sweep of superseded metadata that skips damaged view records and never
  follows a symlinked marker.
- *Drawing cache.* Projections are cached **per view** (key: geometry, the view's
  definition without its name, hidden-line choice). Adding, removing, reordering or
  editing one view projects only that view. Each entry records its budget usage
  (entities, points, examined edges, including hatch regions and hidden-line
  fragments) and `check_drawing_totals` re-enforces the drawing-wide limits and the
  64 balloon anchors on every request, so a request passes or fails identically
  whether its views are cached. Diagnostics: `projection_keys`, `projection_hits`,
  `projection_hit` (all hit); the single `projection_key` is gone. The performance
  test records the cold single-view M20 drawing time as evidence
  (`m20-front-drawing-timing.json`, 0.47 s here) without asserting on it.
- *Parallel hidden-line projection.* A drawing with two or more uncached views
  now projects them in separate worker processes (an internal `projection` request;
  results travel only through files; the coordinator's own slot runs the first
  view, then a final worker renders from the supplied projections). It uses only
  worker slots that are idle at that moment and never waits for one, so the
  workspace-wide limit of four workers still holds and, with no idle slot, views run
  one at a time with identical results. Each worker has the request's full
  `memory_mb` (a drawing can use up to 4×) and the remaining wall-time budget;
  cancellation, a deadline or any failure stops every worker, reports the failing
  view and publishes no cache entry (entries are published only after the whole
  drawing succeeds). Diagnostics add `projection_workers`. Measured on this Mac with
  `tests/cache_benchmark.py --views standard --cold-only` (full M20 knob, four
  views): **197 s** on the previous serial build (`build-app-protocol`, same
  streaming SDK) versus **135 s** now, about 1.46×; the gain is bounded by the
  slowest view. Tests: parallel equals serial output and cache entries, partly warm
  drawings, a failing view (named, nothing published, no leftover directories) in
  `cache_tests.cpp`; a deadline mid-drawing leaves no process, temp directory or
  cache entry in `jobs_tests.cpp`.
- *Notices/CI.* `packaging/THIRD_PARTY.md` named superseded `midpoint-v1`; it now
  names v2, states the OCCT archive hash and relink steps (`lib/` on macOS/Linux,
  `bin/` on Windows), and a CTest (`notices`) fails on drift. Added `NOTICE`
  (original-code license then undecided; MIT was chosen afterwards, see the
  licensing entry above), `SECURITY.md`,
  `CONTRIBUTING.md`. CI artifacts keep 14 days instead of 90. `AGENTS.md` no longer
  claims no remote exists. A tracked `.pyc` was removed and `.gitignore` extended.

**Test evidence** (macOS arm64, local, Release, OCCT 8.0.1 `hlr-streaming-sdk`,
`agentcad-hlr-midpoint-v2`): `cmake --build build-wip --parallel` has no warnings;
`ctest --test-dir build-wip --output-on-failure -j 3` passes **31/31** (the 28
earlier suites plus `notices`, `renamed_worker` and the new `service` suite; about
36 s). The timing-sensitive suites (`jobs`, `service`, `live`, `cache`,
`transactions`, `app_protocol`, `live_mcp_flow`, `renamed_worker`) passed four
consecutive repeats. `tests/schema_conformance.py`: **295 checks across 19 tools**;
`tests/mcp_sdk_smoke.py` (official MCP SDK 2.3.0, native stdio): **752–852 checks**
(varies with asynchronous polls; 852 on the final run). The final tree was also built from an empty build
directory (no warnings) and passed the same 31/31, and `bundle-check` (relocated
bundle, empty PATH: create/edit/reopen/query/STEP/STL/drawings/BOM/balloons/MCP and
embedded app resource) passed. Two
test races found and fixed on the way (a jobs admission-lock race that failed about
half of runs; a lower-bound timing assumption in the lock-wait test).
Test changes to flag for review: the heavy workload used by the cancel/kill/timeout/
memory tests (`jobs_tests.cpp`, `mcp_sdk_smoke.py`) is now a 64×64-pin grid cut from
a plate (4,096 solids, ~15 s, ~355 MB peak) because the old 64³ nested pattern is
now rejected up front by the replication budget; the assembly-drawing assertion
"assembled and exploded cache keys differ" became per-view (the two views still
have different keys, and a view with its explode offset removed now correctly reuses
the assembled view's projection). No tolerance or deadline was loosened except the
memory-limit job's wall deadline (10 s → 30 s) and its poll bound.

**Unverified — CI must confirm** (Windows code cannot be compiled locally):
`CreateProcessW` handle list and `temporary_directory` on Windows (`jobs.cpp`),
`close_lock(HANDLE&)` and `fs::rename` replacing `receipts/coverage.json`
(`storage.cpp`), whether `fs::rename` can move job directories during retention on
Windows, the cmd.exe quoting in the service test's CLI helper, and the `jobs`,
`service`, `live` and `renamed_worker` suites on Windows and Linux. Parallel view
projection is also unverified on both: one Job Object per worker on Windows, up to
four RLIMIT_AS-bounded workers on Linux, and the new drawing test's "no worker
process left" check uses `ps` and is POSIX-only. The Linux " (deleted)" executable
case is covered only by a unit test of the path logic.

**Not done / owner decisions.**
- Cold complex thread drawings still take minutes (the slowest single view bounds
  the parallel speedup); profiling the remaining exact root/trim work is next.
- Existing revisions are re-evaluated with the stricter kernel (see above).
- Live job results (`jobs/live_*`) are bounded only by the job retention policy
  (up to 256 × 64 MiB worst case).
- Review the LGPL/relink wording in `packaging/THIRD_PARTY.md`, and enable GitHub
  private vulnerability reporting (it is currently off) or name another private
  channel in `SECURITY.md`.
- Review items left for later: pin GitHub Actions by SHA, add `concurrency`,
  Debug/sanitizer lanes, split this file into status/history, remove M0-era text
  from `SPEC.md`/`ROADMAP.md`, document the glibc 2.39 / macOS 15 floors.

Next: push the branch, let all five CI lanes run, fix anything Windows/Linux-only,
then merge.

## README marketing banner — 2026-10-07

Added `docs/assets/readme-banner.png` above the README title. The 2120 × 742
banner pairs the project name and native/editable CAD tagline with a cyan
blueprint-to-metal bracket illustration. It was generated with the built-in
image generation tool; the exact prompt is saved in
`docs/assets/readme-banner.prompt.txt`. The bracket is illustrative, inspired by
the example model, and is not a service-rendered geometry or UI acceptance image.

Validation: visually inspected the generated banner for spelling, readable
typography, composition and geometry; verified PNG dimensions and the README's
relative asset path; `git diff --check` passed. Documentation/assets only;
native CTest suites were not rerun. Next: review the banner in the GitHub README
when these local changes are published. The banner itself changes no native
implementation or release gate.

## Intel regression resolved and current native matrix validated — 2026-10-07

Native source commit `541e3bf7f68224dfd8512ff49cda88ec85932469` passed the
[complete five-platform workflow](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37650153048).
This supersedes the pending Intel/native-platform status in the historical notes
below. OCCT remains pinned to 8.0.1 with `agentcad-hlr-midpoint-v2`.

The Intel failure was the right-handed D12, pitch 1.25, length 3.75 thread's front
hidden entity 78: 22 versus 23 adaptive polyline vertices after B-rep roundtrip.
Diagnostic runs `37648289620` / `416b3ae` and `37648841811` / `bf274c7` retained
both actual projections and source snapshots while preserving the strict failure.
The snapshots are byte-identical, endpoints are unchanged, and the bidirectional
continuous polyline-distance upper bound is **0.00451554744 mm**, within the
existing 0.02 mm drawing tolerance. Intel independently passed 398 exact projected
curves / 25,870 paired samples with maximum sampled deviation **3.77814108e-9 mm**,
plus the physical occlusion, balloon visibility and streaming-root checks. The
B-rep checks are finite numerical evidence, not a continuous-domain proof.

The corrected regression permits differing adaptive polyline vertex counts only
after the exact visible/hidden curve checks pass. A bidirectional continuous
polyline-distance bound must meet the existing 0.02 mm drawing tolerance. Curve
counts, visibility metadata, endpoints, and equal-size numeric arrays retain
strict comparisons. Negative controls reject changed visibility, endpoints,
interiors, curve types and missing curves. The native test can replay an archived
fixture JSON directly; both Intel archives passed replay. Production geometry and
tool/document contracts are unchanged. Exact projection capture is an optional
bounded internal kernel diagnostic; ordinary tool calls do not request it.
CI retains native test logs, exact projections, snapshots and metrics on success
as well as failure.

Final CI evidence (all **28/28 CTest suites** passed on every lane):

| Platform | CTest seconds | Schema checks / tools | Official MCP SDK 2.3.0 checks | Performance geometry checks |
|---|---:|---:|---:|---:|
| Linux x64 | 50.50 | 293 / 19 | 437 | 69284 |
| Linux arm64 | 66.74 | 291 / 19 | 442 | 69493 |
| macOS arm64 | 83.24 | 291 / 19 | 517 | 68977 |
| macOS Intel x64 | 236.23 | 295 / 19 | 567 | 69411 |
| Windows x64 | 125.47 | 285 / 19 | 457 | 70467 |

Schema/SDK totals include successful asynchronous polls and therefore vary with
execution timing. Every lane passed relocated empty-PATH create/edit/reopen,
STEP/STL, drawings, assemblies/BOM/balloons, MCP and embedded app-resource checks,
then produced its native preview archive. Both Linux lanes additionally passed
the clean runtime container with no Python, Rust, Node, CMake or compiler.

Independently downloaded all five archives from that exact run and verified
**1,365 manifest-listed files**, complete file inventories, executable binary
architectures, OCCT version/modification notices and original SDK provenance.
The final runtime files match their post-relocation hashes; the original SDK hash
matches the v2 modification manifest. macOS archives target **15.0**. Archive hashes:

| Platform | Archive SHA-256 |
|---|---|
| macOS arm64 | `6600b6a44da03b09e260dbfc60184b2d1bfccd9785160785787db40e8e3b1b37` |
| macOS Intel x64 | `de3f6f26eed24b3563c47f1e1e8edbd5b7d8c9e4000b60be46a0d76aed985621` |
| Linux arm64 | `db3e541e4d739ec5b49dd0f10eada6cc1a85df4d1352e96d8695bc59daf7814c` |
| Linux x64 | `0e1767a9b3ce362af5e97f06725a13c425605d02842c560fcd71193bac8c5f99` |
| Windows x64 | `2bcca17bf6673829d3661849e0c0773a8c3a7b1959c61762a1d45c48d4dc8cd7` |

Local macOS arm64 also passed `cmake --build build-app-protocol --parallel 4`,
`ctest --test-dir build-app-protocol --output-on-failure` (**28/28 in 46.12 s**;
**69,055 performance geometry checks**) and native `bundle-check`.
Logs, downloaded artifacts, verification scripts and exact commands/results are
under `build/intel-perf-fix/`; the consolidated machine-readable record is
`validation-report.json`. The committed native code/tests/workflow match the
validated source; subsequent documentation updates do not change those inputs.

Limits: this validates the current native preview/SDK/bundle matrix. Independent
offline browser/GPU-host interactions, original-code licensing, signing/notarization
and public release preparation retain their separate acceptance gates. Complex
cold thread drawings still take minutes. Next: profile remaining exact root/trim
work before further optimization, or scope a workflow-driven product increment.

## Codex local installation update — 2026-10-07

The user authorized upgrading the installed CAD MCP after discovery showed that
Codex still pointed at the older October 6 bundle. The current macOS arm64 native
bundle, including assemblies/BOM, expanded drawings, live assembly visibility and
`agentcad-hlr-midpoint-v2`, is now installed in a fresh version directory under
`~/Applications/Agent3DCAD/`. Prior installations remain available.

- `cmake --build build-app-protocol --parallel 4` passed. Relevant CTest suites
  `installer`, `embedded_assets`, `runtime_filters`, `protocol`, `cli_smoke`,
  `app_protocol` and `live_mcp_flow`: **7/7 passed in 3.48 seconds**.
- `bundle-check` passed relocation and empty-PATH create/edit/reopen, exchanges,
  drawings, BOM/balloons, MCP and embedded-resource checks. The local installer
  independently verified **263 files** before and after copying. Provenance
  SHA-256: `05e530b6e924390d9074d55b5832bde89c77b453f5722eead74149f5690aaa6a`.
- The installed executable passed **291 schema checks across 19 tools** and
  **497 official MCP SDK 2.3.0 interoperability checks**.
- A byte-preserving atomic update changed only the registered CAD executable
  path. The matching global native-cad skill was refreshed; timestamped config
  and skill backups were retained. Existing workspace arguments and all unrelated
  configuration bytes were preserved. `codex mcp get agent-3d-cad --json`
  confirmed the new enabled registration.
- An independent SDK connection using that actual registration discovered all
  **19 tools**, including `cad_bom`, and read all **four existing documents**.
  All **10 saved HEAD/revision JSON files** retained their pre-update SHA-256.

Exact installation paths, backups, registration, preflight source hashes and logs
are in `build/codex-update-20261007/`. No remote publication occurred. The current
chat still exposes the previous 17 model-visible tools; `cad_viewer` is app-only.
Computer Use explicitly denied access to Codex's own UI, and no automated MCP
refresh tool is available. The user must restart the MCP connection or Codex to
discover the new tool schemas and viewer resource in the running host. Fresh SDK
acceptance is verified; a refreshed Codex-host session remains to be observed.

## Intel CI follow-up — diagnostic context

The reported GitHub Actions run `37609850499`, Intel job `112754188016`, tested
commit `81495027f501563d53e8d91f48f0993b52277d9f` (v1), before the v2 increment below.
Its `performance_geometry` test failed after 60.43 seconds with only
`Equivalent array lengths`; the other four native lanes passed. The existing log
does not identify which array differs, so neither a visibility defect nor harmless
sampling differences have been established. Do not relax that assertion on this
evidence alone.

The JSON regression helper now reports the full comparison path and mismatched
array lengths/numeric values; restored-drawing diagnostics also identify thread
handedness and pitch. All comparison rules and tolerances remain unchanged.
`cmake --build build-app-protocol --target cad_performance_geometry_tests
--parallel 4` and `ctest --test-dir build-app-protocol -R '^performance_geometry$'
--output-on-failure` passed on macOS arm64 in **13.90 seconds**.

This local host has no usable x64 execution environment. A prepared diagnostic CI
branch would use the known v1 source/Intel SDK cache and run only the Intel lane.
Automatic approval review rejected uploading its test/workflow files and creating
remote GitHub objects without explicit user authorization. Approval was requested;
no diagnostic branch or remote source objects were created. Separately, v2 run
`37619754246` was already building native SDKs when inspected. Its Intel result is
still unverified here.

Next: obtain the exact failing comparison and source/result evidence on Intel,
then fix the underlying behavior or prove geometric equivalence with the existing
visibility/occlusion checks before changing any comparison contract.

## Current increment — exact HLR root streaming and face-grid reuse

Implemented `agentcad-hlr-midpoint-v2` in the pinned OCCT 8.0.1 recipe:

- Existential midpoint checks and `Compare` can stop solving ray/surface roots
  after the first root passes the existing depth, periodic-UV and trimmed-face
  classification. Rejected roots continue to later candidates. The sorted
  brackets, parameter bounds, exact solver, point construction, duplicate
  suppression and tolerances are the same as v1. Midpoints retain their dense
  first attempt and original-grid fallback; `Compare` retains its original grid.
- A private owner retains both the dense and original polyhedra for the currently
  loaded face. Alternating queries select the same sampling dimensions instead of
  repeatedly evaluating the surface and rebuilding identical grids. At most two
  grids are retained; `Load` and destruction release both. No global cache, copied
  polyhedra or new geometry approximation is introduced.
- Count-dependent classification retains the complete original intersection
  path. Existing native symbols and object layouts remain unchanged; the additive
  `PerformUntil` method supplies only an existence result, not a complete root
  inventory. Model documents and transport/tool contracts remain unchanged.
- The checked five-file script accepts upstream sources, upgrades exact known v1
  sources, reapplies safely to v2 and rejects unknown input. SDK and bundle
  manifests now identify v2 and verify the selected TKHLR hash. Complete modified
  source, upstream attribution and the reproducible patch accompany bundles.
- Direct native regressions compare streaming and complete root inventories,
  all-rejected queries, first/later accepted roots, empty queries, repeated
  grid/face changes against independently rebuilt grids, analytic supports and
  exact source-surface residuals.
  Older SDKs explicitly report that these additive-method checks are unavailable.

Executed on macOS arm64 with OpenCascade 8.0.1:

- Rebuilt TKHLR in the existing pinned Release dependency tree and staged its
  matching native headers/notices alongside the SDK's unchanged dependencies.
  The staged library uses sibling loader resolution. No global installation.
- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **28/28 passed in
  44.95 seconds**, including **41,074 performance geometry/root checks** in
  **14.40 seconds**.
- Compared exact projected B-rep curves against the frozen v1 patch on **29
  views**, covering handedness, pitch/length, rotated parts, overlapping assemblies,
  fused geometry and a 100x scale case. **255,020 bidirectional samples** matched
  both visible and raw hidden curves within 2e-5 mm; the largest sampled deviation
  was **8.388e-08 mm**. This is finite numerical evidence, not a continuous-domain
  proof. The three previously timed-out long axial diagnostic baselines were not
  rerun in this matrix. Report: `build/drawing-perf-next/final-matrix-report.json`.
- A fresh native full M20 knob, default four-view drawing completed cold in
  **189.782 seconds**, versus **196.185 seconds** for the frozen v1 service/SDK:
  a **3.26% reduction** in this local paired case. Both used empty caches, the
  unchanged source, 300000 ms bounded job budgets and no concurrent geometry/build
  work. This is a modest measured improvement; complex first drawings still take
  minutes. Command: `python3 -u tests/cache_benchmark.py
  build-app-protocol/agent-3d-cad build/drawing-perf-next/native-final
  --views standard --cold-only --timeout-ms 300000`.
- All **40,962** native projection comparisons matched with zero deviation, and
  all six cold PDF/SVG/DXF files were byte-identical to v1. Three cached redraws
  took **0.153, 0.151 and 0.154 seconds** (median **0.153**), with identical files.
  A3 first-angle restyling with dimensions took **0.150 seconds**, reused unchanged
  cache files and preserved the revision-1 source. Report:
  `build/drawing-perf-next/final-paired-report.json`; native result:
  `build/drawing-perf-next/native-final/cache-benchmark-ilisjwpn/report.json`.
- Fresh, CRLF and known-v1 source application, repeated v2 application and
  unexpected-source rejection passed. Regenerating SDK notices removed the obsolete
  v1 binary manifest and retained the matching v2 manifest. The dependency recipe
  configured.
- Native `bundle-check` passed relocation and empty-PATH create/edit/reopen,
  geometry, drawings, assemblies/BOM/balloons, MCP and embedded app-resource smoke.
  Pairing the previous v1 TKHLR with v2 notices was rejected before publication.
  Logs: `build/drawing-perf-next/dual-grid-ctest.log`,
  `dual-grid-bundle.log`, `final-mismatch.log` and
  `final-patch-checks/report.json`.

Limits: exact HLR retains its existing numerical limitations; count-dependent
ray solving still exhausts the full root inventory. These sources and the modified
SDK have local macOS evidence, not a new Windows/Linux certification. Existing SDKs
need the updated dependency recipe to use streaming. No remote push or release.

Next: profile remaining exact root solving and trim classification before another
optimization; validate the updated SDK on the other native lanes. Full-count ray
queries were inexpensive in the top-view probe. Repeated-ray memoization and
spatial-index reuse did not provide a worthwhile benefit and are not included.

## Previous increment — assembly visibility and cold drawing optimization

Implemented live-view presentation controls:

- Assembly parts have Hide/Show and Isolate controls in the inspector, with
  Show all and an all-hidden recovery action. Controls remain available in narrow
  layouts. Rendering, picking, highlights and captures exclude hidden parts;
  Fit frames the visible geometry without changing the camera when hiding a part.
- `hidden_part_ids` persists per `view_id`, is returned by `cad_context` and ready
  `cad_viewer` sync responses, and is included in agent request context. The native
  service validates part IDs and the current displayed evaluation under the final
  view/document locks. Hiding a selected part clears its pick; attempts to select
  hidden geometry fail explicitly.
- Same-document revisions retain surviving part IDs and prune removed instances.
  Changing documents resets visibility. Reloading restores the current mask even
  when an older saved selection is stale. The controller protects local visibility
  changes from older in-flight sync responses, surfaces save failures and allows an
  explicit retry. Presentation does not edit the model, revision, BOM or exports.
- The renderer retains the immutable native mesh and ownership IDs, rebuilding
  visible buffers and its picking tree only when needed. Offline `cad_view`
  artifacts retain their existing behavior. Separate clients sharing a `view_id`
  use the last explicitly published mask; independent chats should use distinct IDs.

Implemented cold-projection and output improvements:

- CPU sampling located the M20 knob bottleneck in exact hidden-line ray
  classification against long thread BSpline faces. The pinned OCCT 8.0.1 recipe
  now applies `agentcad-hlr-midpoint-v1`: two midpoint checks that discard root
  counts use bounded denser initial brackets and stop at the first exact qualified
  occluder. An unsuccessful attempt repeats the original grid; every count-dependent
  caller retains the original path. Source geometry, topology, tolerances and
  existing public symbols/object layouts remain unchanged.
- The five-file patch verifies upstream and resulting hashes, accepts CRLF source,
  rejects unknown input and is idempotent. Dependency stamps track patch/notice
  scripts. Installed notices retain attribution, the patch and all five complete
  modified files. Portable packaging verifies its selected TKHLR hash against the
  SDK modification manifest before claiming the patch in provenance. The cache
  identity already includes SDK binaries, so disposable caches invalidate safely.
- Drawing geometry now paints hidden curves before visible curves in PDF/SVG and
  emits DXF entities in the same order. Coincident hidden dashes cannot overlay
  visible outlines in the native sheet. Center marks and other annotations follow
  geometry; cached projections stay unchanged. Curved-geometry regressions cover
  all three formats and `hidden_lines: false`.
- `tests/cache_benchmark.py` supports `--cold-only`, explicit `--timeout-ms`,
  durable failure timing and executable/source hashes. Resume rejects a changed
  executable or view scenario.

Executed on macOS arm64 / OpenCascade 8.0.1 with the modified SDK:

- `cmake --build build-app-protocol --parallel 4` passed, followed by
  `ctest --test-dir build-app-protocol --output-on-failure`: **28/28 passed in
  50.01 seconds**. The new performance geometry suite passed **39,991 checks**
  in 15.22 seconds, covering left-handed/fine-pitch threads, finite projections,
  source/topology nonmutation, restored snapshots, exact sub-chord-tolerance
  visibility and independent rejection of an occluded balloon anchor.
- Native live context **110 checks**, embedded app protocol **93**, UI state
  **103**, real stdio MCP/controller loop **27**, WebGL renderer/native assembly
  mesh **47**, and drawing **1,947** checks passed within that suite.
  **291 schema checks across 19 tools** and **507 official MCP SDK 2.3.0 checks**
  passed (poll counts vary).
- Native relocation `bundle-check` passed with empty PATH, including visibility
  controls, assemblies, BOMs, balloons and verified modification provenance.
  Pairing the original unmodified TKHLR with patched notices was rejected before
  publication. Fresh/CRLF patch application, repeat application, unexpected-source
  rejection and dependency recipe configuration also passed.
- A temporary loopback MCP host served the embedded app from the actual native
  executable. Eight browser interactions verified hiding the cover, isolating and
  fitting a spacer, selecting its visible geometry, clearing a hidden pick,
  all-hidden recovery, Show all, and persisted visibility/camera after reload.
  The document remained at revision 1 with identical source. Evidence and a
  screenshot are in `build/hide-isolate-demo/`; the server was stopped. This is
  browser/native-service evidence, not a new installed Codex-host test.
- The unchanged full M20 knob, default four-view orthographic drawing completed
  cold in **202.674 seconds**, versus the original build exhausting its budget at
  **240.012 seconds**. The original result is a timeout, not its completion time.
  The final run used the integrated service and selected SDK with no loader
  override, fresh caches and no concurrent geometry/build tests. Command:
  `python3 -u tests/cache_benchmark.py build-app-protocol/agent-3d-cad
  build/drawing-perf --views standard --cold-only --timeout-ms 240000`.
- Three cached redraws took **0.157, 0.158 and 0.156 seconds** (median **0.157**)
  with byte-identical PDF/SVG/DXF files. A3 first-angle restyling with dimensions
  took **0.152 seconds** and reused unchanged cache files. **131 native/cache,
  ezdxf/XML and history checks** plus **20 independent pypdf checks** passed.
  All eight DXF audits had zero errors or fixes; projected dimensions matched the
  source. Poppler inspection confirmed complete four-view A4/A3 sheets and legible
  dimensions. A thread-length edit changed geometry and retained revision 1 source.
- Evidence: `build/drawing-perf/cache-benchmark-jljxl88z/report.json` and its
  artifacts/previews, `build/drawing-perf/final-*.log`, plus the original timeout
  in `build/drawing-perf/cache-benchmark-koctfa5m/report.json`. The executable hash
  remained identical after final packaging regeneration. `git diff --check`
  passed. No global installation, remote push or release.

Additional numerical validation compared original and modified exact projected
BRep curves on threads of different hands/pitches/lengths, rotated parts, fused
geometry, overlapping assemblies and a 100x scaled stress case. Of **29 completed
comparisons**, 26 matched visible and uncovered-hidden geometry within 2e-5 mm;
three long-D20 views removed baseline-visible intervals. Source-solid ray audits
found only physically blocked samples in those intervals (right front: 20 samples
across four intervals after scanning all 152 edges; left isometric: five samples
after scanning all 70 edges; left front: a sampled subset of an earlier 116/116
blocked-point audit). No newly visible sample exceeded that tolerance. Three
long top-view baselines timed out at their 35-second diagnostic limit and remain
uncompared. These are finite numerical checks, not continuous-domain proofs.
Details are in `build/drawing-perf/axial-variants/FINAL-EXISTENTIAL.md`.

Rejected faster experiments included splitting surfaces and changing grids for
all classifiers: they exposed occluded edges or changed count-dependent results.
They are not included. The accepted patch retains OCCT's numerical limitations;
first drawings of complex threads still take minutes and need an adequate bounded
`cad_job` budget. Hide/isolate is presentation-only in the live viewer; exported
models, drawings and BOMs still include every part. The modified SDK and these
sources have local macOS evidence, not a new Windows/Linux certification. Existing
unmodified developer SDKs must be rebuilt with the supplied dependency recipe for
the optimization, new geometry regressions and verified portable packaging.

Next: further cold HLR improvements only with exact visibility evidence;
nested/linked assemblies, additional mate types, or multi-sheet/automatic
annotation layout as separately scoped workflows.

## Previous increment — assembly BOMs and part balloons

Implemented through the shared Service, native CLI/MCP and drawing worker:

- Optional assembly `bom` metadata is keyed by source `input`, with explicit
  item numbers, part numbers, descriptions and materials. Rows group repeated
  source instances and count instances rather than solids. Deterministic automatic
  numbering sorts inputs and skips explicit reservations; fixed item numbers
  survive membership changes. `set_bom_item` and `remove_bom_item` use the existing
  atomic revision path, including final-state validation for number swaps.
- `cad_bom` exports committed or historical assembly inventory as JSON/CSV and a
  manifest without rebuilding geometry. Optional `feature_id` selects a named
  assembly. CSV preserves metadata with quoted fields, doubled quotes and CRLF.
  Publication uses a fresh directory, cancellation checks and a final document
  lock. Durable `cad_job` also accepts this tool.
- Drawing `bom: true` adds a wrapped table to PDF/SVG and independent `bom.json`
  and `bom.csv` artifacts, including DXF-only requests. Balloons derive their item
  numbers from the same inventory; callers specify a part, a source-local surface
  point and projected label coordinates. They follow placement, rigid mates and
  view explosion. The kernel snaps within 1e-5 mm of an exact boundary and rejects
  missing, ambiguous or occluded attachment points. These checks do not mutate
  assembled geometry or the saved revision.
- PDF/SVG and 1:1 mm DXF share numbered circles, arrows and leaders; DXF puts these
  on `BALLOONS`. Circles clear geometry bounds, other circles and dimension
  strokes/text, and leaders cannot cross another balloon circle. Layout includes
  annotations without altering measured geometry extents. Invalid or oversized
  layouts fail explicitly instead of clipping or dropping rows/balloons.
- Saved recipes retain parameterized anchors and labels. Anchor/explosion changes
  invalidate projection cache entries; moving labels rerenders cached projections.
  Metadata changes retain complete-model cache invalidation. Old recipes, source
  revisions and artifacts remain immutable. Added an assembled/exploded example
  and updated protocol, assembly, drawing and bundled agent guidance.

Executed locally on macOS arm64 / OpenCascade 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **27/27 passed in
  28.57 seconds**. New suites passed **68 BOM checks**, **88 balloon kernel checks**
  and **58 BOM drawing checks**. Coverage includes grouping, numbering, quoted
  metadata, semantic edits/rollback, historical exports, exact boundary attachment,
  placement/explosion, hidden/ambiguous anchors, annotation collisions, cache
  invalidation/reuse, native formats and asynchronous regeneration.
- **270 schema checks across 19 tools** and **401 official MCP SDK 2.3.0 checks**
  passed (polling can affect counts), including asynchronous BOM export and
  persisted job results across process restart.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  native relocation with empty PATH, including BOM JSON/CSV, quoted metadata edits,
  history, and the sheet table/balloons alongside prior smoke coverage.
- Generated the four-instance plate/spacer example with demonstration part numbers
  and descriptions. Poppler visual inspection confirms the two-row table and four
  correctly numbered balloons. Independent pypdf, ezdxf, XML and CSV readers passed
  **47 checks**: a single A3 page, table metadata, artifact byte counts, matching
  inventories, DXF units/radii/centers/numbers and zero DXF audit errors or fixes.
  Evidence and sample artifacts are under `build/bom-demo/`.
- `git diff --check` passed. No global installation, remote push or release.

Current limits: flat same-document BOMs; at most 64 source rows and 64 balloons,
item numbers 1–999, printable ASCII metadata, one balloon per part per view, and
no section balloons. Label placement is explicit and conservatively clears the
whole projected geometry bounding rectangle. Tables must fit one sheet, including
DXF-only requests; table pagination, automatic balloon routing, hierarchical BOMs,
and part-specific quantity overrides are not implemented. DXFs contain balloons;
the complete table is in the sheet and JSON/CSV sidecars. These sources have local
macOS evidence, not a new multi-platform certification.

Next: nested/linked assemblies, additional mate types, per-part hide/isolate,
or multi-sheet/automatic annotation layout as explicit follow-up workflows.

## Previous increment — editable assemblies and exploded drawings

Implemented through the existing shared native Service, CLI/MCP and bounded
workers, without adding a separate transport or runtime:

- An `assembly` feature contains 1–64 named part instances of earlier editable
  solid features. Repeated inputs share source intent while placed copies retain
  separate exact solids and topology, including coincident instances. Rotation
  about an explicit axis precedes translation. Parts remain a compound; touching
  or overlapping geometry is never silently fused. Aggregate volume includes
  every part, including overlap.
- Named `rigid` mates align explicit source-coordinate datum frames with an
  optional parent-frame offset and rotation. The parent forest resolves in any
  serialization order, fixing all six relative degrees of freedom. Missing
  references, cycles, multiple incoming mates, invalid frames and conflicting
  child placements fail explicitly. Roots use their saved placement or identity.
  A model permits at most 256 total assembly parts and 63 mates per assembly.
- `set_part_placement`, `set_mate` (upsert) and `remove_mate` are atomic semantic
  edits. Existing parameter/feature edits update source parts and datum values;
  previews, revisions, restore, compare, jobs and expected-revision checks use
  the same existing transaction path. Detaching a child returns it to identity
  unless the same batch supplies a placement. Explicit placements are never
  silently discarded when adding a mate.
- Summaries include part inventory, source feature IDs, row-major world matrices,
  bounds, volume and mate relationships. Topology/provenance and mesh edges carry
  part ownership; triangles map through their face IDs. Assembly picks retain
  evaluation/revision lifetime and expose their owning part. They do not propose
  fillet selectors in the wrong coordinate system. The live viewer lists part
  sources and mate parents and displays the selected part.
- Per-view `explode` offsets translate named parts in world mm after mate
  placement, including section views. Parameterized recipes survive regeneration.
  PDF/SVG/DXF views identify exploded geometry, whose measured dimensions include
  the offsets. Drawing generation does not mutate assembled geometry, HEAD,
  ordinary exports or historical artifacts. Projection cache keys include the
  offsets. Cached assemblies reconstruct placement/ownership from exact cached
  source B-reps and saved intent, avoiding redundant compound snapshots or
  reliance on deserialized enumeration ownership.
- Added `docs/ASSEMBLIES.md`, bundled guidance and an editable four-part
  plate/spacer example with assembled and exploded front/isometric drawings.
  STEP/STL preserve placed geometry; saved JSON retains the assembly semantics.

Executed locally on macOS arm64 with OpenCascade 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **24/24 passed in
  27.33 seconds**. New suites passed **88 service checks**, **85 model checks**,
  **5,385 kernel checks**, and **59 drawing checks**. Service counts include job
  polling and can vary. Coverage includes independent/coincident instances,
  full frame composition and inverse child orientation, parameter edits, graph
  rejection, rollback, detach/placement, historical source, cache restoration,
  stale picks, live context, native STEP readback, asynchronous edits, exploded
  sections and regeneration. Existing geometry, jobs, cache, drawing, protocol
  and viewer regressions pass; the WebGL suite now consumes the assembly mesh.
- **219 schema checks across 18 tools** and **345 official MCP SDK 2.3.0 checks**
  passed (polling affects counts), including an asynchronous exploded assembly drawing. Discovery schemas
  cover assembly intent, edits, inventory, ownership and live summary responses.
- Generated and visually inspected the example PDF using Poppler. Its assembled
  dimensions are 60 mm wide and 24 mm high; exploded height is 48 mm. Independent
  pypdf/ezdxf validation passed **22 checks** across the single-page PDF, four DXFs,
  artifact sizes and retained recipe. All DXFs use mm and audit with zero errors
  or fixes; exploded labels appear only on the corresponding views.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  verified native relocation with empty PATH and SDK environment removed. Its
  new assembly smoke requires the part inventory and labeled exploded SVG, in
  addition to the existing create/edit/reopen/export/drawing/cache/MCP checks.
  The packaged documentation includes `ASSEMBLIES.md` and the editable examples.
- Build, CTest, schema/SDK, native artifact and independent-reader evidence is
  retained under `build/assembly-demo/`. `git diff --check` passed.

Scope: same-document, one-level assemblies and deterministic rigid datum mates.
Nested assemblies, linked document parts, closed-loop/general mate solving,
kinematics, collision/fit validation and per-part hide/isolate
controls remain future work. Solid operations consume source parts, not assembly
outputs. STEP export is placed compound geometry rather than a promise of XCAF
product hierarchy or editable mates. This increment has local macOS evidence;
earlier multi-platform CI and human viewer acceptance do not certify these new
sources. No global installation, remote push or release was performed.

Next: pursue nested/linked assemblies or additional mate types only
with explicit workflows; separately investigate cold HLR performance for complex
threaded solids. The assembly contract is in `ASSEMBLIES.md`.

## Latest increment — geometry and projection caching

Implemented automatic disposable caching through the shared native worker path:

- `.cache` holds exact OCCT B-rep snapshots for every feature, provenance and the
  output summary, plus complete ordered drawing view sets. Restoring geometry
  validates every shape and topology count inside the bounded worker. Triangles
  are regenerated; selection/evaluation IDs remain fresh. No public MCP fields,
  tools, document format changes or cache API were added.
- SHA-256 keys cover the complete model intent (including embedded STEP content),
  native source/header fingerprints, toolchain/configuration, selected OCCT SDK
  binaries, kernel and cache-format versions. Source fingerprints include current
  geometry/projection tolerances. Projection keys add the ordered views, hidden
  lines, section planes and hatch extraction. Titles, sheet layout, dimensions,
  tolerances, formats and view names rerender using the same projections.
- Entries have payload checksums; reads and writes are bounded. Exact B-rep text
  is limited to 32 MiB, encoded entries to 64 MiB and the workspace cache to 128
  entries / 256 MiB. A native lock protects oldest-publication-first eviction and
  atomic writes. Missing, corrupt, oversized, symlinked, unavailable or busy
  caches fall back to rebuilding/skipped publication. Interrupted cache writes
  are cleaned on the next successful publication. Cache loss cannot lose intent.
- Workers only read shared cache and stage new entries. Coordinators check
  cancellation before publishing results from successful workers. Revision locks,
  mutation validation, HEAD publication, resource limits and artifact identities
  are preserved. Cache hits do not share OCCT objects between processes/threads.

Executed locally on macOS arm64 / OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **20/20 passed in
  25.15 seconds**. The new cache suite includes **44,578 checks**: exact feature
  geometry/provenance round trips for threaded, sketched, lofted and imported
  models; equivalent per-face mesh area/oriented volume and selection edges;
  cold/warm drawing artifacts; changed annotations/names/layout/formats; model,
  hidden-line and section invalidation; checksums, damaged B-reps, deletion,
  >1 MiB entries, quotas, concurrent publication and symlink fallback. The job
  suite's **46 checks** include no cache publication from cancelled, crashed,
  timed-out or memory-limited workers. Rendering failures after staging also
  publish no cache. Existing drawing/viewer/transaction regressions pass.
- **189 schema checks across 18 tools**, **296 MCP SDK 2.3.0 interoperability
  checks** (counts vary with polling), and `git diff --check` passed.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  native relocation with empty PATH and removed SDK environment. It now also
  requires geometry/projection cache entries and checks repeated angular
  PDF/SVG/DXF artifacts have identical SHA-256 hashes.
- Reproducible developer benchmark:
  `python3 tests/cache_benchmark.py build-app-protocol/agent-3d-cad build/cache-demo`.
  The clean run uses the unmodified full M20 threaded knob, with two hatched
  section views, width/height dimensions and PDF/SVG/DXF outputs. Durable job
  timestamps include coordinator/worker/cache/artifact publication:

  | Operation | Seconds |
  | --- | ---: |
  | Cold model creation and geometry cache | 1.373 |
  | Cold section drawing (both caches removed) | 5.682 |
  | Repeated drawing, median of three | 0.126 |
  | Changed A3 sheet/title/general tolerances | 0.127 |
  | Geometry cached, projections removed | 4.544 |
  | Changed thread length, rebuilt new revision | 1.814 |

  Repeated section generation is **45.1x faster** in this local run. All warm
  PDF/SVG/DXF bytes match cold artifacts. Geometry-only regenerated dimensions
  agree within 1e-8; styled dimensions match the 38 mm height and exact model
  width. Editing thread length changes volume while historical source remains
  unchanged. Report, manifests and artifacts:
  `build/cache-demo/cache-benchmark-2bbbfeg7/`. Suite, SDK, schema and bundle logs
  are in `build/cache-demo/`. The benchmark has optional `--views standard`.

Limits and remaining work: this caches whole models and complete ordered view
sets, not incremental features or independent views. Meshes and rendered files
are not cached. Entry count/size eviction is oldest-first, not LRU. A full
four-view hidden-line drawing of the M20 knob still hit a **240-second cold job
limit**; earlier synchronous attempts hit 30 seconds. The failed job/state is
retained under `build/cache-demo/cache-benchmark-_g4wzpwl/`; its subsequent
orthographic/top diagnostic probes were cancelled. Caching does not fix that
first-generation HLR bottleneck and no warm full-view speed claim is made.
These changes have local macOS evidence, not a new five-platform CI claim. No
commit, global installation, remote push or release was performed.

Next: editable assemblies with multiple parts, placements, mates and exploded
views; separately, investigate first-time exact HLR performance on complex
threaded solids. Those capabilities remain planned.

## Latest increment — angular dimensions and explicit tolerances

Implemented the user's next drawing increment through the shared native service,
CLI/MCP schemas, and isolated drawing jobs:

- `kind: angular` measures two directed projected straight-line references.
  Each reference's `from` and `to` points must resolve to the same supporting
  line; nearby distinct matches fail. The actual line directions and intersection
  supply the angle and vertex, including virtual intersections. `sweep` selects
  minor or reflex angles; optional model-mm `arc_radius` sets annotation size.
  Parallel/collinear lines and unreadably small arcs fail explicitly. Arcs and
  virtual vertices participate in sheet fitting without changing width/height
  measurements. PDF/SVG render tangential arrows and extension/leader lines;
  DXF preserves analytic annotation ARC entities at 1:1 mm.
- Per-dimension `manufacturing_tolerance` supports symmetric allowances, signed
  lower/upper deviations (including unilateral/same-sign values), and absolute
  limits. Optional `general_tolerances.linear`/`.angular` supply symmetric mm/deg
  defaults, overridden by individual dimensions. General notes are printed in
  both sheets and standalone DXFs. No tolerance is inferred, and the old circle
  `tolerance` remains a matching rule only.
- Tolerance scalars preserve parameters and bounded expressions with mm/deg
  contexts. Explicit allowances support six decimal places; finer input fails
  rather than rounding away intent. Toleranced nominals display six decimals,
  trimming zeros; deviations apply to that displayed nominal and absolute limits
  must contain it. Results distinguish unrounded `value_mm`/`value_deg` from
  `display_value_mm`/`display_value_deg`, report the exact printed label, effective
  allowance/source, and acceptance bounds. Projection accuracy is unchanged.
  Longer tolerance labels move outside short dimension spans to avoid crossing
  extension lines; unfittable annotations fail.
- Added editable `examples/angular-plate.create.json` and its drawing request
  with explicit demonstration allowances. The user's existing sprocket drawing
  was not assigned guessed manufacturing tolerances. Updated discovery contracts,
  protocol/drawing docs, README, packaged native-cad guidance and roadmap.

Executed locally on macOS arm64 with exact OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` followed by
  `ctest --test-dir build-app-protocol --output-on-failure`: **19/19 passed in
  19.37 seconds**, including **1,876 drawing checks**. New checks cover measured
  acute/obtuse/reflex and section angles; analytic DXF arc radius/sweep; ambiguous,
  missing and parallel references; signed/micron tolerances, explicit limits,
  general overrides and DXF preservation; wrong units and invalid ranges;
  failed-publication rollback; parameterized regeneration after reopening; and
  asynchronous angular drawing jobs. `git diff --check` passed.
- Final independent contracts: **189 schema checks across 18 tools** and **286
  official MCP SDK 2.3.0 interoperability checks**. Counts can vary with job
  polling. These include successful native angular/tolerance requests and degree
  output schemas that cannot be confused with linear `value_mm` results.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  relocation with empty PATH and SDK environment removed. The smoke now creates
  the angular plate and checks its 45-degree result, 44.75-degree lower limit,
  exact printed tolerance label and 7.98 mm bore lower limit.
- Generated 45-degree and 315-degree example sheets and visually reviewed their
  Poppler-rendered PDFs. **40 independent pypdf/ezdxf checks** verified two
  single-page PDFs, four clean DXF audits with zero errors/fixes, mm units,
  explicit/general tolerance text, and both annotation arcs' center, radius and
  sweep at 1:1 mm. Editable source read back unchanged after export. PDFs, PNGs,
  manifests, independent-reader results and all logs are retained under
  `build/drawing-angular-demo/`.

Limitations: angular dimensions reference straight lines in the selected 2D
projection, not arbitrary curved-edge tangents or 3D angles inferred from an
isometric view. Labels use portable ASCII (`deg`, `+/-`, and `.. LIMITS`); this is
not a GD&T or standards-certification implementation. These changes have local
macOS native/bundle evidence; the older five-platform CI results do not validate
this increment. No global installation, remote push or release was performed.

Next: measured geometry/projection caching, particularly repeated drawings of
threaded parts; then editable assemblies with placement, mates and exploded
drawings. These remain planned capabilities.

## Latest increment — aligned drawing layouts and section hatching

The user selected hatching and standard layouts as the next increment. The
existing macOS Codex select/edit/refresh acceptance below already closes the
viewer loop; this work does not claim a new host/platform acceptance run.

Implemented through the shared native `cad_drawing` service and bounded workers:

- `layout: third_angle | first_angle | grid`. Standard layouts align front/top
  world X and front/right world Z at a common fitted scale, even with unequal
  annotation margins. View ordering in a recipe does not change the arrangement.
  A front view is required; duplicate orthographic/isometric orientations fail.
  Isometric and up to two additional section cells fit beside the orthographic
  group; absent orthographic views leave reserved cells. The title block states
  the convention. Defaults without custom views use third angle; existing custom
  view recipes without `layout` retain grid ordering. `view_layouts` reports
  sheet cells and origins with documented coordinate mapping.
- Section views default to material hatching, with `hatch: false` for outlines.
  OCCT intersects each solid with the section plane, retaining faces and inner
  boundaries. The renderer clips 45-degree lines at 2.5 mm sheet spacing using
  analytic lines/circles/arcs and the existing bounded polyline approximation.
  Face intervals are united, preserving cavities, disconnected material and
  nested islands without parity cancellation between overlapping solids.
  An added overlap regression exposed empty material output from intersecting
  a whole interfering pattern compound; per-solid intersection fixes that case.
  A tangent profile without material area fails explicitly unless hatch is off.
- PDF/SVG use thin hatch strokes; 1:1 DXFs retain clipped line entities on a
  separate `HATCH` layer. Hatch geometry never participates in dimension matching.
  All material boundaries share the existing projection budgets. Additional
  limits bound scan lines, intersection work and output segments. Failed layout
  fitting or hatching publishes no drawing and never changes model HEAD.
- Updated discovery schemas, protocol/drawing docs, packaged native-cad guidance,
  and examples. `examples/plate-section.drawing.json` uses a parameterized section
  offset; the keyed sprocket recipe now requests third angle explicitly.

Executed on macOS arm64, exact OCCT 8.0.1:

- `cmake --build build-app-protocol --parallel 4` and
  `ctest --test-dir build-app-protocol --output-on-failure`: **19/19 passed in
  18.45 seconds**. The final additional tangent-section regression passed the
  drawing suite in **1.08 seconds**, now **1,825 checks**. Coverage includes
  layout placement/alignment, negative coordinates and unequal annotations;
  bore/keyway voids, islands, disconnected/overlapping material, circular seams,
  elliptical polyline sections, multiple scales, explicit hatch disablement,
  tangent profiles, work limits, failed scale rollback, existing regeneration
  and asynchronous job behavior. `git diff --check` passed.
- **179 schema checks across 18 tools** and **283 official MCP SDK 2.3.0 checks**
  passed. Native schemas accept the new contracts and reject invalid layout/
  hatch shapes. Logs: `build/drawing-layout-demo/{schema,mcp}.log`.
- `cmake --build build-app-protocol --target bundle-check --parallel 4` passed
  verified relocation with empty PATH and SDK environment removed. Its smoke
  now additionally generates the parameterized plate section, checks third-angle
  metadata, and verifies actual hatch strokes in the exported SVG. No global
  installation or host registration was changed.
- Recreated the keyed 13-tooth sprocket in an isolated workspace, applied its
  saved revision-2 fillet, and generated both first-/third-angle A3 sheets plus
  the parameterized plate section. The sprocket source read back unchanged.
  Poppler rendered both final sprocket PDFs for visual inspection. Independent
  pypdf checks verified three single-page PDFs, revision/convention labels and
  all eight sprocket dimensions. All **14 DXFs** passed ezdxf audit with zero
  errors/fixes and mm units. They contain **98 hatch segments**; 101 samples per
  sprocket hatch segment stayed in the hub material outside the bore/keyway.
  Evidence, PDFs, PNGs, manifests and CTest logs: `build/drawing-layout-demo/`.

Limitations: section views remain plane profiles, not cutaway projections with
cutting-plane arrows; hatching has one fixed angle/spacing and does not encode
material conventions. DXF hatch lines are not associative HATCH objects. These
changes have local macOS build/bundle evidence; the earlier five-platform CI
results below do not validate this increment. No remote push or release was made.

Next: angular dimensions and explicit manufacturing tolerances; then measured
geometry/projection caching (especially threaded parts); then editable assemblies
with placement, mates and exploded drawings. Those capabilities remain planned.

## Latest increment — GitHub CI and independent Arch Linux validation

The user created and checked in the public repository
`https://github.com/cfaulkingham/agent-3d-cad` and authorized an independent Arch
Linux test host. The initial source commit is
`04e73a43b2199027c37e114907c253e96f2e560e` (`init`). Its initial five-platform
push run is `https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37536892312`.
Both macOS and both Ubuntu lanes completed successfully. The first Windows
lane built its SDK and service but failed two native suites. The corrected run
passed all native and MCP checks, then exposed a bundle dependency-filter issue;
the final packaging follow-up passed all five native lanes, as recorded below.
This supersedes
earlier statements that there was no remote repository or runner execution.

The successful `macos-15` arm64 runner passed **17/17 CTests** in **49.03
seconds**, **171 schema checks**, **298 official MCP SDK checks**, relocation
with empty PATH, and native packaging. Its downloaded archive independently
verified **244 files**, has SHA-256
`051776b1adb379ac424c218f2b4156a7008eea834edb3354cbfed2f847c0acb8`, and records
**minimum macOS 15.0** rather than the local SDK's 27.0 baseline. Its provenance
SHA-256 is `2dee873fa3099d5ef4c02b79da801346be885569beca0c7b03a579aaea7ef621`.
That exact archive was also installed into an isolated local directory and
passed the full runtime workflow on this Mac with SDK environment removed and
developer tools hidden from PATH. No global service registration changed.
Logs, archive verification and installation evidence are in `build/platform-ci/`.

The `macos-15-intel` x64 runner passed **17/17 CTests** in **89.03 seconds**,
**173 schema checks**, **343 official MCP SDK checks**, empty-PATH relocation
and packaging. Its independently verified archive contains **244 files**, has
SHA-256 `8b052a4f0effeaf813c234a21ed94697b43d2de13b9b9b7dcb0fbd8e55f5cc51`,
and records **minimum macOS 15.0**. Provenance SHA-256 is
`a73532891a21b93f7ccd5871e5faa9ba133e61b3befaf08f9c48d391f70a605b`.
This is native Intel runner evidence, without installing Rosetta on this Mac.

Both Ubuntu 24.04 CI lanes passed all native, schema, official MCP SDK,
relocation, packaging and **fresh runtime-only container** checks. The container
asserted the absence of developer tools while generating PDF/SVG/four DXFs and
preserving the model record. Recorded CI test results:

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 17/17 | 49.03 | 171 | 298 |
| macOS 15 / x64 | 17/17 | 89.03 | 173 | 343 |
| Ubuntu 24.04 / arm64 | 17/17 | 27.63 | 169 | 243 |
| Ubuntu 24.04 / x64 | 17/17 | 30.66 | 169 | 243 |

The first Windows run passed **15/17 suites**, including persistence, locks,
jobs, topology, drawings and the native MCP/viewer loop. It failed `modeling`
because OCCT's STL filename overload opens a narrow standard stream and creates
the wrong name for `prism-é.stl`, and failed the exact embedded-source check in
`app_protocol`. It is not reported as a successful Windows acceptance run.
The portability patch on `codex/native-platform-validation`:

- Uses OCCT 8.0.1's existing STL stream overload with `std::ofstream(path)`;
  native wide paths work on Windows, and flush/close failures remain explicit.
- Builds the app byte initializer from normalized UTF-8 data, forces the
  generated review HTML to LF, and preserves its exact final newline. The
  generated HTML and native resource retain app hash
  `affa14fe5b7576007408288b25074192df2b8ba79a56b8b4b9fb3867c2a9a3a7`.
- Keeps both failing assertions and adds binary STL read-back plus an isolated
  LF/CRLF checkout regression. The latter checks physical bytes and exact output,
  avoiding CMake text reads that can hide newline differences. Source-integrity
  failures now report actual/expected lengths and the first differing position.

Before pushing the patch, macOS passed all **18/18 suites** in **18.60 seconds**;
the final embedding checks passed **4/4 relevant suites** in **1.18 seconds**.
The final Arch patch passed **18/18 suites** in **17.97 seconds**. Both final
relocated bundles passed the native workflow and embedded-resource hash check
with empty PATH. A temporary embedding implementation added a final newline;
the exact Arch assertion detected it and the final patch corrected it rather
than changing expectations. `build/arch-validation/windows-fixes*` and its
`logs/windows-fixes*` retain the executed evidence. The corrected Windows run
subsequently verified both fixes, as recorded below.

The corrected source commit `555737b23c4e66dbbf21436b532f5999c16c0460` completed at
`https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37544102649`.
Its four macOS/Ubuntu lanes have completed successfully, including the added
physical-byte embedding regression:

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 18/18 | 40.39 | 175 | 328 |
| macOS 15 / x64 | 18/18 | 149.49 | 171 | 333 |
| Ubuntu 24.04 / arm64 | 18/18 | 26.71 | 169 | 248 |
| Ubuntu 24.04 / x64 | 18/18 | 19.78 | 171 | 248 |

All four corrected archives passed independent file-set, link and SHA-256
verification, with 244 manifested files per macOS archive and 256 per Ubuntu
archive. Every bundle passed verified relocation and the empty-PATH native
workflow; both Ubuntu lanes also passed the fresh runtime-only container.
`build/platform-ci/fixed-evidence.json`, `fixed-*.log` and
`fixed-*-verification.json` retain the consolidated evidence. Corrected archive
SHA-256 values:

- macOS arm64: `8c12a815f8248698a303011890bd80735eb31fb3ea1156f25d9efcdc3c14f12e`.
- macOS x64: `d1b46c072c451e03261ac01c7c9fb21ca49a6102200b1128545ec9feb86e0205`.
- Ubuntu arm64: `e5a2b495b740020d29ef65d0a652950b85fe0f4e096d6a21c564afdc660c5050`.
- Ubuntu x64: `ca1bca3308d2d0ca1c93074ea9f454b024f9f28373cc108a4ca782a3dc0a42b9`.

The corrected Ubuntu x64 archive was separately hash-checked, installed and
relocated on Arch. Its full PATH-isolated native runtime workflow and **171
schema checks across 18 tools** passed. Logs and the installation record are
in `build/arch-validation/logs/ubuntu-fixed/`. This tests the patched CI binary,
in addition to the independent patched Arch compiler build.

The corrected Windows x64 run passed **18/18 CTests in 57.03 seconds**, **167
schema checks** and **243 official MCP SDK checks**. This verifies Unicode STL
export/read-back and exact viewer embedding on native Windows, alongside real
geometry, persistence, workers and drawings. It then failed bundle relocation:
`wtdccm.dll` was unresolved. CMake's policy warnings show paths such as
`C:\Windows\system32/advapi32.dll`; the old forward-slash-only filter missed these
system paths and traversed the OS dependency graph. This is a packaging failure,
not successful bundle acceptance; the failed run and full log are retained.

The packaging follow-up matches both separator styles at every System32 boundary
and selects normalized paths when CMake supports
[CMP0207](https://cmake.org/cmake/help/latest/policy/CMP0207.html). It retains hard
failure for unresolved application libraries and conflicting dependencies. The
new `runtime_filters` CTest checks **16 cases**, including the actual mixed paths,
normalization/case variants, SDK/MSVC DLLs, unresolved names and lookalike
folders. Local macOS passed **19/19 suites in 18.57 seconds** and the relocated
bundle workflow with empty PATH. Evidence is in
`build/platform-ci/packaging-fix-local-{build,ctest,bundle}.log`.

CI now uses separate cache restore/save actions, saving a completed SDK before
service tests or packaging. Previous Windows failures discarded each successful
42-minute SDK build because the old cache action saved only on job success. SDK
keys now hash the exact recipe plus an explicit compiler/deployment/configuration
ABI tag; service or packaging edits do not invalidate them. The four existing
verified macOS/Ubuntu caches migrate only under the unchanged recipe hash and
ABI tag, using their exact original key; there is no broad restore fallback.
The workflow passed actionlint 1.7.12. The final native Windows relocation and
archive run subsequently passed, as recorded below.
The final portability/packaging source commit
`2e96291e8114bdbe362eb935f37fe228ac3f0f2a` passed **all five native CI lanes**:
`https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37548998613`.
All lanes passed native geometry/persistence/workers, schema conformance, the
official MCP SDK, verified relocation with empty PATH, and native packaging.
Both Linux lanes additionally passed the fresh runtime-only container.

| Runner / architecture | CTests | CTest seconds | Schema checks | MCP SDK checks |
|---|---:|---:|---:|---:|
| macOS 15 / arm64 | 19/19 | 38.71 | 171 | 293 |
| macOS 15 / x64 | 19/19 | 95.14 | 171 | 333 |
| Ubuntu 24.04 / arm64 | 19/19 | 26.26 | 171 | 248 |
| Ubuntu 24.04 / x64 | 19/19 | 30.48 | 171 | 248 |
| Windows 2025 / x64 | 19/19 | 47.25 | 167 | 248 |

All five downloaded archives independently passed complete file-set, per-file
SHA-256 and link verification. macOS archives contain 244 manifested files,
Ubuntu archives 256, and the Windows ZIP **270**. Windows records MSVC
19.51.36260.0, exact OCCT 8.0.1 and the same canonical viewer app hash as all four
other platforms. Its ZIP also passed case-insensitive filename uniqueness and
checks that kernel32/user32/advapi32/wtdccm OS DLLs were not copied. It contains
the required OCCT/FreeType DLLs and the MSVC/UCRT redistributable runtime.
Final archive SHA-256 values:

- macOS 15 / arm64: `1ec9ee4815e7fbf39ac4749ac02bef0441df4c710a7c4677ffaf6856ed22f64b`.
- macOS 15 / x64: `79b748997eeb9a80937da5aad56c836d6920913b4812c5fa646a8a6fcbed04dc`.
- Ubuntu 24.04 / arm64: `880c8eae74424d7e65ec59ff850e8d745a609d9e895d59a482efdb2557363148`.
- Ubuntu 24.04 / x64: `030fd69ea991b04ee08dc686fc437744ec32902d93432342c79590f1882ed48e`.
- Windows 2025 / x64: `b102604ad9042ea5dd1076b03becbc03239934a28818426ac16cb99dfc34702e`.

Windows bundle relocation generated PDF/SVG/four DXFs and exercised the native
CLI/MCP/worker workflow with empty PATH and SDK variables removed. This is
native runner relocation evidence, not a fresh Windows desktop or GPU-host
claim. Actual GPU selection/edit/refresh remains demonstrated on the Codex Mac.
The Windows SDK build took **41 minutes 49 seconds** and was successfully cached
before service testing; the service/tests/contracts/package portion took about
four minutes. The new cache saved under the Windows recipe's CRLF hash
`41d6d2e21ae2be0cfd597d63d52d59286c7a291edccf3aaa07941070083bb496`.
The four previously verified SDKs also migrated to the new keys without rebuilds.
`build/platform-ci/packaging-evidence.json`, `packaging-*.log` and
`packaging-*-verification.json` retain the complete final evidence. Initial and
intermediate Windows failures remain recorded above and were not weakened into
passing assertions. Native platform validation is complete; next authorized
product work is better drawing layouts, hatching, angular dimensions and
explicit tolerances, followed by caching/performance and assemblies.


Both downloaded Linux archives independently verified all **256 manifested
files**, including link destinations and the exact file set. The x64 archive
SHA-256 is `904176122410cfdbf6713fd716d9b50205af81204315edbf3fab6ad8572db2fa`
and the arm64 archive SHA-256 is
`a41a04bb1c1c5ac5dda64b9fe970f0d4c6163184dc40368e3b46559c9ee8da64`.
That exact **Ubuntu x64 archive was transferred to Arch**, rechecked against its
archive hash, verified/relocated with the installer, and passed the full runtime
workflow and **171 schema checks across 18 tools** there. This demonstrates the
Ubuntu-built native bundle on a newer Arch distribution, in addition to the
independent Arch compiler build below. SDK environment and developer PATH were
removed during that runtime check; the Arch host itself still has developer
tools installed. Logs and installation records are in
`build/arch-validation/logs/ubuntu-*`.

That exact commit was archived and transferred to a fresh, isolated directory
on the user-supplied Omarchy 4.0.4 / Arch x86_64 host. Its compiler was GCC
16.2.1 and its glibc was 2.44. All three upstream archives were verified against
the recorded SHA-256 pins. OCCT 8.0.1 and FreeType 2.14.3 were built from those
archives, without a system OCCT or sibling checkout. CMake 3.31.10, patchelf
0.19.1 and the independent Python MCP client were installed only in a test-local
virtual environment; system packages and user service configuration were not
changed.

Executed Arch evidence, retained locally in `build/arch-validation/`:

- The complete native build and **17/17 CTests** passed in **18.01 seconds**.
- The verified, relocated shared-library bundle passed model creation, editing,
  reopen/rollback, STEP/STL/drawings/MCP and embedded app-resource checks with
  empty PATH and SDK loader/resource overrides removed.
- The independently installed bundle passed **173 schema checks across 18
  tools** and **248 official MCP SDK 2.3.0 interoperability checks** (counts
  vary with job polling).
- The runtime workflow passed with only required OS utilities on PATH and SDK
  environment removed, including native PDF/SVG/four-DXF generation and
  byte-identical model readback after drawing. This host has developer tools
  installed: this is a PATH-isolation test, not a fresh tool-free OS/container.
  The reused smoke script's container wording does not change that limitation.
- A native Arch preview archive was produced, SHA-256
  `004bcd91a1ce4f17fdef5f4c9e9ee65cf8c45dd4c20e7cb3e77777c15b823a63`.
  Its **256 manifested files** were independently verified after retrieval,
  with no extra entries or escaping links. Provenance SHA-256 is
  `5cf896613d18d25357ce8f62d6f068d44754b0920c79ea943480da16ca1f7f17`.
  It inherits the Arch glibc baseline and is not evidence of compatibility with
  older distributions. The Ubuntu CI bundle remains the intended portable
  Linux baseline.
- The original knob and duplex sprocket examples were replayed in a separate
  Arch workspace, with **23 model/geometry/export/drawing/source-preservation
  checks**. The sprocket retained 409 faces, 1,210 edges and volume
  64,181.2664969257 mm³, and generated PDF/SVG/five DXFs. Both saved records
  remained unchanged after STEP/STL export and drawing generation.

The extra knob comparison exposed the limitation of the existing non-adaptive
volume quadrature: the identical example gives 32,476.00416788722 mm³ on macOS
and 32,476.001346121142 mm³ on Arch, a 0.00282 mm³ difference. An initial extra
0.001 mm³ comparison failed and is retained in `representative.log`; it is not
reported as passing. Independent native readers then loaded both platforms'
STEP files on both platforms with healing disabled: all four retained one valid
solid and passed **15 helix/flank/root/crest/lead probes each**. Adaptive volume
integration at requested relative errors 1e-7/1e-9/1e-11 gave final values within
**0.000000433 mm³** of each other (reported relative error about 1.44e-10).
The continuation compares that independent geometry at the original 0.001 mm³
threshold, rather than broadening it or changing product tests. The default
summary should not imply precision beyond its integration method. Numerical
read-backs, probe source, original failure and final replay logs are retained.

The first temporary SSH driver stopped after the successful native suites and
relocation because its schema-test filename was mistyped. The actual unchanged
`tests/schema_conformance.py` and remaining checks were then executed directly
and passed; no product test was weakened. Original and corrected driver logs
are retained. The final corrected Windows x64 runner results are recorded above.

## Latest increment — actual viewer acceptance and current Linux validation

The user authorized working through the remaining items starting with the live
viewer loop. The **installed** service created separate `viewer_loop_plate`
revision 1 and opened `viewer_loop_acceptance` once in the actual Codex MCP App.
The human expanded the app and picked its 60 mm top/front edge. Native context
resolved `edge-10` in evaluation `eval_5c13132c3b3f0d477d7c3b1c7e0cc344`
to one geometric selector, centered at [30, 0, 12] mm. Quick Edit published that
reference and the request “Round this selected edge to 1 mm.” The host acknowledged
the handoff and placed it in the chat composer. The human confirmed sending it.
The exact request then arrived as an MCP App message after the acceptance turn,
independently confirming receipt. Its revision-1 context was explicitly checked
against saved revision 2, which already contained the requested fillet; no second
edit/revision was created. Do not claim the host automatically posts messages.

The agent read the document and context, explicitly resolved the selection, and
committed only that edge's 1 mm fillet as revision 2. The same rendered app
followed automatically, displayed the new feature and rounded edge, and cleared
the old pick. Native camera yaw/pitch/zoom/pan matched within 1e-12 (only yaw's
floating-point normalization changed). The solid stayed valid with seven faces,
15 edges and volume 28,787.12388980385 mm³. A stale revision-1 pick failed with
`stale_selection`; an intentionally impossible radius failed on `rounded`
without changing revision 2 or its displayed evaluation. The sprocket and knob
were not edited. Actual DOM, GPU screenshots and native calls are in this turn;
`build/viewer-acceptance/host-evidence.json` and `plate-r2.jpg` retain local evidence.

Two concrete fixes followed the acceptance work:

- The UI now says **Send to chat** and explains the possible composer Send step.
  An acknowledgment no longer claims “Request sent.” The guide, protocol and
  packaged native-cad skill describe this host behavior.
- A repeat Linux run exposed a transient `workspace_busy` during post-edit
  polling. The actual state controller now treats read-only polling lock/queue
  contention and superseded transfer references as loading, disables old picks,
  preserves the last rendered solid/camera, and retries a fully validated transfer
  on the next poll. Other failures remain explicit. This never retries mutations,
  context writes or message delivery. Deterministic tests cover each transfer
  stage, revision changes, disabled sends during recovery and persistent errors.

Executed evidence:

- macOS arm64: five relevant CTest suites passed in **2.94 seconds**, including
  **63 bridge/state checks** (up from 40). Ten additional consecutive actual
  native MCP/controller loops passed **13 checks each**. Logs:
  `build-app-protocol/viewer-acceptance-tests.log` and
  `build/viewer-acceptance/stress.log`.
- Current Linux arm64 source in Docker's native aarch64 engine: **17/17 CTests**
  passed in **15.26 seconds**, **171 schema checks across 18 tools**, and
  **253 official MCP SDK 2.3.0 interoperability checks**. Relocation with empty
  PATH passed. The fresh Ubuntu runtime-only stage, with no Python/Node/Rust/
  compiler/CMake, passed the model/view/jobs/export/MCP workflow plus actual
  native PDF/SVG/four-DXF generation and byte-identical model readback afterward.
  The drawing runtime smoke was added in this increment. Full final log:
  `build-linux-container/final-runtime.log`. An earlier run failed the polling
  race above; its test was not weakened or retried to manufacture a passing claim.
- Both preview archives were independently checked against every manifested
  file, including in-tree symlink destinations and rejection of extra entries:
  **244 macOS files**, **256 Linux files**. Both embed app SHA-256
  `affa14fe5b7576007408288b25074192df2b8ba79a56b8b4b9fb3867c2a9a3a7`.
  `build/viewer-acceptance/archive-verification.json` records full archive paths,
  sizes and hashes. The macOS bundle is installed as a fresh version; its
  executable passed **169 schema checks** and **278 official MCP SDK checks**
  (counts vary with polling). `install-result.json`, `registration.json` and
  `installed-{schema,sdk}.log` in that evidence directory record the installation
  and preserved configuration/skill backups. Only the CAD command path and its
  skill changed; unrelated configuration bytes and saved models are intact.

This does not establish offline artifact/browser acceptance or other-host
behavior. Native Windows x64, Linux x64 and macOS x64 still need execution.
At the end of that earlier increment, the five-platform CI definition had no
remote repository or runner execution. The user subsequently created and
checked in the GitHub repository; see the newer platform-validation record above.
Local packaging/installation records for this increment live in
`build/viewer-acceptance/`; refresh the running MCP connection before expecting
new app text/race handling in an already mounted viewer.

## Latest increment — native engineering drawings

The user authorized implementing 2D drawings from saved models. `cad_drawing`
is now the eighteenth CLI/MCP tool and is admitted by `cad_job`. It generates
PDF/SVG sheets and separate 1:1 mm DXFs for one to six front/top/right/isometric
or planar section views. It preserves original parameterized drawing recipes
for explicit regeneration against a later committed revision. Drawing exports
never change model HEAD. Details and examples are in `DRAWINGS.md`.

Implementation:

- `BuiltModel::drawing` in `kernel.cpp` keeps OCCT hidden-line removal and plane
  intersections inside the kernel boundary. Analytic lines/circles/arcs remain
  analytic; other curves are approximated within 0.02 mm model-space deflection.
  Coincident projected entities are deduplicated, edge-on circular splines
  collapse to lines only when their poles prove collinearity, and covered hidden
  line spans do not redraw visible boundaries as dashed geometry.
- `drawing.cpp` validates closed recipes, resolves existing bounded scalar
  references, measures view extents and geometry-attached linear/radial
  dimensions, rejects missing/ambiguous references, lays out labeled A4/A3
  sheets, and writes native PDF/SVG/DXF. No converter or language runtime is
  added. Printable labels round to three decimals; returned values retain their
  measured precision. DXF uses valid R2000 handles, owners and table records.
- Projection and rendering both execute under worker time/memory limits. The
  coordinator writes a fresh generation directory and publishes its manifest
  last under the document lock after a cancellation check. Failure cannot
  replace existing drawings. Sidecars retain original/resolved intent and the
  source model SHA-256. A crash may leave a directory without a final manifest;
  it is an unpublished export, not a committed model revision.
- Bundle documentation, schemas, agent skill, examples and empty-PATH relocation
  checks include the new workflow. Ordinary model/STEP/STL contracts remain intact.

Final macOS arm64 evidence in `build/drawing-demo/`:

- `cmake --build build-app-protocol --parallel 4` succeeded. All **17/17 CTests**
  passed in **18.01 seconds**, including **434 drawing checks**. Tests cover
  exact view extents, hidden bore edges, arcs and section offsets; attached,
  missing and ambiguous dimensions; native PDF cross-reference offsets; DXF
  coordinates/units/handles/ownership; historical recipe regeneration; preserved
  files/HEAD after failure; and asynchronous drawing jobs. `git diff --check`
  passed. Existing modeling, storage, jobs, MCP and viewer suites remain green.
- The final installed binary passed **171 JSON Schema checks across 18 tools**
  and **293 official MCP SDK 2.3.0 interoperability checks** (polling counts can
  vary); logs are `schema-installed.log` and `sdk-installed.log`. The actual
  installed service also completed **11 real MCP
  calls** in `installed-mcp.json` to draw the user's saved sprocket and confirm
  its source record was unchanged.
- All five representative DXFs passed **ezdxf 1.4.3 with zero errors and zero
  repairs** (`dxf-audit-final.json`). Poppler rendered the A3 PDF; visual review
  found readable, unclipped dimensions and labels. Independent strict pypdf
  parsing confirmed model/revision and dimension text. All seven files produced
  by the installed MCP service match these reviewed files byte-for-byte.
- `bundle-check` generated drawings after relocation with empty PATH and no SDK
  loader/resource overrides. The final archive has **244 verified files** and
  matches the installed provenance. It is 23,893,213 bytes, SHA-256
  `c0ca67aa5fe3472e239ecf752accc0c3b9ceba4696f06c79d5cc7251cf871c97`.
  Path: `build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`.
  Provenance SHA-256:
  `af58c23d07b881a17260db5c8b4b8971ce8b73c2bd94f49a6acdbc8e0b606134`.

The new version is installed under `~/Applications/Agent3DCAD/`; exact executable
and backups are recorded in `build/drawing-demo/install-result.json` and
`registration.json`. The existing Codex registration now points to it and the
global native-cad skill is updated. The CLI registration command unexpectedly
removed another MCP server's `args` field; a byte-preserving correction restored
the original configuration with only the authorized CAD executable path changed.
All unrelated settings were verified preserved. Previous installations, skill
and config backups remain available. A running host connection may need an MCP
refresh to discover `cad_drawing`; no Codex UI automation was attempted.

The user's drawing is in
`~/Documents/Agent3DCAD/exports/duplex_35_sprocket_20t-r1-drawing-eval_0f63d8497144773773715bc2f23ddd5e/`.
Its sheet measures bore **19.05 mm**, hub length **34.925 mm**, and row spacing
**10.1346 mm** (printed 10.135), with a real plane section through the first row.
The 3D source is still revision 1. The PDF was queued for the Codex file preview.

Limits: labeled view cells do not claim a standard first/third-angle arrangement;
sections are plane profiles, not cutaway projections. Drawings do not assign
GD&T, manufacturing tolerances, material or thread conventions. ASCII annotation
text, explicit regeneration, bounded one-sheet layouts and the documented
projection/export caps apply. Three-view sprocket HLR took 1.62 seconds in a
native probe; the helical knob exceeded a 30-second probe timeout. Complex
curved drawings may require `cad_job` with a longer budget, and can still time
out explicitly. No new Linux or Windows execution evidence is claimed. Existing
platform, host-interaction, signing and license gates remain open. Nothing was
uploaded or published. Next drawing extensions should follow actual user needs:
standard sheet arrangements, cutaway hatching, richer annotations or multi-sheet
layout, rather than promising manufacturing certification.

## Direct MCP demonstration — duplex #35 sprocket

The native MCP tools are now available directly in this chat. The user's next
request, “Build a double-strand #35 sprocket,” created saved document
`duplex_35_sprocket_20t`, revision 1, using the **installed** service's `cad_job`
→ `cad_create`, then `cad_open`, `cad_export` (STEP/STL) and `cad_read` tools.
No service implementation or installed bundle changes were needed. Its 65
ordinary native features form exact circular seating/working/topping surfaces,
tangent flanks, 20 repeated tooth spaces, two aligned rows, a hub and bore.

Assumptions: 20 teeth, 3/4-inch plain bore, type-B hub, 9.525 mm pitch,
5.08 mm bushing diameter, 4.1148 mm tooth-row width, 10.1346 mm transverse pitch,
49.2125 mm hub diameter and 34.925 mm overall length. Sources, tooth-form equations,
edit limits and complete structured intent are in
`examples/duplex-35-sprocket.prompt.md` and the paired `.create.json`.
The tooth count is encoded in instance layout/constants rather than an independent
parameter. No material, load rating or manufactured shaft fit is certified.

The native job committed in 1.464 seconds: one valid solid, 409 faces, 1,210 edges,
volume 64,181.266497 mm³. Independent native STEP read-back with optional healing
disabled retained one valid solid and volume, passed **812** bore/bushing/row/phase
point probes, and found **40/80/80** exact seat/working/topping cylindrical faces
at the expected radii. The STL's 5,264 triangles have two incident triangles per
each of 7,896 edges at 0.00001 mm vertex quantization. One-off verification source
and log are in `build/sprocket35/verify.cpp` and `validation.log`.

`cad_open` opened view `sprocket35_review`; subsequent `cad_context` returned the
current evaluation, non-stale revision 1 and host-published camera state. This is
new evidence of the actual host/App connection. It does not establish visual
GPU correctness or the complete selection-to-Quick-Edit acceptance loop.

## Local setup and threaded-knob acceptance — 2026-10-06

The user requested installation on this Mac and a real MCP demonstration:
“make a knob with a male 20mm coarse threaded end.” This increment adds the
missing `external_thread` primitive, installs the native bundle and packaged
`native-cad` skill, and registers the enabled `agent-3d-cad` STDIO server in the
user's Codex config. Existing config was backed up and all unrelated settings
were verified unchanged. The runtime workspace is `~/Documents/Agent3DCAD`.

`external_thread` builds an exact continuous cylindrical helix with nominal
60-degree flanks, P/8 crest, flat root, optional left/right handedness, and a
45-degree lead at +Z. Its bounded domain is pitch ≥0.1 mm, diameter ≤200 mm,
diameter/pitch 3–100, and 1–16 turns. It has no certified fit class or process
clearance. The writer now sets the pinned OCCT ToSTEP defaults explicitly
(`SplitCommonVertex` and `DirectFaces`), avoiding shared-actor flag leakage from
other writers without enabling import healing.

`examples/m20-knob.create.json` and its paired prompt specify M20×2.5 right-hand,
20 mm exposed stud, 45 mm nominal grip diameter, 18 mm grip height, eight finger
scallops, 1.2 mm top/bottom grip rounds, and 38 mm overall height. The stud
intersects the grip by 1 mm before fusion. Through the **installed native MCP
server**, the official SDK completed **18 tool calls**: create/read, STEP/STL
export, open/live mesh transfer, and a fresh-process reopen. Saved document
`m20_coarse_knob`, revision 1, has one valid solid, 53 faces, 148 edges and
volume 32,476.0042 mm³. STEP is 895,518 bytes; STL is 257,484 bytes. The STL's
5,148 triangles have two incident triangles at each of 7,722 shared edges using
0.00001 mm vertex quantization. Its STEP exceeds the current 512 KiB embedded
import cap; native exchange tests validate the complete example's round trip.

Final source verification: **16/16 CTests passed in 17.31 seconds**, with **1,353
C++ assertions plus CLI**, **17 installer checks**, 15 offline-renderer, 40 live
UI/state, 13 real-MCP live-loop and 42 WebGL checks. Schema conformance passed
**158 checks across 17 tools**; official MCP SDK 2.3.0 passed **249 checks**.
The modeling suite's 245 checks include exported RH/LH helical phase, actual
flank/crest/root probes, expression rebuild, full-knob STEP one-solid/volume,
and dimensional boundaries. M20×2.5×20 builds in approximately 0.63 seconds.
The verified relocation workflow runs with empty PATH. `git diff --check` passed.

The installer (`packaging/install-local.cmake`) checks every manifested file and
link before/after copying into a fresh versioned directory, preserving prior
versions. The final archive contains exactly 239 manifested files plus
provenance. A macOS CPack post-build step uses GNU tar serialization to avoid
unmanifested AppleDouble entries without changing source extended attributes.
Final preview: `build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`,
23,755,678 bytes, macOS 27.0+ arm64.
SHA-256: `49e790d5f176de7adad65cc6fb99dd6011302d7f2f22c368347b593d2573b9aa`.
Installed/archive provenance SHA-256:
`5b6c9b7e38c55e59ba3b4834b40373d1e2c9def09aa37418fb4f90d0a0d3c077`.
The installed directory is recorded in `build/knob-setup/install-result.json`;
setup details and complete MCP evidence are in `build/knob-setup/SETUP.md`,
`installed-mcp.log` and `installed-report/mcp-transcript.json`.

At the end of knob setup, this chat's tool catalog had not refreshed (the next
sprocket turn above confirms direct availability). Computer Use explicitly
prohibits controlling Codex itself; that boundary was not bypassed. A manual
MCP refresh was the remaining host step at that time.
The saved `m20_knob_review` view and mesh are ready, but real host/GPU and
selection/Quick Edit interaction remain unverified. Windows and other existing
release gates remain open. No remote repository or release was created.

## Latest increment — live create–view–select–edit loop

After comparing this project with `../text-to-cad`, the user authorized moving
to the next experience step. M4 adds a focused native-backed viewer rather than
the sibling's complete Python service/UI contract. No sibling code or runtime
was copied. Existing M0–M3 release gates remain open; this increment does not
claim full text-to-cad feature or host parity.

Implemented:

- `src/live.cpp`: five shared CLI/MCP tools (`cad_open`, `cad_show`, `cad_list`,
  `cad_context`, app-only `cad_viewer`) and durable workspace-scoped view state.
- `web/`: bundled WebGL2/WebGL1 viewport, model library, source feature tree,
  native measurements, selection, standard views, pan/orbit/zoom and Quick Edit.
- `src/mcp.cpp`, `cmake/EmbedViewerApp.cmake`: self-contained MCP App resource
  `ui://agent-3d-cad/viewer.html` with no network origins. CMake embeds exact UTF-8
  asset bytes in the executable. End users still need no Node or other tooling.
- Sync uses isolated mesh jobs, checks view generation plus HEAD before
  publication, freezes evaluation data, and transfers at most 128 KiB per chunk.
  Unchanged HEAD does not rebuild. Context validates pick resolution and rechecks
  identity under locks; stale context remains explicitly marked for inspection.
- The actual app bridge/controller follows edits automatically, preserves camera,
  clears outdated picks, restores matching saved context on reopen, and hides an
  unrelated old model while a document switch loads. Mesh identity is rechecked
  after transfer. Optional host context acknowledgments cannot block HEAD polling.
- Quick Edit publishes context then sends an explicit user message if supported.
  Optional PNGs require image-message support. Copy request has a selectable-text
  fallback. A message with uncertain delivery is never automatically resent.
- `skills/native-cad/SKILL.md` is packaged with the product; it teaches authoring,
  selection resolution, revision checks and reusing the same viewer. Validation
  with the skill-creator validator passed (temporary developer PyYAML 6.0.3).

Latest macOS arm64 shared-OCCT build: `build-app-protocol`. All **15 CTest suites
passed in 9.73 seconds**: **1,286 native checks plus CLI**, **15 offline renderer
checks**, **38 live bridge/state checks**, **13 real MCP live-loop checks**, and
**42 WebGL math/lifecycle/native-payload checks**. Independent schema validation
passed **158 checks across 17 tools** and official MCP SDK 2.3.0 passed **249
checks**, including resource discovery/read and UI metadata. Polling-dependent
counts may vary. Logs: `build-app-protocol/final-ctest.log`, `final-schema.log`,
`final-sdk.log`, and `Testing/Temporary/LastTest.log`.
The default shared build, `build-package`, was then rebuilt with the final assets
and passed the same 15 suites in **9.44 seconds**. `git diff --check` passed.
The final host-sizing correction adds debounced, deduplicated MCP App size
notifications and handles hosts declining fullscreen. Its focused bridge/state
suite passes **40 checks** (superseding 38 above), and the real MCP loop still
passes **13 checks**. This is covered by protocol tests, not real inline layout
evidence from a browser. The final archive information below names this version.

Final M4 macOS preview:
`build-app-protocol/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`
(23,749,281 bytes, **macOS 27.0+ arm64**).
SHA-256: `f77e5454544089886359d407f146a17aaf435bfdbfe28fb18d1c83720a0d0d4a`.
Embedded app SHA-256:
`4a5c0c8e3779ffb1d34008657b0614e6ba0811735e9ebe87a17bd52f091a943d`.
The final resource/bridge/live-loop suites passed again (93/40/13 checks), and
relocation with empty PATH passed, including serving the exact embedded app.
All 224 provenance entries were checked against files inside the final archive.
The bundle includes 17-tool schemas, the guide, agent skill and live-plate example.
`build-app-protocol/RESULTS.md`, `final-app-tests.log`, `final-bundle.log` and
`final-package.log` retain evidence. The archive is local and unpublished.
The new live layer has not been run on Linux: Docker was stopped at the optional
follow-up probe and was not restarted. Earlier Linux evidence below covers M0–M3.

The real MCP loop starts the native executable and executes the actual app bridge
and state controller through a test host transport. It computes an unambiguous
visible edge from native tessellation, resolves a unique geometric selector,
commits a selective fillet, automatically receives revision 2, preserves camera,
clears the old pick, and proves invalid fillet rollback. This is **not a real
browser/GPU or Codex-host interaction test**. GPU lifecycle uses mocks; ray math
and native payload compatibility are real. Existing local-file browser policy
was not bypassed. No global host configuration was installed during that earlier M4 increment;
the local setup increment above subsequently registered the native server.

The M4 acceptance step is to connect this native service to a real MCP Apps host,
open `cad_open`, pick/send a Quick Edit, and observe an agent edit in the same
rendered view. See `docs/LIVE_VIEWER.md` and `examples/live-plate.create.json`.
Windows/x64 execution, signing/license decisions, automatic evaluation garbage
collection, assemblies, hide/isolate/clipping, recents/thumbnails and broader
fabrication formats are not established by this increment.

The older M0–M3 evidence and archives below are retained as historical evidence;
they do not contain the M4 viewer unless a newer artifact is explicitly named.

## Scope and architecture

The user requested completion of the spec, explicitly including M3 modeling and
Windows, and authorized parallel sub-agents. Four workstreams implemented
kernel/model, jobs/storage, viewer/service integration, and packaging/CI.

- C++20 service with exact checksum-pinned OpenCascade 8.0.1.
- Closed structured intent, persistent feature IDs, ordered replay, finite values.
- Shared `Service` for CLI and MCP; seventeen tools with input/output JSON Schemas.
- Kernel work is serial inside isolated native workers; coordinator publishes.
- Per-document writer lock checks revision both before work and before publication.
- Immutable revisions include optional durable request receipts; HEAD is atomic.
- No Python/Rust/Node/compiler required in the end-user modeling path.
- No HTTP endpoint or public release. The user-created GitHub repository is
  recorded above. This Mac now has a
  local global MCP registration for the native preview.

## Implemented behavior

| Area | Files | Behavior |
|---|---|---|
| Model contract | `src/model.cpp` | Closed schemas, unit-checked bounded arithmetic, dependencies, edit batches |
| Geometry | `src/kernel.cpp` | Primitives, exact helical external threads, booleans, selective fillets, numeric sketches/workplanes, extrusion/revolve/loft/sweep, rigid transforms, holes, patterns, reusable instances |
| STEP import | Model/kernel/service | Embedded content ≤512 KiB and verified SHA-256, unit conversion to mm, rebuild independent of original file |
| Topology | Kernel/service | Evaluation-scoped face/edge measurements, unique selector suggestions, explicit cardinality errors and bounded OCCT history evidence |
| Mesh/viewer | Kernel, `src/viewer.cpp` | Same-evaluation B-rep face/triangle mapping and edge polylines, offline HTML with picks and revision loading |
| Live viewer | `src/live.cpp`, `web/`, MCP resource | Library, feature tree, WebGL picking, automatic HEAD refresh, validated context and host Quick Edit |
| Editing utilities | Service | Candidate preview, historical comparison, restore-as-new-revision, stale/draft selection rejection |
| Persistence | `src/storage.cpp` | POSIX and Windows file/lock implementations, per-document conflict control, durable deduplication receipts |
| Workers/jobs | `src/jobs.cpp` | Native spawning, queue admission, cancellation, wall/memory budgets, recoverable durable jobs, bounded result files |
| Distribution | `cmake/`, `packaging/` | Relative library paths, native closure, OCCT resources, notices, schema publication, provenance, archives and relocation checks |
| CI | `.github/workflows/ci.yml` | Native macOS arm64/x64, Linux x64/arm64, Windows x64 lanes plus Linux runtime-only container gate |

The original acceptance requirements remain in `ROADMAP.md`. Implementation
status does not waive their unexecuted platform and human-interaction checks.

Historical M0–M4 requirement audit (newer acceptance evidence is recorded above):

| Gate | Demonstrated | Outstanding |
|---|---|---|
| M0 editable backend | Real OCCT geometry, transactions, CLI and MCP suites on macOS arm64 and Linux arm64 | Other platform runs tracked below |
| M1 selection/viewer | Persisted evaluated picks, selective edits, ambiguous replay rejection, draft previews, offline artifacts and pure renderer tests | Human/browser pick, reference handoff and updated-view interaction |
| M2 jobs/distribution | Worker failure/cancel/deadline/memory handling, durable retries, old-revision reads, independent MCP SDK, relocated Mac/Linux bundles and fresh Linux runtime-only workflow | Remaining platform bundles |
| M3 modeling/portability | All scoped features, immutable imports, section/copy lineage, two representative parts edited/reopened with analytic checks | Native Windows persistence/geometry and remaining architecture runs |
| M4 integrated experience | Native live state, actual bridge/controller over real MCP, selection-to-fillet refresh, camera retention, stale/failed edit handling, self-contained resource | Real MCP Apps host and GPU interaction |

## Validation evidence

Local host: macOS arm64, AppleClang 21.0.0, exact OCCT 8.0.1, JSON 3.12.0,
FreeType 2.14.3. The convenience static-SDK build passed all nine CTest suites.
The complete pinned dependency recipe was also built and installed from source,
including **shared** OCCT and standalone shared FreeType with optional codecs
turned off. A service linked to that freshly built SDK passed all nine suites
in the final run (7.66 seconds): **1,118 explicit native checks plus the CLI
process integration suite**.

Native suites:

- `model`: closed fields, dimensions, graph/parameter/edit validation.
- `geometry`: analytic volumes, booleans, rounded four-hole plate, STEP read-back.
- `transactions`: failed-build rollback, historical reopen, stale revisions,
  orphan candidates, cross-process writer exclusion and concurrent reads.
- `protocol`: MCP initialization, discovery, structured errors, notifications,
  malformed frame recovery and JSON-only stdout.
- `cli_smoke`: complete plate create/edit/reopen/export across native processes.
- `topology`: 783 checks including selected-edge analytic fillets, unique/missing/
  ambiguous rules, unchanged-reference evolution, exact mesh mappings, limits,
  valid OCCT history targets, and per-instance pattern lineage checked against
  translated geometry and matching mesh IDs.
- `modeling`: 178 checks including arbitrary planes, clockwise/self-intersecting
  profiles, signed extrusion, partial/full revolve, loft, sweep, transform,
  repeated solids, holes, expression unit/node/depth bounds, STEP hashes,
  source-unit conversion, Unicode STEP/STL filenames and rejection of STEP with
  a valid solid plus loose surfaces. Every solid must have closed shells and
  positive finite volume; optional STEP healing is disabled. Ruled and smooth
  loft histories identify generated side faces without mutating source sections.
- `jobs`: 42 checks including real worker cancellation/kill/deadline/memory
  failure with HEAD preservation, live MCP ping/read/cancel during a build,
  four worker slots/eight active-job admission, path symlink rejection, and
  recovery before/after commit without duplicate publication.
- `viewer`: 46 checks for stored picks after restart, wrong/stale/draft identity rejection,
  preview without mutation, HTML/data identity, safe embedded JSON, comparisons,
  request deduplication and schema discovery; resolved picks drive a real selected
  fillet and updated view; ambiguous replay preserves HEAD; Unicode workspace
  paths and evaluation metadata exceeding 1 MiB round-trip correctly. Summary
  queries include the measured feature ID for both output and intermediate shapes.

Independent `tests/schema_conformance.py` used jsonschema 4.25.1 (developer-only)
to validate all twelve tool input/output schemas and real CLI/MCP responses:
**120 checks passed** in the final run (polling count can vary). It authors, edits,
reopens and exports the bracket and nozzle examples, checking changed geometry
against analytic volumes and preserving historical revisions. It also rebuilds
an import after deleting its original STEP and restores history. Only documented
transient `workspace_busy` responses are retried with a bounded deadline.
Python is not used by native CTest or included in the product bundle.

`tests/mcp_sdk_smoke.py` passed **211 interoperability checks** using the official
MCP Python SDK 2.3.0 against the real native stdio subprocess. The independent
client negotiated protocol 2025-11-25 in auto and legacy modes, discovered and
validated all twelve tools, edited/exported models, inspected structured errors,
pinged/read during active native work, cancelled it, and reopened durable history,
request receipts and job results. This establishes SDK interoperability, not a
GUI-host attachment; no global MCP registration was installed. Dependencies are
pinned in `tests/mcp_sdk_requirements.txt` and are development-only.

`node tests/viewer_renderer_tests.js` passed **15 pure renderer checks**: nearest
depth selection, hidden edges, coincident-geometry ambiguity, degenerate triangles,
topology mappings, payload bounds and viewport clipping. Node is a developer-only
test dependency. The viewer uses a depth buffer and bounded raster work; these
tests do not claim real browser-interaction evidence.

A relocated shared-library bundle passed create/edit/reopen/query/STEP/STL/MCP
with an empty PATH and SDK/loader variables removed. Every bundled Mach-O
executable/library was audited for non-system absolute load paths. This is
relocation evidence on the development Mac, not a fresh-machine claim.

Linux arm64 validation ran natively under Docker Desktop's Linux aarch64 VM in
Ubuntu 24.04 with GCC 13.3.0, using source-built pinned OCCT 8.0.1 and FreeType
2.14.3. The final run passed **all nine CTest suites (1,118 checks plus CLI) in
4.97 seconds**, **118
schema checks**, **181 official SDK checks**, and **15 renderer checks**. Polling
counts vary; the earlier successful Linux run had 120/186 schema/SDK checks.
The relocated bundle passed with an empty PATH. A separate fresh Ubuntu 24.04
runtime image passed create/edit/reopen, failed-edit rollback, saved views, durable
jobs/retry, STEP/STL export and MCP in 0.7 seconds. The runtime script asserts
that Python, Rust, Node, CMake and compilers are absent. `ldd` confirmed every
non-glibc dependency came from the bundle, including the pinned FreeType.

Linux validation exposed and fixed three distribution issues: Ninja needed
FreeType declared as an OCCT ExternalProject dependency; OCCT's Linux font code
needed Fontconfig headers; installed SDK libraries needed `$ORIGIN` RPATH for
transitive dependency lookup. The bundle includes Fontconfig/Expat and their exact
distribution notices/version record. Its runtime scan retains the configured
FreeType instead of traversing an unused system duplicate. GCC notices follow
the actual bundled runtime package version, which can differ from the compiler.
These fixes passed the full source build and final runtime gate without OCCT
source patches or loader-environment workarounds.

The browser tool refused `file://` navigation to the viewer under its URL policy.
No browser workaround was attempted. HTML generation and saved pick behavior are
tested; interactive rendering/picking remains unverified by a real browser here.

## Reproduce

Convenience local build (ignored preset/SDK, not a repository requirement):

```sh
cmake --preset local
cmake --build --preset local
ctest --preset local
```

Fresh dependency and portable bundle commands are in `README.md` and
`docs/DISTRIBUTION.md`. Current fully built shared dependency workspace:
`build-deps-packaging`; service build: `build-package`.

```sh
cmake --build build-package --parallel 4
ctest --test-dir build-package --output-on-failure
cmake --build build-package --target bundle-check
cpack --config build-package/CPackConfig.cmake -C Release -B build-package/packages
```

Optional independent contract/client checks (install
`tests/mcp_sdk_requirements.txt` into a development-only virtual environment):

```sh
python3 tests/schema_conformance.py build-package/agent-3d-cad
python3 tests/mcp_sdk_smoke.py build-package/agent-3d-cad
```

Local preview artifact:
`build-package/packages/agent-3d-cad-0.1.0-Darwin-arm64.tar.gz`.
SHA-256: `88f38e1405a26a650c79db2117f1bfabfd0b8b2f858b88fece5f59e4715a4b27`.
It inherits this SDK's **macOS 27.0 minimum**; see packaged provenance. Do not
claim it supports older macOS versions. CI builds use their own explicit baseline.

Linux preview artifact (tested Ubuntu 24.04 aarch64, glibc 2.39):
`build-linux-validation/agent-3d-cad-0.1.0-Linux-aarch64.tar.gz`.
SHA-256: `4fac3f62efc789c5d4bf6ac8aca50695b7fce7321446e34996522ed46a4ad4df`.
`build-linux-validation/RESULTS.md`, `validation.log`, `LastTest.log`,
`final-loader-audit.log` and `provenance.json` retain the exact commands, results,
runtime lookup evidence and packaged hashes. No archive has been published.

## Remaining acceptance gates and limits

Native platform validation passed all five CI lanes, including Windows x64
relocation and preview packaging. See the latest increment for exact results and
archive hashes. Remaining independent gates:

1. In a normal browser, open a generated HTML artifact, pick a face/edge, copy or
   save its reference, resolve it through the service, make a selective edit,
   and load the new `.view.json`. The UI includes these controls; real interaction
   still needs evidence because of the browser tool's local-file policy.
   A simple local demo is ready in `build/manual-review`, document `pick_demo`,
   revision 1. Its HTML is
   `build/manual-review/exports/pick_demo-eval_55b5a63bd8cd6fba5d77016be1dea535.html`.
   The pending user request is to select an edge and paste its copied reference;
   then resolve it, add a selective fillet, and verify the updated view manually.
2. Before a public release, the owner must choose the original-code license and
   arrange signing/notarization and applicable source/relinking distribution.
   The preview archive records the pending license decision; it is not a release.

Other deliberate limits:

- Numeric profiles are not a sketch constraint solver. No assemblies/joints,
  full drafting/GD&T, manufacturing certification, general scripts or remote multi-tenancy.
- Geometric selectors encode intent; indices and OCCT history are not universal
  persistent topology names. Face picks are inspectable; editing currently uses
  edge selectors. A changed cardinality fails explicitly.
- Every revision/query rebuilds intent; there is no persistent B-rep cache.
  Stored evaluations and viewer files may be removed only if their picks are no
  longer needed. No automatic evaluation/artifact garbage collector is present.
- Viewer/evaluation artifacts permit 64 MiB; model/transport input stays 1 MiB.
  Viewer rendering is bounded and can require narrowing to a feature for a very
  complex model. Occluded geometry is not selected through a visible face.
- macOS memory enforcement samples physical footprint every 10 ms and can
  overshoot between samples. Linux address-space limits and Windows Job Object
  limits have different memory accounting. All platforms have worker deadlines.
- Synchronous geometry calls wait for workers; use cad_job for a responsive MCP
  connection during work. Jobs report coarse phases, not estimated percentages.
- Workspace files are trusted local storage, not hostile-user or NFS sandboxing.
  Abrupt exits can leave ignored temporary worker/staging files. Request receipts
  reconcile commits; no filesystem durability rollback is promised.

Next authorized product work is drawing layouts/hatching/angular dimensions/
explicit tolerances, then performance/caching and assemblies. Native platform
validation has passed. The live Codex viewer loop has real rendering/selection/
refresh evidence on macOS; other GPU hosts and offline artifact interactions
retain their own independent gates.

## Direct MCP model — 13-tooth duplex #35, keyed 5/8-inch bore

On 2026-10-06, the installed native service created
`duplex_35_sprocket_13t_keyed`, revision 1, for the user's explicit 13-tooth,
5/8-inch bore and keyway request. Its 53 supported features create two aligned
13-tooth rows with exact seat/working/topping arcs, a type-B hub and a through
3/16-inch keyway. Complete intent, sources, dimensions and edit limits are in
`examples/duplex-35-sprocket-13t-keyed.create.json` and its `.prompt.md`.

Native validation reports one valid solid, 273 faces, 802 edges and volume
17,888.61949253724 mm3. Independent OCCT 8.0.1 STEP read-back with healing disabled
retained one valid solid and volume within 0.01 mm3. All 568 clearance/material
probes passed, including both tooth rows, bushing seating, phase, inter-row
clearance, bore and keyway walls. Exact cylindrical seat/working/topping face
counts are 26/52/52. STL validation found 3,512 triangles and 5,268 edges, each
with two incident triangles at 0.00001 mm quantization. Export did not change
editable intent. Verification source and evidence: `build/sprocket35-13t/`.
The actual Codex MCP App was opened once as `sprocket35_13t_keyed_01a113a0`;
DOM and GPU screenshot inspection confirmed revision 1 and the two rows/keyway.

The first create job was rejected for duplicate `tooth_row_cut_13` identity;
no document was published. Corrected job `duplex35_13t_keyed_20261006_v2`
succeeded. STEP/STL are saved in the native workspace's exports directory.
No service implementation, installed bundle or pre-existing model changed.
Hub dimensions are documented assumptions; manufacturing fit/material/load rating
remain unspecified. Existing platform-validation next tasks above are unchanged.

### Follow-up — selected hub edge rounded to 1 mm

The user's MCP App Quick Edit selected revision-1 `edge-787`, the circular
outside edge of the projecting hub at Z=-17.5006 mm, and requested a 1 mm round.
`cad_read`, `cad_context` and `cad_resolve_selection` confirmed current revision 1
and one unique circular edge. `cad_apply(expected_revision=1)` added parameter
`hub_edge_radius=1`, the `rounded_hub_edge` fillet and output change, committing
revision 2. The edit recipe is
`examples/duplex-35-sprocket-13t-keyed.edit.json`.

Revision 2 is one valid solid, 274 faces, 804 edges, volume
17,869.923224229355 mm3. Topology confirms the new toroidal face spans exactly
Z=-17.5006 to -16.5006 mm. The existing live viewer followed revision 2, cleared
the old selection and displayed the rounded outer hub edge; its DOM/screenshot
were inspected. Revision-qualified STEP/STL exports succeeded as `-r2.step` and
`-r2.stl`. No additional manufacturing fit or load validation is claimed.

### Drawing export — 13-tooth keyed sprocket revision 2

The user requested drawings of the current sprocket. Installed `cad_job` /
`cad_drawing` job `sprocket35_13t_keyed_drawing_r2_v1` generated an A3 landscape
sheet at 2:1 with top/front/right/isometric views and a true hub section at
Z=-5 mm. PDF/SVG and five 1:1 mm DXFs are in the native workspace export folder
`duplex_35_sprocket_13t_keyed-r2-drawing-eval_93a432aeb7e1e6b8ad2bd3e4d5d36d85`.
The reusable request is `examples/duplex-35-sprocket-13t-keyed.drawing.json`.

All eight requested dimensions resolved to actual geometry: OD 44.45, overall
length 31.75, row spacing 10.1346, row width 4.1148, hub diameter 28.178125,
bore 15.875, keyway width 4.7625 and opposite bore wall-to-keyway-roof 17.8816 mm.
Poppler rendered the single-page PDF for visual inspection; pypdf confirmed all
eight printed dimension labels and revision 2. All five DXFs passed independent
ezdxf audit with zero errors/fixes and millimeter units. An initial validation
process loaded a Python 3.14 NumPy into Python 3.12; reusing the bundled compatible
NumPy corrected the verifier environment without changing artifacts. Native
source read-back remained byte-equivalent JSON to its pre-export response.
Review evidence and the complete 132,638-byte ZIP are under
`build/sprocket35-13t/`; no model revision or service implementation changed.
