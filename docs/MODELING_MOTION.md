# Modeling flexibility and moving mechanisms

Authorized objective, 2026-10-07: add the modeling flexibility and moving
mechanisms identified in the comparison with text-to-cad. This is native product
work; it does not replace the separate two-host release gates in RELEASE_1_0.md.

## Completion contract

The authorized requirements below are implemented locally. The evidence audit
after the requirements records the validation scope; it does not certify the
separate stable-release installation journey.

- Exact, editable chamfers with the existing geometric edge-selection contract.
- Parametric profiles composed of lines, circular arcs, Bezier curves and
  interpolating splines, including explicit interior boundaries. No sampled
  polygon substitution for curved geometry. Curved 3D sweep paths use the same
  curve vocabulary. Preserve existing profile and polyline-path documents.
- Procedural reuse through native structured patterns and parameter expressions;
  extend repetition to circular patterns without model-supplied programs.
- Rigid, revolute, slider and cylindrical mates with explicit datum frames,
  bounded degrees of freedom, validated limits, and deterministic forward
  kinematics through a single-parent acyclic assembly graph.
- Linear coupled motion, named poses, and atomic pose/joint edits. Changes must
  flow through create/read/apply/preview/jobs, preserve historical revisions,
  and produce matching query, STEP/STL and drawing geometry.
- Embedded-viewer mechanism controls, live pose preview, named-pose selection,
  explicit saving, and revision-qualified agent context. No stale pick can name
  geometry at a different pose. Camera and part visibility remain coherent.
- Robot-description handoff (URDF, SRDF and SDF) for supported articulated
  assemblies, with exact correspondence between exported frames/joints and
  native evaluated poses. Explicitly reject unsupported semantics; do not
  invent physical properties or silently discard couplings.
- Documented schemas, examples, agent guidance, regression coverage, packaged
  native checks and an observed embedded-view interaction for the new controls.

General sketch constraints, closed-loop/inverse dynamics solvers, arbitrary
Python compatibility and full CAD API parity are not implied. Geometry remains
inside BuiltModel/kernel.cpp and serial bounded native workers. All committed
edits validate before publication, and failures preserve HEAD. Existing client
and distribution acceptance gates remain independent of this feature work.

## Verification

Use analytic volumes, curve types and positions, independent STEP readback,
roundtrip/cache equality within tolerances, and negative cases (ambiguous
selection, invalid curves, impossible geometry, joint-limit violations,
coupling conflicts/cycles). Mechanism tests must include rotated parent frames,
multi-joint chains, coupled motion, saved/reopened poses, cancellation and
failed-edit atomicity. Viewer tests must cover actual transforms/picks, pending
preview races, reset/save, and external revisions. Exported robot descriptions
must be parsed independently and their forward kinematics compared numerically.

## Work state

- Initial inspection: clean checkout; chamfers, mixed curve profiles, curved
  sweep paths, articulated mates and robot exports are absent. Existing native
  assembly, preview, jobs, drawings, cache and live-view infrastructure can be
  extended. The configured build-app-protocol SDK is pinned OCCT 8.0.1.
- Exact curve profiles/paths, chamfers and circular patterns are implemented with
  analytic geometry, curve-type, STEP readback, edit/history and rejection tests.
- Revolute/slider/cylindrical mates, explicit limits, linear acyclic couplings,
  named poses and atomic edits are implemented and tested with rotated frames.
- Embedded native mesh previews, joint controls, named-pose selection, reset and
  explicit save (including preset creation) are implemented. Tests cover stale
  evaluations, pending preview/reset, failed input, external revisions, history,
  camera and visibility. Actual browser/native-service interactions have verified
  posing, coupled coordinates, reset, saving revision 2 and reloading the preset.
- Native `cad_robot_export` now produces paired URDF/SRDF or SDF 1.12, source STL
  meshes, an explicit coordinate/physical-data ledger and portable hash manifest.
  Independent XML/FK checks compare saved, named and sampled poses with native
  geometry, including offset/rotated frames, mixed-unit couplings and cylindrical
  decomposition. SDF requires supplied inertials; no physical values are invented.

## Completion evidence audit

| Requirement | Authoritative implementation and executed evidence |
|---|---|
| Exact curves, holes, chamfers, circular repetition | `model.cpp` schemas/validation and `kernel.cpp` OCCT construction; `modeling_curves` checks analytic volumes, curve types, exact STEP readback, invalid wires/holes, selective edge edits and failed-edit rollback |
| Existing documents, parameterized edits and history | `modeling`, `transactions`, `service`, `modeling_curves` and schema checks exercise old models, new examples, preview/apply/reopen and immutable old revisions |
| Articulated forest, limits, couplings and poses | `motion.cpp`, native assembly FK and `motion` tests cover revolute/slider/cylindrical coordinates, transformed datums, limit violations, coupling conflicts/cycles and complete named poses |
| Pose geometry across outputs | `motion` checks native query transforms, independently imported STEP volume/parts, binary STL vertex bounds and drawing dimensions for a committed pose |
| Atomic motion and cancellation | `motion` cancels a running pose+geometry transaction and checks complete old intent and query results; `jobs` exercises process kill, time/memory budgets and publication protection |
| Embedded motion workflow and context | `live.cpp`, `web/state.js`, `web/app.js`; `motion`, `live_mcp_flow`, `live_ui` and `app_protocol` cover pending-reset races, stale/draft picks, saves, external revisions, camera and hidden parts |
| Observed interaction | Browser test host running the native stdio service: previewed folded/extended poses, edited a driver and observed coupled values, reset, saved named poses at revisions 2 and 3, then reloaded revision 3 with visibility intact; rendered geometry observed. Workspace under `build/motion-demo/browser-workspace` |
| Robot handoff | `robot.cpp`, `BuiltModel::robot_frames`, worker/service export; `robot_export_test.py` independently parses relocated XML/meshes and compares FK against native geometry, validates physical data, SRDF states, limits, hashes and historical/asynchronous exports |
| Agent/API/package usability | Published input/output schemas and bundled skill; `schema_conformance.py`, official SDK, skill validator and `bundle-check` pass. Empty-PATH relocated bundle creates curved parts, poses a mechanism, and writes URDF/SRDF/SDF with native code |

Executed local counts and logs are in HANDOFF. The installed external CAD Viewer
could not start because its package lacks its documented `agent:start` script.
`gz`, ROS `check_urdf`, MoveIt and a dynamics simulator are unavailable here;
no consumer/simulation validation is claimed. Robot handoff is verified by the
independent XML/FK checks above. Current platform evidence is macOS arm64;
the added CI checks have not yet been run on the other supported platforms.
No release, installation, signing or publication claim follows from this audit.
