# Editable CAD service specification

Status: accepted implementation direction; capability status is in `HANDOFF.md`.
Date: 2026-10-06. Project: `agent-3d-cad`.

Implementation note (2026-10-06): the M0-specific descriptions below record the
original baseline and architectural direction. M1–M3 capabilities have since
been implemented as a native preview. `PROTOCOL.md` describes current contracts;
`HANDOFF.md` separates executed acceptance evidence from remaining platform,
browser-interaction and release gates. The acceptance requirements below remain
in force; implementation alone does not establish platform certification.

Experience extension (2026-10-06): the user requested the next step toward the
neighboring text-to-cad experience. M4 adds a bundled MCP App with live revision
updates, WebGL selection, model discovery and shared selection context. It uses the same
native Service and worker isolation, without an HTTP listener or sibling runtime.
See `LIVE_VIEWER.md` and the current protocol for implemented behavior and limits.

Interaction revision (2026-10-09): users select a face or edge in the viewer and
make requests in the main host chat. The embedded Quick Edit composer is removed;
agents retrieve the current qualified selection through `cad_context` using the
retained view ID. Native context contracts and stale-reference checks remain.

Modeling extension (2026-10-10): native shell/offset/thicken, derived planar
sketch operations, solid mirror/split/intersection, captured text/SVG/DXF and
controlled extrusions/sweeps are implemented. Separate sheet-metal and surface
phases add exact direct-edge bends/developed blanks and editable rational
patches, rectangular UV trims, manifold sewing and explicit solid creation.
Intentional surface outputs extend the original solid-only output contract;
they retain zero material volume until solid creation or thickening. Sketches
remain intermediate regions. Manufacturing and assembly roots require solids.
These bounded operations do not establish a constraint solver, arbitrary-source
reconstruction or full build123d API parity. See the current protocol and
SHEET_METAL.md/SURFACES.md for exact supported scope.

Drawing extension (2026-10-06): the user authorized M5 native engineering
drawings. `cad_drawing` derives revision-qualified views and geometry-checked
dimensions from exact solids in isolated workers, exports PDF/SVG sheets and
per-view DXFs, and saves parameterized drawing recipes. See `DRAWINGS.md` for
the supported drafting subset; this extends the original exclusions below.

Assembly extension (2026-10-06): the user authorized multiple editable parts,
placement, mates, and exploded drawings. M6 began with one-level assembly features with
named source-part instances, deterministic rigid datum mates, and view-specific
drawing explosions. See `ASSEMBLIES.md` for the current contract and limitations.

Modeling/motion extension (2026-10-07): the user authorized richer native
modeling and moving mechanisms. Exact curve profiles and paths, symmetric
chamfers and circular patterns are implemented locally, along with articulated
mates, coupled motion, named poses and native live preview/reset/save controls.
Native URDF/SRDF/SDF export preserves frames, coordinates and couplings with
explicit physical inputs. `ROBOT_EXPORT.md` describes the handoff and validation
limits. `MODELING_MOTION.md`
records the full completion contract; `PROTOCOL.md` defines implemented inputs.

Desktop/install extension (2026-10-07): the user requested simple client setup,
versioned platform artifacts, a standalone CLI viewer, exports and old-project
access. A Tauri shell reuses the existing renderer and communicates only with the
bundled native service over stdio. It shares workspace/view selection context
with connected agents; geometry stays in C++ workers. Export dialogs write
independent STEP/STL/PDF/SVG/DXF outputs. No Electron runtime or end-user language
installation is permitted. See `DISTRIBUTION.md` for release gates and platform
runtime prerequisites.

Presentation extension (2026-10-08): saved live clipping planes and exploded leaf
inspection are independent of source geometry and revision history. CPU picking
and GPU rendering share displayed coordinates; exact measurements and topology
references stay source-qualified. This initial uncapped visual clipping is not
exact section geometry. See `PRESENTATION.md` for its implemented contract.

Measurement extension (2026-10-08): `cad_measure` computes exact source-pose
face/edge/leaf-pair distances, closest-point witnesses, supported analytic angles
and material overlap. Bounded assembly queries state all-leaf or subset coverage.
Current evaluation/source/build identity and unique geometric reference recovery
qualify results; no stable enumeration naming is claimed. Durable native jobs
and the live panel preserve geometry/history and retire obsolete evaluations.
See `MEASUREMENTS.md` for tolerances, coverage and failure semantics.

