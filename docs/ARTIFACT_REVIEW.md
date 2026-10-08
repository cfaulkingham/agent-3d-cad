# Native external-artifact review

Service/MCP/CLI admission, durable jobs and the read-only live viewer for
original external files are merged into the main repository. The combined macOS
arm64 build passes native, schema, SDK and relocated-bundle checks. Artifact review creates
no editable document or native revision. The isolated browser evidence below
concerns a loopback test host; it does not establish ChatGPT/Claude host
installation or Windows/Linux release acceptance.

## Public contract

`artifact_review_definitions()` returns four closed JSON Schema definitions:
`artifact_review_arguments`, `artifact_review_source`,
`artifact_review_summary`, and `artifact_review_result`.

`validate_artifact_review_arguments(arguments)` validates admission.
`review_external_artifact(workspace, arguments, native_build)` captures bounded
original bytes, parses the selected original format, and atomically publishes
a portable read-only review package under `workspace/artifact_reviews`.
`verify_external_artifact(package, expected_review_sha256)` rechecks captured
bytes and the exact file ledger, validates the full persisted source/geometry
contract and reparses captured original bytes. The persisted geometry, summary,
metadata and limitations must reproduce that parser's output. STEP reparsing
uses the existing bounded serial kernel worker; numeric comparisons permit
`1e-8 + 1e-7 * max(abs(a),abs(b))`. Other format outputs compare exactly.
Verification qualifies the stated review representation, not authenticity,
source authorship, physical readiness or recovered editable history. Malformed
stored fields fail as `artifact_invalid`; source/review changes or a fabricated
parsed result fail as `artifact_mismatch`, without leaking JSON exceptions.

Review arguments require `action: "review"`, an absolute regular non-symlink
`path`, lowercase `expected_sha256`, explicit `format`, and explicit `units`.
Source parent paths must also be free of symlinks; resolve macOS `/tmp` and
`/var` aliases to their `/private` paths before admission.

Optional `references` are at most 64 `{uri,path,expected_sha256,units}` records
for URDF/SDF STL meshes. A URI must be a plain relative portable path without
parent traversal, schemes, escapes, or query/fragment suffixes. Its absolute
path must match that URI beneath the original source's parent. Every reference
is explicitly captured and hashed; the module never fetches network resources
or automatically reads unlisted files.

Optional `native_source: {document_id,revision,feature_id}` remains
`caller_declared`. The exact historical document record and named feature are
resolved under `DocumentLock`; `source_record_sha256` binds the canonical
record. The record is rechecked under that source lock before package publication
and again before viewer publication. HEAD may advance because the historical
revision remains explicit. The module does not compare the native model's
geometry with the external file or recover history. The real native document
remains the authoritative editable source. Portable byte/parser verification
alone cannot resolve a native document from a different workspace; showing an
associated review requires its historical record in the active workspace.

`review.json` contains source identities, canonical millimeter geometry, summary,
format metadata, limitations, and explicit read-only flags. Its mesh groups
and curves use `artifact-N`/`curve-N` labels with source-format reference paths.
These labels belong to the exact **review SHA-256**, which also binds native
build and parser output. They are never original CAD face/edge selectors.
STEP display curves are also derived labels. No editable history is recovered.

Packages retain `original.<extension>`, referenced snapshots in `references/`,
`review.json`, and `manifest.json`. The manifest records each relative file's
size and SHA-256 plus original/review identities. Relocation and deletion of
the original inputs do not invalidate the captured package. Unexpected files,
symlinks, missing files, changed bytes, and review/manifest changes during
verification fail. Publication uses an owner-only temporary directory and an
atomic rename under a workspace publication lock, or the appropriate native
source publication lock for an associated review; failures clean the stage.
Identical review bytes reuse an existing verified package.

## Supported original formats and honest limits

