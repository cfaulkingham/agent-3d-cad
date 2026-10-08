# Assembly composition, fabrication workflows, and visual review

Authorized objective, 2026-10-08: address the remaining assembly-composition,
fabrication-workflow, and presentation/visual-review shortcomings identified in
the text-to-cad comparison. The software capabilities below are implemented
locally; HANDOFF records combined acceptance and separates executed
evidence from platform, host-installation and physical-hardware release gates.

## Assembly composition

- Compose reusable nested subassemblies, with explicit hierarchy and unambiguous
  occurrence paths. Repeated instances share source intent while retaining
  independent placements, selections, visibility, and exact solids.
- Preserve articulated child mechanisms when composing them; expose their motion
  and saved poses without losing parent transforms or silently freezing joints.
- Reuse components across saved documents through explicit, revision-pinned
  component snapshots and parameter mappings. Updates must be explicit atomic
  edits; source changes cannot silently alter a consuming historical revision.
- Rebuild only invalidated feature/component dependencies using bounded,
  checksummed native caches. Independent native work may use bounded processes;
  each kernel process remains serial. Missing/corrupt caches must rebuild safely.
- Provide hierarchical assembly inspection and BOMs, including rolled-up leaf
  quantities, subassembly structure, instance paths and explicit purchasing data.
- Keep query, STEP/STL, robot handoff, drawing/balloon, history, and selection
  semantics consistent through nesting, repeated sources, poses and edits.

## Fabrication workflows

- Produce revision-qualified manufacturing packages with source identification,
  per-part exact/mesh/drawing outputs, BOM/purchasing information, stated process
  assumptions and a portable manifest. Preserve editable source and history.
- Provide measured fabrication review with explicit process inputs and geometric
  evidence: mesh soundness, build-envelope/orientation/overhang checks, thickness
  and clearance checks where supportable, and process-specific findings for FDM,
  CNC, sheet/laser work and molding. Unsupported checks must say unknown, never
  pass merely because no measurement was made. Separate measured findings from
  cited process guidance; do not invent production tolerances or certification.
- Support purchased-part sourcing/import with recorded supplier, part identity,
  source URL and artifact identity; incorporate those parts into assemblies/BOMs.
- Integrate installed slicers through explicit executable/profile inputs and
  bounded cancellable jobs; create and inspect real G-code and preserve slicer,
  machine/material/profile provenance. Models cannot supply executable code or
  commands. Report missing tooling instead of fabricating sliced output.
- Provide an explicit printer handoff workflow with dry-run validation and
  separate intentional authorization for any physical print start. Installation
  and machine setup must be visible prerequisites, not hidden dependencies of
  ordinary native CAD creation or export.

## Presentation and visual review

- Add interactive clipping/sections, exploded assembly inspection, exact
  topology-based measurements and minimum clearance/interference queries.
- Make nested parts searchable, selectable, hideable and isolatable. Show
  supported imported-geometry feature candidates as inspection hints, not
  recovered editable history or stable topology identifiers.
- Add per-part appearance, camera/presentation presets, screenshots and visual
  annotation context for agent requests; retain explicit revision/evaluation
  identity and keep presentation transformations separate from saved geometry.
- Add declarative time-based mechanism/presentation sequences with play/pause,
  seek, speed and looping, including coordinated joints and exploded sequences.
  Do not execute artifact-supplied JavaScript or imply dynamics validation.
- Support review of external STEP, STL, 3MF, GLB, DXF and robot-description
  artifacts, with explicit units, references and failure states. Preserve native
  editable documents as the authoritative source when they exist.
- Exercise these controls in the actual rendered viewer, including stale picks,
  pending work, external revisions, state restoration and image context.

## Acceptance and progress

Every capability needs published schemas/documentation and agent guidance,
small isolated regression fixtures, failure/cancellation/history coverage where
applicable, and native package checks without developer runtimes for core CAD.
Validate geometry numerically and with independent readers; use real tool and
browser execution for integration claims. Record unavailable external hardware,
tools and platforms separately from executed evidence. Existing 1.0 installation
and distribution gates remain in RELEASE_1_0.md.

Initial audit: the committed baseline is `d21ea3a`. It has one-level assemblies,
whole-model geometry caching, articulated mates/poses, native robot export,
flat BOMs, drawings and basic live viewing. None of the new workstreams is
complete. First implementation: bounded nested assembly evaluation with stable
occurrence paths and preserved topology ownership, then its downstream consumers.