Section extension (2026-10-08): the same `cad_measure` service intersects native
B-reps with a displayed-world plane, accounting for visual exploded offsets
without editing source placements. Exact curves, tangent contacts and material
caps retain holes and explicitly summed per-solid areas. Derived section IDs
are review data, never source topology references. A separate durable live slot
retires changed plane/placement/source results. See `SECTIONS.md` for bounds,
qualification and viewer behavior; executed evidence remains in HANDOFF.

## 1. Product outcome

Fabrication extension (2026-10-08): revision-qualified native manufacturing
packages preserve complete editable source, unique leaf exact/mesh/drawing
artifacts, saved assembly transforms, BOM/purchasing identity and explicit caller
process assumptions. Atomic generation preserves history. `MANUFACTURING.md`
defines the implemented package contract. Native exact/sampled process checks,
bounded source review and saved-pose clearance/interference are in
`FABRICATION_REVIEW.md`, with explicit unsupported checks. Sourced STEP identity
and original supplier artifacts are in `PURCHASED_PARTS.md`; discovery remains
an agent/catalog workflow.
`GCODE_REVIEW.md` describes native stateful static inspection of caller-supplied
plain G-code with explicit bounds/temperatures/initial state, preserved bytes
and atomic review packages. Caller-supplied review alone does not establish
slicing provenance. `SLICING.md` defines the implemented native OrcaSlicer 2.4.2
plan/run workflow: explicit installed executable/profile identity, fixed argv,
bounded cancellable processes, actual output/effective settings and atomic
source-qualified packages. `PRINTER_HANDOFF.md` defines portable offline printer
plans and re-verification; upload and physical start remain separate workflows.
Appearance/presets, saved inspection notes and declarative native playback are
implemented in `APPEARANCE.md`, `ANNOTATIONS.md` and `PLAYBACK.md`. External file
review is a bounded read-only representation in `ARTIFACT_REVIEW.md`, with no
recovered editable history. `COMPOSITION_FABRICATION_REVIEW.md` records the
combined implementation scope; HANDOFF records its validation status.

Release focus (2026-10-07): 1.0 targets ChatGPT desktop and Claude Desktop plugins
with the embedded viewer and local CAD engine. Standalone Tauri and CLI onboarding
are deferred. Installation and actual two-host acceptance gates are in
`RELEASE_1_0.md`; prior preview implementation does not establish 1.0 readiness.

An agent can create a saved design, reopen it in another session, make a targeted
edit, inspect the resulting geometry, and export a manufacturing file. The user
installs a native application bundle, not a language environment or compiler.

The agent supplies structured modeling intent. A C++ service evaluates it with
OpenCascade. There is no generated C++ compilation, generated Rust compilation,
Python execution, or embedded shell in the modeling path.

Example acceptance journey: create a plate with four mounting holes and rounded
edges; display and select geometry; change thickness and hole spacing; measure
the rebuilt part; reject an invalid radius without losing the valid design;
restart; reopen; export STEP and STL on a machine without developer tools.

Full build123d API parity is not a success criterion. Existing Python programs
are not accepted model inputs. Early implementation emphasizes agent editing
and reliable state transitions over feature count.

## 2. Layers and ownership

```text
MCP client / native CLI / future browser client
                    |
              application service
              /                 \
    model + revision store      geometry jobs
                                  |
                          C++ OCCT adapter
                                  |
                          OpenCascade 8.0.1
```

- **Model layer:** schema validation, parameter resolution, feature dependencies,
  semantic edits. No transport, file writes, or OCCT types.
- **Application service:** transaction lifecycle, revision checks, queries,
  exports, capability discovery. One implementation serves every adapter.
- **Storage:** saved intent and committed revisions. Derived geometry is
  reproducible work, not the authoritative editable model.
- **Kernel adapter:** shape ownership, construction, validity checks, measurements,
  tessellation, exchange. No public OCCT handles or pointer-derived identifiers.