| Format | Parsed review | Units and unsupported cases |
| --- | --- | --- |
| STEP/STP | Existing isolated native `import_step` and `BuiltModel` view worker; exact summary and tessellated display | `file`; embedded bytes ≤512 KiB. No OCCT pointers outside `kernel.cpp`. Opaque import is not recovered feature history. Current fixture is a native round trip, not cross-vendor qualification. |
| STL | Binary or ASCII triangles, including finite coordinates and degenerate-triangle rejection | Explicit mm/cm/m/in/ft/um; no intrinsic units, watertightness or exact-solid claim. Binary attribute/color payloads are unsupported. |
| GLB | GLB 2 JSON/BIN, selected scene, indexed/unindexed triangle primitives, float positions, byte/ushort/uint indices, hierarchy and matrix/TRS transforms | Intrinsic meters, Y up. Mirror winding is handled. External buffers/images, extensions, skins, animation, morph targets, sparse accessors and non-triangle modes are unsupported. Materials/textures/cameras/lights and inactive scenes are not rendered. |
| 3MF | OPC content types/relationships, stored/deflated ZIP, core build meshes, nested component placements and unit conversion | `file`; core units/default millimeters. Only the default core XML namespace is supported. Required extensions, extension attributes, ZIP64, encryption, multi-disk ZIP and noncontained relationships are unsupported. Instances remain separate meshes; overlapping parts are not unioned or manufacturing certified. |
| DXF | ASCII LINE, straight LWPOLYLINE, sampled ARC/CIRCLE with retained analytic metadata | Explicit units must agree with supported `$INSUNITS`. Widths, bulges, thickness, non-default OCS, INSERT/blocks, splines and text are unsupported in the entity stream. Administrative tables/blocks are not rendered. No cutting certification. |
| URDF | URDF 1.0 connected link/joint tree, origin transforms, box/cylinder/sphere geometry and explicit contained STL references; visual and collision roles retained | Intrinsic meters, zero joint coordinates. Floating/planar joints, mimic coordinates, newer quaternion/capsule syntax, macros/plugins and simulator extensions are unsupported. Dynamics/controllers and physical adequacy are not validated. |
| SDF | One model, declared model/link/visual/collision poses, tree graph and primitive or explicit STL geometry | Intrinsic meters. Worlds, nested models/includes/plugins, named `relative_to` frames, quaternion/degree poses and unsupported joints require another backend. Joint axes/physics are not fully validated; this is not simulation acceptance. |
| SRDF | Groups/subgroups/chains, state values, end effectors, virtual/passive joints and disabled-collision declarations | Semantic graph only; no standalone geometry. Group references/cycles are checked. Link/joint names remain unqualified without the intended URDF; planning/collision-exemption feasibility is unknown. |

The readers are deliberately bounded subsets. They do not claim full glTF,
OPC/3MF, XML, DXF, URDF, SDF or SRDF conformance. Unsupported geometry-bearing
features fail explicitly instead of substituting unrelated shapes. Inputs are
never executed as JavaScript, Python, shell commands, plugins or code.

## Bounds and parser dependencies

Each source/reference file is at most 64 MiB; all explicit references total at
most 128 MiB. XML inputs are at most 16 MiB with 100,000 lexical/tree nodes,
depth 64 and attribute length 4096. Geometry has at most 200,000 vertices,
triangles and curve points, 10,000 groups/curves/placements, and coordinates
within ±1e9 mm. Robot trees have at most 1024 links/joints. Review JSON is at
most 64 MiB; captured package ledger bytes are at most 256 MiB; the separately
bounded manifest is at most 1 MiB. Cancellation checkpoints occur throughout
parsing, hashing and publication. Durable admission/history/failure/cancel
state is provided by the shared Service/job coordinator. `cad_artifact` supports
`review` and `verify`; both have typed durable job arguments/results without
requiring a synthetic `document_id`. Cancellation and parser failures retain
the coordinator's normal terminal states and history.

ZIP has at most 256 entries, 32 MiB expansion per entry and 128 MiB total.
It checks local/central headers and optional data descriptors, actual decoded
length, independent CRC-32, directory/special-file modes, overlap, duplicates
including portable case collisions, path safety and expansion limits. An
independent bounded DEFLATE structure reader checks stream consumption and
expanded byte count before decompression. No archive member is extracted to
the filesystem. Empty streams, stored/fixed/dynamic blocks and streamed data
descriptors are included in the independent corpus.