First increment now implemented locally: nested geometry and child kinematic
placement, hierarchy and leaf paths, rolled-up/hierarchical BOM metadata, group
exploded drawings and leaf balloons, bounded renderer validation, searchable
parts and group visibility/isolation. HANDOFF records native, schema, SDK and
actual browser evidence.

Second increment implemented locally: definition-scoped child mechanism controls
and presets, combined multi-definition drafts, atomic save/reset, and complete
composed URDF/SRDF/SDF graphs with shared-definition mimic coordinates. Native
and browser regression coverage also verifies persisted-view upgrades and camera
restoration without reusing stale geometry picks. Independent XML/FK and relocated
bundle evidence are in HANDOFF.

Third increment implemented locally: revision-pinned editable components across
saved documents, self-contained checksummed source snapshots, parameter bindings,
deterministic dependency maps, explicit updates and local-edit conflicts. Native
tests cover history, cancellation, replay, selectors, nested provenance, cache
restore, portable source-free rebuilds, STEP readback and composed robot export.
The live demo saves a shared child pose, displays its pinned source and local
edits, and keeps that consumer unchanged when its source advances. Protocol,
agent guidance, schemas and relocated bundle checks include these operations.
HANDOFF records exact execution evidence and the corrected verification issues.

Fourth increment implemented locally: checksummed native feature caches with
referenced-parameter/upstream fingerprints, selective rebuilds, independent
assembly compound restoration and current metadata on shape hits. Projection
keys follow the output closure, while validation still covers every declaration.
Corrupt or missing entries rebuild affected features; only coordinators publish
successful worker stages, within existing shared quotas and a new per-worker
feature-staging bound. Native regression and benchmark evidence are in HANDOFF.

Fifth increment implemented locally: revision-qualified native manufacturing
packages with complete editable source, unique leaf STEP/STL/PDF/SVG/DXF outputs,
saved occurrence transforms, BOM/purchasing metadata and relative hash manifests.
Generation is atomic, bounded and cancellable; failed work preserves HEAD and
existing exports. Caller process/material assumptions stay explicit, with process
review recorded as not evaluated. Independent readers and hashes, source-free
rebuilds, historical job replay and cancellation cover the package contract.
The actual curved-linkage package has six sources and seven visually inspected
drawing sheets. MANUFACTURING and HANDOFF record contract and executed evidence.

Sixth increment implemented locally: explicit native fabrication profiles with
oriented B-rep bounds, welded mesh topology, sampled exact wall chords, all-triangle
FDM overhang area, CNC cylinder-radius/point-access checks, sheet form/stock
checks, sampled signed mold draft/undercuts and exact saved-pose occurrence
clearance/interference. Unsupported global and process checks remain unknown,
with measured coverage and witnesses recorded. Source/revision/build-qualified
hashed reports can join manufacturing packages without changing intent or
relabeling failed findings. FABRICATION_REVIEW and HANDOFF record contract and
executed native, schema, SDK and runtime-package evidence.

Seventh increment implemented locally: sourced STEP imports verify caller/catalog
hashes and bind supplier/part/source identity to exact embedded artifact bytes.
Unchanged rigid copies and pinned components inherit that identity into nested
BOMs and manufacturing packages; contradictory BOM metadata fails validation.
Packages retain original supplier STEP artifacts alongside independent exports,
with relative paths and verified hashes. Geometry-changing features do not claim
the unchanged purchased identity. Catalog discovery/download stays an explicit
agent workflow. PURCHASED_PARTS and HANDOFF record contract and executed evidence.

Eighth increment: native stateful static review of checksummed existing plain
G-code preserves original bytes, a caller-associated committed source record and
a portable review hash ledger. Explicit machine/material/initial assumptions,
linear/relative/unit/reset and arc-extremum checks retain unsupported firmware
behavior as unknown. Inspection uses bounded isolated native workers with job
cancellation/replay and atomic package publication. This review prerequisite alone
does not implement installed slicing or printer handoff. GCODE_REVIEW defines
the contract; HANDOFF records validation evidence.

Ninth increment implemented locally: native `cad_slice` plan/run with explicit
OrcaSlicer 2.4.2 executable and resolved profile hashes, fixed native argv,
process-tree resource/cancellation containment, reviewed-plan snapshots,
exact-source numerical verification, actual G-code/effective settings and static
findings, and coordinator-only portable package publication. No hardware is
contacted. SLICING defines its limited backend/bed/single-solid contract;
HANDOFF records native containment tests and actual installed-Orca evidence.