- **Adapters:** input/output only. MCP lifecycle and tool envelopes; CLI flags;
  later HTTP asset delivery and viewer events.
- **Viewer:** renders a specific revision, submits selections with their revision,
  and displays diagnostics. It does not parse source code or rebuild geometry.
  Per-part hide/isolate is saved presentation state for a live view. Hidden parts
  are excluded from rendering and picking; the document, measurements, exports
  and BOM retain the complete assembly. Revision updates retain matching part
  IDs and prune removed IDs; changing documents resets visibility.

The initial process evaluates synchronously. The target architecture isolates
geometry work in bounded worker processes (M2). The transport process must
eventually remain responsive during builds, cancellations, and kernel failures.

## 3. Document contract

A document stores a schema version, millimeter units, named parameters, named
features, their dependencies, and one output feature. A service revision wraps
that intent with a document identity, revision number, and kernel version.

Features form an acyclic dependency graph serialized in topological order.
IDs belong to the design and survive parameter edits. Replacing a feature
preserves its ID; removing it requires resolving downstream references in the
same transaction. Every feature is evaluated and checked, including features
not reachable from the chosen output. This avoids saving hidden broken branches.

An assembly feature keeps independently editable source parts as a compound of
positioned exact solids. Part IDs and rigid mate IDs are saved design intent.
Each mate defines one child's complete placement relative to its parent using
explicit source-coordinate datum frames, an offset and a rotation. The graph
is a forest: at most one incoming mate per child, no cycles, and no explicit
placement on mated children. Roots are grounded by their placement. Ambiguous
constraints never trigger a guessed solver result. Nested assembly inputs retain
their child transforms and occurrence hierarchy. Ordinary
solid operations consuming assemblies are rejected;
geometry edits happen on the upstream source features.

Revision-pinned components embed the captured source document and its checksum,
then materialize the selected dependency closure as local editable features.
Explicit scalar bindings connect source dimensions to consumer parameters. Only
capture/update reads the source workspace; normal rebuilds remain self-contained.
Updates preserve the component root ID and require explicit replacement of local
edits. See `COMPONENTS.md` for ownership, integrity and bounded provenance.

Assembly BOM rows group instances by source input. Quantities derive from part
membership; saved metadata may supply explicit item numbers, part numbers,
descriptions and materials, with no inferred manufacturing attributes. Drawing
balloons derive numbers from that same BOM and bind to visible exact part
surfaces using source-coordinate anchors. BOM/table export and exploded
annotation generation preserve the committed model and historical artifacts.

M0 supports numeric literals and explicit parameter references. No expression
strings are evaluated. Initial dimensions and coordinates use millimeters;
future angles and expressions must introduce explicit dimension checking.
For future expressions, prefer a bounded arithmetic expression tree with named
inputs. Patterns and reusable components should remove repetitive authoring
without requiring arbitrary code execution.

STEP is an exact geometry output; it does not replace the editable model.
STL is an approximate mesh output whose coordinates use the document unit.
Artifacts must remain usable independently of source documents and service
memory. Imported STEP will initially be an opaque imported feature, not an
inferred reconstruction of the originating feature tree.
Optional purchasing provenance binds caller supplier/part/source identity to
the verified raw STEP bytes. Unchanged rigid copies and pinned component reuse
retain that identity; geometry modifications do not imply the unchanged purchase.
Native CAD does not fetch supplier URLs or execute catalog-supplied code.

## 4. Transaction and persistence invariants

1. The caller supplies `expected_revision` for every existing-document edit.
2. Acquire the writer lock before checking that revision. Concurrent conflicting
   edits must never silently overwrite one another.
3. Apply all operations to a candidate copy; validate the resulting graph.
4. Rebuild and validate the geometry, and compute the response measurements.
5. Write the candidate revision durably, then atomically publish HEAD.
6. Only committed revisions are visible to readers. Return the new revision.

Schema, graph, or modeling failure leaves HEAD and previous revisions untouched.
Readers continue to see the old revision while a writer builds. Explicit queries
and exports name a committed revision, rather than implicitly racing HEAD.

