# Progressive viewing, markup and manufacturing workflows

Authorized 2026-10-11. This is the completion ledger for the next local
text-to-cad comparison priorities. The requirements below are implemented in the
native preview, with executed evidence summarized at the end. The native service remains
the single source of modeling behavior; no sibling runtime or model-supplied
code is introduced.

## Progressive component loading and adaptive detail

- Publish a source-qualified assembly manifest with complete bounds, occurrence
  hierarchy and transforms before all display geometry is available.
- Build and transfer individual component meshes independently; repeated source
  geometry shares construction/cache work. Display the first available component
  before waiting for the entire assembly. Byte-chunking one complete mesh alone
  does not satisfy this requirement.
- Refine visible geometry from camera-dependent projected error, with hysteresis
  and bounded resident/cache memory. Coarsen under pressure, cancel obsolete work
  and report geometry that cannot fit instead of silently omitting it.
- Keep initial framing stable, preserve the previous usable model while an edit
  loads, and reject stale source/pose/component results. A failed replacement
  preserves the last usable scene and reports the failure.
- Keep exact geometry, manufacturing exports and measurements independent of
  display detail. Picks must resolve to the correct source occurrence and exact
  topology across detail swaps; enumeration is never persistent design intent.
- Verify actual native component results, repeated instances, source changes,
  cancellation and memory pressure, and exercise the shipped viewer in a real
  browser. Record first geometry and completed/refined state separately.

## Viewport markup

- Draw freehand strokes, lines/arrows, rectangles/ellipses and text over the
  displayed model, with undo, redo, clear and visible editing controls.
- Associate bounded markup data with its source and camera/display frame; never
  silently attach old marks to a changed model or different view.
- Preserve recoverable markup appropriately, include visible marks in saved PNGs
  and agent screenshots, and expose useful qualified context to the main host
  chat. Preserve the existing main-chat interaction choice.
- Make pointer/camera ownership explicit while drawing. Verify undo/redo,
  transformed viewport sizes, source/frame changes, screenshot races and actual
  composed pixels. Markup never edits source geometry or becomes a topology pick.

## Manufacturing skills

- Ship discoverable workflows for native process review and redesign, including
  measured comparison of candidate print orientations.
- Cover additive-process choices beyond FDM through current sourced guidance,
  distinguishing measured geometric facts from unsupported process checks.
- Provide supplier-specific SendCutSend preflight, sourced STEP discovery and
  purchasing provenance, and native slicing/appropriate printer-app handoff.
- Keep requirements traceable to actual material/machine/vendor sources and
  preserve unknown coverage. No manufactured certification, guessed tooling
  threshold or implied order/physical-action authorization.
- Include the skills and their references in real portable packages, with valid
  internal links and supported tool contracts. No end-user Python dependency.

## Articulated gripper reference project

Build a substantial editable two-finger gripper with multiple reusable parts,
hierarchical hardware occurrences, coupled joints, named poses and clear design
assumptions. Use it to exercise editing, progressive viewing, markup,
manufacturing reviews and portable outputs together.

Required evidence includes independent placement/motion/clearance checks,
parameter edits and invalid-edit rollback, BOM quantities, exact STEP and visual
exports, drawings and a movable manufacturing package. Review more than one
pose and compare build orientations for selected fabricated parts. Capture
actual images and inspect them. The model is a reference design; fabricated
performance is not established by geometric validity.

Record concrete modeling/motion limitations encountered while making and
reviewing the project, with reproducible requests and observed results. Do not
claim a general solver, flexible-body mechanics or production readiness from
successful forward kinematics. Keep automated fixtures isolated from mutable
user projects and distinguish developer generators from native end-user inputs.

## Acceptance record

Implemented on `codex/progressive-cad-workflows`:

- [Progressive viewing](PROGRESSIVE_VIEWER.md) uses independent native source
  meshes, three detail levels, shared occurrence transfers, camera/perspective
  visibility, bounded pressure-driven coarsening and qualified selections.
  Visible omissions or load failures prevent capture readiness; memory refusals
  can recover when the view changes without retrying true geometry failures.
- [Markup](MARKUP.md) provides all six requested tools with source/frame-bound
  persistence, undo/redo, visible controls, composed PNGs and an explicit
  capability-gated image/context handoff. Main-chat requests remain the normal
  interaction. Drawing never modifies the CAD document.
- Four manufacturing workflow skills and their examples/references are in actual
  verified native, Agent Plugins and Claude packages. The installed native
  executable and installed orientation fixtures passed **110 checks**; all
  inventoried archive bytes and **263 native/Claude / 421 Plugin** relative links
  were verified.
- The [48-solid gripper](../examples/articulated-gripper/README.md) passed
  **947 reference checks** and exposed reproducible collision-unaware motion,
  rejected closed loops and the 23-leaf automatic clearance limit. Its README
  records measured gaps, orientation results, assumptions and incomplete pair
  coverage. Developer Python only prepares/tests native inputs.

Final macOS arm64 validation includes all **86 CTest suites** across the full run
and corrected test-harness rerun, **1,806 official MCP SDK interoperability
checks**, **913 independent schema checks**, **64 focused
progressive schema checks**, **4,803 native progressive checks**, **64 native
markup checks**, and **47 scheduler checks**. Actual Chromium/native MCP tests
loaded the compiled viewer resource: **18 gripper/progressive/markup checks** and
**23 markup workflow checks**, with no browser exceptions. The first gripper
frame contained one occurrence before all 48 loaded; completion had no missing
geometry and used 3,978,396 of 33,554,432 source-cache bytes. This timing/size
observation is a fixture result, not a comparative performance benchmark.

The exact production build, official SDK result, package identities and evidence
locations are recorded in [HANDOFF.md](HANDOFF.md). Existing signing,
cross-platform and target-host installation gates remain separate. The initial
exact assembly/topology pass, monolithic draft/external/offline views, expanded
occurrence GPU buffers and the reference's modeling/motion limits remain future
work. No installed host plugin or user project was replaced.
