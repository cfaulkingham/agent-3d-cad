# Implementation roadmap

Use this with `HANDOFF.md`; a milestone is complete only when its acceptance
evidence exists. Work in useful increments, not broad API parity passes.

Implementation update (2026-10-06): M1 selection/preview/viewer, M2 jobs/bundling,
and M3 modeling/import/native Windows code are implemented. Native macOS arm64
and Linux arm64 suites, relocated shared-library bundles, and a fresh Linux
runtime-only container have passed. An independent Arch Linux x86_64 build also
passed all 17 suites, relocation, schemas, official MCP SDK and drawings; the
user-created GitHub repository's initial CI passed both macOS and both Ubuntu
architectures. The final packaging follow-up passed all 19 suites, schemas,
official MCP SDK checks, empty-PATH relocation and preview archive generation on
all five native CI lanes, including Windows x64. Every downloaded archive passed
independent manifest/hash verification. The original acceptance gates below are
retained: offline artifact/browser and other GPU-host interactions still need
independent evidence. The live Codex MCP App now has actual
macOS arm64 rendering/selection/refresh evidence, and current M4/M5 sources
passed the Linux arm64 builder and clean runtime container. See HANDOFF.md.

## M0 — editable native backend (implemented, local validation)

- C++20 build with exact OCCT 8.0.1 and checksum-pinned JSON dependency.
- Named parameters, ordered features, graph validation, exact solid construction.
- Box/cylinder/cut/fuse/all-edge fillet; bounds, volume, area, topology counts.
- Atomic saved revisions, expected-revision checks, cross-process writer lock.
- Shared service reached by CLI and MCP stdio, complete discoverable schemas.
- STEP and binary STL export; historical revision query/export after restart.

Acceptance: CTest suites `model`, `geometry`, `transactions`, `protocol`, and
`cli_smoke` pass against real OCCT. Include analytic-volume checks, a STEP
read-back, invalid edit rollback, cross-process lock exclusion, MCP export with
JSON-only stdout, and the four-hole mounting plate edited across CLI processes.

M0 does not establish end-user installation, host-app integration, hard execution
deadlines, or stable topology naming. Linux CI and the fresh OCCT dependency
build need their own evidence; do not infer that from a macOS SDK build.

## M1 — geometry selection and a reviewable model

Suggested task order:

1. Add evaluated revision/build identity and `cad_query` topology enumeration.
   Return face/edge type, measurements, and explicit selection lifetime.
2. Add a selector schema with feature scope, predicates, and expected count.
   Implement selected-edge fillets. Tests must cover missing/ambiguous selections
   after a parameter edit and prohibit stale selection reuse.
3. Add viewer mesh + edge payloads whose triangle/edge mapping shares that exact
   evaluation identity. Do not confuse STL triangle indices with B-rep faces.
4. Add a small browser viewer or adapt isolated `text-to-cad` UI components.
   Preserve protocol ownership; no runtime dependency on Python or sibling repos.
5. Add dry-run candidate previews; distinguish draft preview from committed HEAD.

Acceptance: a human picks a face/edge, an agent receives a revision-qualified
reference, a selective edit updates the viewer, and an ambiguous rebuild produces
a repairable error. Restart and saved artifact viewing work without source code.

## M2 — reliable jobs and packaged macOS/Linux preview

- Move kernel evaluation to bounded worker processes; never fork a live threaded
  runtime without a safe spawning design. Keep publication in the coordinator.
- Durable job identity/state; queue admission; progress; cancel; wall-time and
  memory budgets. Define request deduplication and crash-recovery semantics.
- Per-document conflict control with lock recheck at publication; preserve M0's
  ability to read old revisions while work runs.
- Bundle native libraries and OCCT resources, relative loader paths, notices,
  dependency provenance, and install/uninstall instructions.
- Publish full input/output schemas, inspect host interoperability, and implement
  authorization/origin controls before exposing any future HTTP endpoint.

Acceptance: kill/cancel/time out a worker during a build, continue serving reads,
and preserve HEAD; retry without duplicate commits. Run the full example on a
fresh machine/container with no Python, Rust, Node, CMake, or compiler installed.
No absolute developer-machine library paths remain in a shipped executable.

## M3 — broader parametric modeling and portability

- Explicit workplanes, sketch profiles, extrusion/revolve/loft/sweep, transforms,
  holes, patterns, reusable components, and bounded parameter expressions.
- Define constrained sketches as a separate effort; do not equate numeric
  profiles with a general constraint solver.
- STEP import as a feature with immutable external content identity.
- Durable feature/shape provenance and tested reference evolution.
- Windows locking/publication implementation and real platform CI; release
  coverage for the remaining architectures.

Acceptance: multiple representative parts can be authored and edited using
reusable parameters/features; imported components remain reproducible; Windows
passes the same persistence and geometry contracts.