Writers of one document are serialized by a per-document lock taken under a
shared workspace lock (`flock` on POSIX, `LockFileEx` on Windows). Service
mutations and artifact publication wait a bounded 5 seconds for a transient
holder, so finished geometry work is not discarded; then, like other lock
users that fail fast, contention returns `workspace_busy`. Files are published
by writing a temporary file in the destination directory, fsyncing it
(`F_FULLFSYNC` on macOS), renaming it, and fsyncing that directory. Revisions at
or below HEAD are immutable. A snapshot
beyond HEAD is an interrupted, uncommitted candidate and can be replaced by a
subsequent writer. Temporary files may remain after abrupt process termination;
they are never treated as documents.

A storage error after the final rename can have an uncertain commit outcome.
The caller must read HEAD before retrying. M0 does not claim exactly-once
execution, request deduplication, rollback after an OS durability error, or
network-filesystem locking semantics. The worker/job milestone must add request
IDs, recoverable job state, and a deduplication policy before automatic retries.

Saving does not depend on geometry being byte-identical across rebuilds. Geometry
comparisons use explicit tolerances; artifact hashes identify actual bytes.
Kernel upgrades require replay/regression tests and an explicit document
migration policy. M0 rejects records with another kernel version.

## 5. Selections and editing intent

Distinguish three identities:

| Identity | Lifetime and meaning |
|---|---|
| Feature ID | Persistent design intent, e.g. `mounting_holes` |
| Resolved geometry selection | A face/edge from a specific evaluated revision/build |
| Persistent design reference | A rule describing intended geometry across rebuilds |

Never persist a bare face or edge enumeration number as a design reference.
M0 has no selectable topology tool and supports `edges: "all"` for fillets only.

M1 should start with explicit feature-scoped selectors and geometric predicates:
surface/curve kind, direction, position, and expected cardinality. Store the rule
and the source feature, not an accidental matching index. Geometry picks from
the viewer must identify document, revision, and evaluation. Until resolved
topology is persisted, rebuilds may require a new evaluation identity even for
the same revision.

Use OCCT generated/modified/deleted history as evidence where available, combined
with feature provenance and geometric checks. Do not promise that history solves
all topology naming. If an edit changes a unique selection into zero or multiple
matches, return a structured diagnostic with candidates and preserve HEAD.
The agent must be able to query and repair the reference.

## 6. Agent interface

Current tools and exact schemas are specified in `PROTOCOL.md`; running `tools`
or MCP `tools/list` returns the machine-readable input contracts.

- Create/read a document.
- Apply multiple semantic edits as a single transaction.
- Query facts about a named revision.
- Export a named revision.

Planned capabilities: preview an uncommitted candidate; enumerate/select
topology; compare revisions; import components; queue/poll/cancel jobs; present
the model in a viewer. These are not advertised until implemented.

Results should be compact structured facts with explicit units, revision, and
feature context. Failures distinguish invalid input, graph references, kernel
failure, invalid geometry, revision conflicts, missing data, and storage issues.
Do not guess a replacement radius or silently omit a feature after a failure.

## 7. Geometry and resource rules

Exact B-rep solids remain authoritative. Validate operation completion, shape
validity, solid presence, and positive finite volume. An OCCT validity result is
not a manufacturability certificate. Later DFM checks must be separately named.

The original baseline supports one output shape, possibly containing multiple
valid solids. M6 adds a named assembly output containing distinct positioned
part instances, with up to 64 direct parts and 63 mates per assembly and 256 declared
parts across a document. Nested composition permits eight occurrence levels,
1,024 expanded leaves per assembly and 4,096 summed across assembly definitions.
Interference does not fuse parts; measurements sum
their volumes, including overlap. Exchanges preserve positioned solid geometry;
the saved document retains editable part and mate semantics. Drawing explosion
offsets only affect the selected projection, never the model or mate solution.
Internal primitives remain axis-aligned in their source coordinates.