Decompression uses the existing SDK's public
[`FT_Gzip_Uncompress`](https://freetype.org/freetype2/docs/reference/ft2-gzip.html#ft_gzip_uncompress)
API from pinned FreeType 2.14.3. Raw ZIP DEFLATE receives a fixed generated gzip
header plus declared CRC/size trailer. FreeType receives a bounded output and
a custom 1 MiB allocator budget; actual output size and independent CRC are
checked. A build without gzip support returns explicit `unsupported_feature`
on `FT_Err_Unimplemented_Feature`. macOS arm64 linkage was tested directly with
the existing `libfreetype.6.20.6.dylib`; Windows/Linux export/linkage and bundle
qualification remain pending. No additional zlib dependency is introduced.

XML uses the two-file native
[TinyXML2 release 11.0.0](https://github.com/leethomason/tinyxml2/releases).
The official tag archive is pinned to SHA-256
`5556deb5081fb246ee92afae73efd943c889cef0cafea92b0b82422d6a18f289`.
Upstream zlib-license source headers and `LICENSE.txt` are preserved.
`dependency-provenance.json` records exact source/API URLs. Before TinyXML2
parses, inputs receive UTF-8/control/NUL checks, lexical/entity bounds and
strict Unicode numeric-reference validation, including prevention of the
[reported release numeric-reference overflow](https://github.com/leethomason/tinyxml2/issues/1087).
DOCTYPE/entity declarations are prohibited; no external entity or DTD resolver
is invoked. XML files are parsed from captured memory, never loaded via an XML
path resolver.

Format decisions follow primary
[3MF core specification](https://github.com/3MFConsortium/spec_core/blob/master/3MF%20Core%20Specification.md),
[glTF 2 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html),
[Autodesk DXF groups](https://help.autodesk.com/cloudhelp/2018/ENU/AutoCAD-DXF/files/GUID-89CB823D-614D-4D1E-8204-568EC72DF869.htm),
[Autodesk LWPOLYLINE](https://help.autodesk.com/cloudhelp/2018/ENU/AutoCAD-DXF/files/GUID-748FC305-F3F2-4F74-825A-61F04D757A50.htm),
[ROS urdfdom](https://github.com/ros/urdfdom), and
[SDFormat specification](https://sdformat.org/spec/1.12/model/), and
[MoveIt URDF/SRDF documentation](https://moveit.picknik.ai/main/doc/examples/urdf_srdf/urdf_srdf_tutorial.html).
3MF's 12-number row-vector transform is converted explicitly to the common
column-vector matrix; GLTF matrices are read as column-major, with T*R*S order.

## Live viewer contract

`cad_artifact_show({review_path,expected_sha256,view_id?})` verifies/reparses the
portable package before publishing a frozen display. `cad_viewer` sync and
mesh transfer retain their existing bounded chunk protocol. Artifact sync and
payload use `read_only: true`, `document_id/revision/feature_id: null`, and
`artifact: {review_sha256,source,summary}`. Sync/context explicitly return
`annotations: []`, `sequences: []` and `playback: null`; native review state is
cleared when the view changes to an artifact. Their evaluation token belongs to the
viewer only; it is never registered as an editable native evaluation.

Renderer admission is separate from the exact native mesh/topology validator.
External geometry uses `artifact_geometry`, without native face/edge topology.
Picks are exactly `{review_sha256,kind:"mesh_group"|"curve",entity_id}`. Server
context publication checks those labels against the frozen display and its
review hash. Picks expire when that review is superseded. Context/copy/capture
retain source hashes, declared units, read-only status and explicit limitations.
Displayed geometry is the frozen captured review; sync does not continuously
reread the original external paths or portable package.

The viewer shows original format/units, canonical display dimensions, source
and review identities, mesh/curve counts, source-format references, robot graph
or semantic metadata and parser limitations. DXF displays sampled curves;
SRDF displays semantic data with no invented shape/bounds. Clipping is visual
and remains uncapped. Native exact measurements/sections, presets, edits,
source export, notes and playback are unavailable in an artifact session.
`cad_show` of a real native document restores the ordinary editable workflow.

## Remaining release validation

The main implementation includes direct pinned FreeType linkage, vendored
TinyXML2, dependency provenance/notices and bundle checks. The artifact branch
preserves the native printer, appearance, notes and playback workflows; external
sessions do not inherit native notes or sequence state. The combined main build
passes all 54 CTest suites, 929 independent general schema checks, 197 actual
artifact schema checks, 3,317 general and 498 artifact official MCP SDK 2.3.0
checks, and relocated empty-PATH bundle smoke. Its 28-tool compact discovery
catalog is 427,829 bytes under the unchanged 430,080-byte limit. Exact build and
log identities are in HANDOFF. Windows/Linux public API linkage and release bundles, independent
external STEP fixtures, and executed ChatGPT/Claude host acceptance also remain
pending. Fixture scripts and the loopback browser host are developer-only; no
product runtime invokes Python, Node, a compiler, artifact code or a shell.

## Executed integration evidence

The macOS arm64 source-copy build passed the **115 native module checks** with
reparse verification and a resolved historical source association. Actual
stdio MCP + shipped controller/renderer checks cover every original format,
source/reference hashes, typed durable success/failure, observed-running cancellation, 1ms timeout,
same-request success/failure replay, unchanged prior package and source HEAD, stale picks, malformed
persisted fields, forged geometry, native source mismatch and native-view
restoration. Independent Draft 2020-12 validation covers actual responses and
rejects false source identities/execution fields. The isolated final build
passed **115 native checks**, **53 real MCP/controller checks**, **201 independent
actual MCP schema checks**, and **459 official MCP SDK 2.3.0 checks**. The SDK
fixture covers all eight original formats and automatic/legacy process
lifecycles, durable replay and native-view restoration. Seven focused CTest
suites passed in **6.48 seconds**. These counts qualify the isolated artifact
integration; they do not establish final combined-main acceptance.

The official SDK omits TMPDIR from its child environment. The service resolves
only its own operating-system temporary root before creating a transient
parser workspace, allowing macOS /tmp aliases without relaxing caller source
or package symlink restrictions. Developer validation commands:

```sh
python tests/artifact_mcp_sdk_smoke.py <native-executable> [evidence.json]
python tests/artifact_schema_tests.py <artifact-mcp-evidence.json>
```

An actual in-app browser loaded the served app through a loopback MCP test host.
It rendered URDF sphere/cylinder geometry, picked `artifact-1`, displayed the
three-field review-local reference, persisted visual clipping with uncapped
surfaces, and copied a read-only request. Native exact-section, measurement,
preset and export controls were hidden. The host logged native RPCs and a
pinned executable hash. Save PNG requested a download, but the browser provider
did not expose a completed download event; that is not download acceptance.
One injected `MutationObserver` console error was observed; the shipped app
and test host contain no MutationObserver and subsequent native/viewer
interactions succeeded. A final pinned-binary browser pass also rendered
curve-only DXF with Curves selected and Meshes disabled, then retargeted to
SRDF with semantic-only guidance and original group/state/collision metadata.
No third-party chat message or hardware action occurred.

The isolated macOS bundle installed and relocated, and its TinyXML2 source,
license and provenance checks passed. The inherited appearance-base smoke then
failed on an existing viewer-poll `workspace_busy` response. A full combined-main
bundle pass remains required; isolated installation is not complete bundle
acceptance.

## Executed standalone evidence

On macOS arm64, the final native fixture suite passed **115 checks** using
the existing `build-app-protocol/libcad_core.a` and existing native executable
for STEP workers. Two independent ZIP corpus runs passed **181 cases each**;
the second substitutes a gzip-unavailable test stub solely to verify explicit
unsupported reporting. Independent Draft 2020-12 JSON Schema validation passed
**32 checks** over real arguments/results and rejected incorrect units, start/
execution/host fields and false read-only claims. Compilation used C++20 with
`-Wall -Wextra -Wpedantic` and emitted no warnings. Logs and exact argv are in
`tests-final.log`, `schema-tests.log`, `cad_zip_corpus_tests.log`,
`cad_zip_no_gzip_tests.log`, `build-final.log`, and `build-command.json`.
Python fixture/oracle scripts are development checks; no runtime path invokes
them. Those standalone logs qualify the frozen module version. Durable jobs and
actual viewer behavior are separately covered by the integration evidence;
platform and product-host/bundle acceptance remain unestablished.