Tenth increment implemented locally: saved uncapped live clipping, axis/offset
and kept-side controls, exploded leaf inspection with explicit direction overrides,
and source-preserving picking. Ordered context persistence guards delayed sync,
revision pruning, retargeting, stale snapshots and graphics recovery. PRESENTATION
defines the contract. Exact capped sections and the remaining visual-review
capabilities above remain required; HANDOFF records executed acceptance evidence.

Eleventh increment implemented locally: native exact pair measurement for
evaluated faces/edges and leaf occurrences, closest-point witnesses, acute
analytic angles, material overlap and minimum-clearance queries with explicit
all-leaf/subset coverage. Current source/build and unique geometric recovery
qualify references. Durable jobs and live pair/assembly controls preserve source
geometry, carry measurement context to agents and retire old evaluations.
MEASUREMENTS defines the contract; HANDOFF records validation evidence. Exact
sections and the remaining presentation/fabrication scope above remain required.

Twelfth increment implemented locally: native planar section intersections,
exact summed areas/perimeters, tangent contacts and hole-preserving cap meshes,
with a separate durable live slot and current plane/exploded-pose qualification.
The actual renderer displays native caps, preserves open bores and original face
selection, rejects cap edit picks, and retires old sections on source or geometric
presentation changes. Failed admission feedback remains visible across empty
sync. Native analytic fixtures, independent schemas/SDK, rendered revision-change
and restoration evidence, and relocated empty-PATH runtime checks are in HANDOFF.

Thirteenth increment implemented locally: persisted opaque default/per-leaf
colors, bounded native saved review views, atomic preset application, local PNG
download requests and revision-qualified rendered image context. The actual
browser restores camera/presentation/colors/visibility, retains matching native
sections, cold-reopens and captures a real PNG into an isolated host fixture.
A downloaded file is not established in that browser. APPEARANCE and HANDOFF
record exact native/controller/schema/SDK/package evidence and host limitations.

An earlier external workflow example executed real OrcaSlicer 2.4.2 slicing of
the saved curved arm with explicitly sourced stock profiles, checked effective
settings/actual G-code, recorded firmware coverage limitations and preserved
source/artifact hashes. A standalone browser diagnostic demonstrates layer seek,
play/pause and full-bed inspection. This is integration evidence for the next
implementation and did not establish a native slicer tool or complete presentation
capability. The ninth increment above now implements bounded native slicing.
The native printer plan is now implemented below. HANDOFF records the
actual execution, compatibility/bed-selection corrections and failed installed
CAD Viewer launcher handoff.

Fourteenth increment implemented locally: native offline printer handoff plans
capture checksummed G-code, profile snapshots, caller printer assumptions,
committed source identity and a portable hash ledger. Re-verification recomputes
native findings and readiness. No hardware is contacted; physical readiness stays
fail or unknown, with native upload/start explicitly unsupported. Result-storage
failure after package publication can leave an inspectable package without a
successful job result; verify exports before retrying. PRINTER_HANDOFF and HANDOFF
record executed native/schema/SDK/package evidence and this recovery limitation.

Fifteenth increment merged locally: bounded saved inspection notes retain
explicit source document/revision/evaluation/feature and evaluation lifetime.
Native bounds/entity centers support current numbered pins; historical anchors
retire without rebinding and text remains available to agent requests. PNG
capture composites visible pin labels. Native, controller, renderer, independent
schema/SDK and relocated empty-PATH fixtures are recorded in HANDOFF. Actual
browser review verifies hidden-owner filtering, original face-center anchoring,
revision retirement, reload and rendered image context. Combined acceptance is
recorded in HANDOFF.

Sixteenth increment merged locally: declarative playback provides
bounded saved native source keyframes, coordinated independent joints across
composed/reused definitions, native seek and pure presentation interpolation,
Play/Pause/Speed/Loop, paused restoration, source retirement and uncertain-ACK
reconciliation. PLAYBACK defines the contract. Actual browser acceptance covers
coordinated seeking, repeated loops, pause/reload, endpoints and source retirement;
the observed polling/control defects are fixed. HANDOFF records the exact native,
renderer, controller, schema, SDK and package validation scope.

Seventeenth increment merged locally: bounded external STEP/STL/GLB/3MF/DXF and
URDF/SDF/SRDF readers capture original bytes and explicit contained references
into verified portable read-only packages. Re-verification reparses the captured
originals. Live review-local picks carry the full review hash; native notes,
playback and exact operations are cleared or rejected when retargeting to an
artifact. Native editable source remains authoritative. ARTIFACT_REVIEW defines
format subsets, parser bounds, provenance and unsupported cases. Combined
acceptance is recorded in HANDOFF; existing platform, desktop-host installation and
physical-hardware release gates remain separate.