M0 bounds documents to 1 MiB of JSON, nesting depth 64, 128 parameters, 256
features, and 256 operations per edit. The current STEP importer excludes embedded
STEP source strings from internal document/worker JSON byte budgets; imports
have no fixed file-size cap and remain subject to worker memory/time budgets.
Captured SVG/DXF source and font bytes have separate bounded asset budgets and
raw hashes; their geometry remains native and their source is never executed.
Transport input envelopes and ordinary metadata retain their byte limits. Input numbers are finite and within
plus/minus 1,000,000; positive primitive dimensions/radii are at least 0.00001 mm.
These limits do not bound kernel execution time. M2 must add process deadlines,
memory limits, queue admission, cancellation, and recovery before untrusted
remote use. Cancellation may terminate a worker but must not interrupt a
partially published transaction in the transport process.

Geometry and drawing projections use a disposable workspace cache. Keys include
the complete canonical model intent (including embedded imports), native source,
toolchain/configuration and OCCT SDK binary fingerprints, kernel version and
cache format version. Source fingerprints cover geometry/projection tolerances
and algorithms. Exact B-rep snapshots preserve every feature and its provenance;
restoration validates shapes and topology counts inside a bounded worker. Meshes
and evaluation identities remain fresh, with evaluation-local selection IDs.

Projection keys are per view: each includes that view's orientation, section
plane/hatch settings, exploded offsets and balloon source anchors, plus the
hidden-line choice, so editing one view never repeats the others' hidden-line
work. View names, revision/document identity,
sheet layout, dimensions, explicit tolerances, notes and output formats are
rendered afresh, as are BOM tables and balloon label positions. Projections
retain their original aggregate geometry budgets (entities, points and balloon
anchors are totalled over the assembled drawing).
Only successful workers stage reusable results; coordinators check cancellation
before atomically publishing cache entries. Cache writes never publish HEAD or
artifacts. Checksummed entries are optional, bounded and evicted oldest-first;
missing, damaged or unavailable entries rebuild. Losing the cache must not lose
editable documents. Dependency-level feature caches now complement whole-model
and per-view caches; `DEPENDENCY_CACHE.md` defines fingerprints, invalidation and
bounded publication. Cached shapes do not establish persistent topology naming.

## 8. Distribution and viewer direction

Target native bundles for macOS arm64/x64, Linux x64/arm64, and Windows x64.
The first supported development platform is macOS/POSIX; CI is initially Linux.
Compilers and CMake are development dependencies, not end-user requirements.
Native libraries and required OCCT resources must ship with relative lookup
paths. Run a clean-machine test with no SDK/Homebrew paths before calling a
bundle self-contained. License notices, source/relinking obligations as
applicable, signing, and notarization belong to the release gate.

Keep the viewer in browser JavaScript. Reusing `text-to-cad` renderers is a
candidate, not an assumed drop-in dependency: its client protocol expects
document/cache/topology services currently implemented in Python. Prefer an
isolated adapter or a small initial viewer over copying its whole runtime.
The new service must not depend on the sibling checkout being present.

## 9. Explicit initial exclusions

No full sketch constraint solver, loft/sweep/revolve, feature patterns,
assemblies/joints, DXF/PDF drawings, remote multi-tenant hosting, arbitrary
scripts, Python API compatibility, or automatic conversion of old scripts.
These may be added according to user workflows after the editable core works.
The current protocol supersedes these historical exclusions for M3 modeling,
M5 drawings, M6 assemblies and the modeling/motion extension. General
sketch/assembly constraint solving, dynamics and remote hosting remain outside
the implemented scope. Nested composition, child Motion-panel controls and
composed robot graphs and revision-pinned cross-document components are
implemented. Component snapshots and explicit updates are in `COMPONENTS.md`.

## 10. Completion evidence

Each milestone in `ROADMAP.md` has testable acceptance criteria. Maintain actual
evidence in `HANDOFF.md`: commands, platform, kernel version, successes,
failures, and unverified claims. New agents should be able to pick one next
task, run a reproducible baseline, and demonstrate a concrete increment.


STEP workflow extension (2026-10-10): native read-only inspection diagnoses
invalid per-solid input before admission. Explicit source-hash-qualified subsets
can become opaque imported features. Periodic meshing retries preserve the exact
source; complete closed print meshes drive STL and 3MF. Native 3MF and optional
rectangular plate packing are derived exports with qualified manifests, never
assembly edits or slicing approval. See PROTOCOL.md for implemented bounds.