## M4 — integrated create–view–select–edit experience (authorized 2026-10-06)

- Embedded MCP App resource and host bridge; no end-user JS tooling or network.
- Model library and source feature tree over committed documents.
- WebGL viewport, occlusion-aware face/edge picking, camera retention and capture.
- Native live-view state and isolated mesh jobs follow HEAD automatically.
- Validated selection/context and explicit Quick Edit requests through the host,
  with Copy request fallback. Open once and reuse the view across edits.
- Bundle the native-cad agent skill and reproducible integration checks.

Implemented as a preview. Native service + actual app bridge/controller tests
cover a visible-edge pick, unique selector, selective fillet, automatic refresh,
camera retention and rejected-edit rollback. Actual WebGL/host interaction was
observed on macOS arm64 in Codex: a human edge pick, reference-qualified Quick
Edit handoff to the chat composer, selective fillet, same-view revision refresh,
camera retention and stale-pick clearing. This host requires the user to submit
the composer draft. Other hosts/platforms retain their own acceptance gates;
no test stub substitutes for that evidence. Full sibling parity is not implied.

## M5 — native drawings (authorized 2026-10-06)

- Revision-qualified `cad_drawing` through the shared CLI/MCP service and jobs.
- Native B-rep hidden-line projection and planar cross-sections.
- Geometry-checked dimensions, center marks, labeled sheets and revision metadata.
- Native PDF/SVG sheets and independent per-view 1:1 mm DXF outputs.
- Aligned first-/third-angle view arrangements and clipped section hatching,
  with an explicit grid/outline-only option. See HANDOFF.md for local evidence.
- Geometry-checked minor/reflex angular dimensions and explicit symmetric,
  deviation and limit tolerances, with optional general allowances and per-view
  DXF preservation. See HANDOFF.md for local evidence.
- Saved parameterized recipes for explicit regeneration after model edits.
- No end-user PDF converter, Python or Node runtime.

Acceptance: known solid/view extents, visible/hidden geometry, analytic circles,
sections and dimension values; explicit missing/ambiguous-reference failures;
regeneration without changing historical source/drawing revisions; native CLI,
MCP, schema and job tests; visually inspected representative PDFs; relocated
bundle generation with empty PATH. HANDOFF.md records executed evidence.

## M6 — editable assemblies (authorized 2026-10-06)

- Named part instances of earlier solid features, with editable source geometry
  and parameterized placements. Distinct instances remain separate exact solids.
- Deterministic rigid datum mates: explicit parent/child frames, offsets and
  angles, grounded roots, single-parent acyclic graphs and explicit conflicts.
- Atomic placement/mate edits through existing apply/preview/jobs workflows.
- Per-part world transforms, measurements and evaluated topology ownership.
- Exploded drawing views with parameterized part offsets, independent of saved
  assembled geometry. Native PDF/SVG/DXF output and projection-cache separation.
- Source-grouped BOM quantities and editable metadata, deterministic item numbers,
  native JSON/CSV exports, sheet tables and geometry-checked part balloons.
- Live per-part hide/show/isolate with persisted view context, visible-only
  rendering and picking, selection clearing and recovery through Show all.

Acceptance: known rigid transforms, independent/duplicate instances, chained and
rotated mates, source parameter edits and historical revision preservation;
invalid/cyclic/conflicting mates must preserve HEAD. Verify topology ownership,
STEP/STL geometry, cold/warm caches, preview/live paths, schema and MCP contracts,
native and asynchronous drawings, and visually inspected exploded sheets.
BOM acceptance additionally covers grouping/numbering, metadata and membership
edits, independent CSV/JSON readers, surface visibility and ambiguous balloon
rejection, table/label fitting, cache attachment invalidation, historical recipes
and asynchronous exports.
Visibility acceptance covers hidden occluders and highlights, all-hidden views,
reopen/revision/retarget behavior, pending context races and unchanged model/BOM.
See `ASSEMBLIES.md` for the implemented subset and `HANDOFF.md` for executed
evidence; no additional platform acceptance is implied by source implementation.

## Later, only when driven by actual workflows

Geometry/projection caching is implemented: bounded, checksummed exact feature
snapshots and complete view sets, content/build invalidation, fresh render and
evaluation identity, corruption/deletion fallback, and coordinator publication.
Native regression tests and a reproducible threaded-part benchmark are provided;
executed local results are recorded in HANDOFF.md. No public cache API is needed.

Further work: nested assemblies, general mate constraint solving, kinematics,
hierarchical BOMs, full drafting/GD&T, multi-sheet drawings, manufacturing
checks, distributed execution, remote collaborative editing, and additional
authoring frontends. Python/build123d API parity remains outside the product.
