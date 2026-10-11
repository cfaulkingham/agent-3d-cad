# Native visual display and portable GLB

The bundled viewport supports Solid, Render, X-ray, Hidden line and Wireframe.
The `render` view setting is data, independent of camera pose, source geometry,
selection identities, exact measurements and committed history. Native screenshot
and export contracts are described in the protocol. End users need no JavaScript
installation, rendering package or network service.

The closed display object accepts these optional fields. Omitted fields use the
chosen mode's defaults; the default mode is `solid`.

| Field | Values |
| --- | --- |
| `mode` | `solid`, `render`, `xray`, `hidden_line`, `wireframe` |
| `projection` | `orthographic`, `perspective` |
| `lens_mm` | 18–120, default 50 |
| `surfaces` | Optional `opacity` in [0,1], `finish`: `matte`, `satin`, `glossy`, `metal` |
| `lighting` | Optional `enabled` boolean, `exposure` in [0.1,4], `rotation_deg` in [-360,360], `fill` in [0,1] |
| `background` | Optional RGB `color`, each channel in [0,1] |
| `floor` | Optional `enabled`, RGB `color`, `opacity` in [0,1], `finish`: `matte`, `glossy` |
| `grid`, `axes`, `edges` | Boolean guide visibility |

Render uses perspective, a floor, studio lighting, and disabled guides by default.
X-ray uses 22% surfaces with all edges. Hidden line writes surface depth without
painting fills, exposing visible edges. Wireframe paints all edges without
surface depth. Solid paints authored surfaces and visible edges. Picking retains
source-qualified, occlusion-aware native geometry in every mode.

The Display panel and guide buttons persist these settings in native view state.
Named view presets capture rendering and materials alongside camera, clipping,
visibility and explosion. Older state or presets with no `render` field inherit
Solid defaults. Render edits never modify model HEAD; changing the reviewed
document resets source-scoped rendering. Capture waits for confirmed persistence
and rejects a snapshot superseded by a later display change.

Native appearance keeps its required `default_color` and `parts` fields. Optional
`material:{opacity,finish}` on the default appearance or a part override supplies
visual alpha and finish. This describes appearance, rather than manufacturing
material, density or physical properties. Global surface opacity multiplies the
authored material opacity. Explicit leaf finish takes precedence over default
finish, then the display finish. Matte, satin, glossy and metal map respectively
to roughness 0.9, 0.5, 0.15 and 0.24; metal sets metalness to one.

The viewport uses bounded WebGL geometry, an explicit key and fill light,
roughness-dependent highlights, projected floor shadows and a matte/glossy floor.
These are interactive presentation effects, rather than path tracing or optical
material certification. Transparent occurrence groups are blended in depth
order; intersecting transparent surfaces can retain approximate ordering.
WebGL2 supplies the perspective-correct floor/grid backdrop. A WebGL1 fallback
still renders model shading, perspective, alpha, edges and projected shadows;
its backdrop retains the theme clear color. Actual host GPU readiness is
separate from unit and headless browser evidence.

## Shared projection and shading

`CadRenderer.math` exports `renderSettings`, `surfaceStyle`, `studioLight`,
`studioGeometry`, `project`, `screenRay`, `materialData` and ordinary camera
helpers. These functions use normalized geometry: model bounds center is zero,
and the greatest source extent is one. Camera pose remains yaw, pitch, zoom and
pan. Internal perspective parameters are display settings, never geometry.

The viewport eye is `-D * basis[2]`, with
`D=max(depthExtent+0.25,max(3,depthExtent+1)*lens_mm/50)`.
Projected horizontal/vertical coordinates multiply ordinary orthographic framing
by `D/(D+cameraDepth)`. The corresponding source ray uses direction
`basis[2]+u/D*basis[0]+v/D*basis[1]`, so its parameter is camera depth plus D.
Perspective edge-depth interpolation uses reciprocal projective weight, keeping
edge picks at the displayed source point. Camera fitting evaluates perspective
bounds corners; guides, notes, exact section caps and artifact overlays share
the displayed projection.

The world key vector `[-0.45,-0.55,1]` rotates about world Z by the requested
lighting rotation. It is transformed to view coordinates for shading. Diffuse
intensity is `0.35 + fill*max(dot(n,normalize([.55,.3,-.4])),0) +
0.65*max(dot(n,key),0)`. Specular exponent is `max(2,128*(1-roughness))`, with
strength `0.08+0.5*metalness`. Exposure scales the lit result. Floor shadows
project the actual visible/clipped triangles along this world key, then apply
bounded screen offsets for softened edges.

## External authored appearance

GLB review displays source base color, RGBA vertex colors, unit normals, metallic
and roughness factors, emissive factors, OPAQUE/MASK/BLEND alpha and double-sided
flags. Embedded base-color PNG/JPEG textures use TEXCOORD_0 with source sampler
filters and wrapping. The renderer decodes only bounded embedded image data;
URLs, caller shaders, scripts and callbacks do not execute. Capture waits for
accepted images to finish loading. Other PBR texture channels fail explicitly
in the native reader; they are not silently discarded.

Native pose evaluation can replace transient artifact geometry for animation,
skinning, morph targets and robot joints while preserving the source review hash,
group identity and vertex/UV ordering. Source files and editable documents are
independent of transient review poses.

DXF TEXT/MTEXT paint actual text in a sibling canvas overlay, with source height,
rotation, width, alignment and attachment. Solid HATCH paints even-odd loops and
keeps holes. The captured PNG composites these same overlays. Fonts use the
browser's sans-serif font; SHX/style-specific font reproduction is outside this
review implementation. Aligned/Fit text must be rejected by the native reader
until both-point shaping is implemented.

## Native GLB encoding

`src/glb.cpp` accepts JSON mesh/topology/summary data from native geometry workers.
It writes a self-contained GLB 2 file with one mesh/node per leaf occurrence,
local mesh coordinates, source occurrence names and source identity metadata.
Actual glTF child nodes preserve nested assembly occurrence groups from the
native summary tree. Intermediate groups have identity transforms; descendant
leaf transforms remain world-relative, preserving placement and leaf animation.
Native world-mm coordinates are inverse-transformed into leaf-local metres;
a root rotation maps CAD Z-up into standard glTF Y-up. Triangle normals and
PBR color/finish/alpha accompany the same tessellation. Exact STEP remains an
independent artifact; GLB does not recover editable source or persistent faces.

Animation receives complete native rigid pose samples. Every sample covers all
source leaves, time starts at zero and increases, and source geometry remains
unchanged. Translation/quaternion channels animate each occurrence; quaternion
sign continuity avoids unwanted hemisphere jumps. LINEAR glTF playback
interpolates sampled translations and quaternion rotations. The coordinator
qualifies saved source definitions and controls sampling; GLB does not execute
sequence code. Clip geometry cannot be represented as a rigid glTF channel and
must fail explicitly unless separately baked into supported geometry.

Encoding permits at most 1,024 leaves, 8,192 hierarchy occurrences, the native display mesh's 200,000 vertices
and triangles, 2,048 animation samples and a 64 MiB file. Writes target a staged
path; native publication remains the coordinator's responsibility. Cancellation,
invalid rigid transforms, ownership mismatches and incomplete pose coverage
produce explicit failures.

The binary conventions follow the [Khronos glTF 2 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Native animated GIF encoding

`src/gif.cpp` streams native RGBA frames through `export_gif` without retaining
the entire sequence. N+1 time boundaries describe N frames; the callback renders
once at each starting boundary, and the final boundary sets the last hold.
Rounding absolute boundaries to centiseconds keeps total duration consistent.
The self-contained GIF89a has full-canvas frames, variable-width LZW compression,
explicit delays, and an optional infinite NETSCAPE loop extension.

A fixed 256-color RGB 3-3-2 palette keeps colors stable across frames. Alpha
composites onto white. This is lossy palette quantization and can produce banding;
PNG remains the full-color static output. The encoder permits 2–256 frames,
4,096 pixels per axis, 4 million pixels per frame, 128 million total frame pixels,
up to 3,600 seconds, 1–65,535 centiseconds per hold, and a 64 MiB file. Failed or
cancelled encodes clean their temporary output before publishing a staged file.
Saved source qualification and frame evaluation belong to the native coordinator.
MP4 encoding remains unsupported by this helper.

Encoding follows the [GIF89a specification](https://www.w3.org/Graphics/GIF/spec-gif89a.txt).
The Graphics Interchange Format is copyright property of CompuServe Incorporated;
GIF is a service mark property of CompuServe Incorporated.

## Verification

The `glb` native suite constructs real native solids and independently decodes
chunks/accessors, checking metre bounds, normals, source metadata, material alpha,
inverse leaf transforms, actual nested hierarchy and animation quaternion continuity. The
`gif` suite independently parses palettes, delays, loop semantics and LZW, reconstructs
source pixels across dictionary-width changes, and checks byte/work/cancellation bounds.
An additional Pillow developer check decoded both saved GIF fixtures with the expected
dimensions, frame counts, delays, colors and loop status. Renderer unit tests
check source-space perspective picking, edge depth, finish/alpha ownership and
clipped shadow geometry.

`tests/browser_render_smoke.cjs` is an optional real Chromium/WebGL2 pixel test.
Developer use requires Playwright, a supplied Chrome path and optionally an
evidence directory. It uses a disposable profile with local test data:

```sh
node tests/browser_render_smoke.cjs /path/to/chrome .local/evidence/render-glb
```

It verifies shader compilation/drawing without GPU errors, all five distinct
modes, perspective, floor, projected shadows, studio lighting, authored alpha,
embedded texture decode, actual text pixels and an unfilled hatch hole. It saves
PNG evidence. Software Chromium rendering does not establish either target
host's install/reload journey or hardware GPU acceptance.

## Native CPU images

`cad_render` rasterizes a committed native revision or a hash-verified external
review without an open viewer. The native worker uses display triangles and
source curves; it preserves exact geometry and committed revisions. Camera,
projection, clipping, exploded leaf placement, appearance, hidden leaves and
render settings use the same definitions as the viewport. External pose frames
keep the captured source bounds as their camera frame, so animation and joint
motion remain visible under a fixed saved camera. The current posed bounds are
validated independently and determine floor placement and camera depth bounds.

The portable C++ renderer produces opaque-background RGBA pixels and compressed
PNG. It implements opaque depth, depth-ordered transparent triangles,
perspective-correct vertex colors and texture coordinates, transformed normals,
authored double-sided flags, emissive/base-color/roughness/metalness factors,
MASK/OPAQUE/BLEND alpha, visible source edges and all five display modes.
Triangle meshes without source curves expose triangulation edges in Wireframe.
The floor, grid, axes, studio shading and softened projected shadows are bounded
presentation effects. Transparent intersections and projected shadows retain
approximate ordering and softness; this is a visual review renderer.

Embedded PNG base-color textures preserve source nearest/linear filters,
mipmapped minification and clamp/repeat/mirrored-repeat wrapping. Aggregate
decoded texture levels are limited to 16 million pixels. Embedded JPEG textures
currently fail with `unsupported_format` in CPU output, while the viewport can
render them. Unsupported image sources are never replaced with an untextured
surface.

Drawing overlays paint even-odd solid hatch loops, including empty inner loops,
and actual text with source height, direction, width factor, alignment,
attachment and multiline placement. CPU text uses an original deterministic
bitmap font for printable ASCII and newline. Other glyphs and Aligned/Fit text
fail explicitly; browser sans-serif and style-specific font metrics are separate
rendering implementations. Overlay text and hatch fills follow the viewport's
annotation layering over the geometry image.

Output dimensions are 16–2048 pixels per axis, at most four million pixels; PNG
output is limited to 16 MiB. Inline image content is limited to 2 MiB. Geometry
retains the display limits of 200,000 vertices, triangles and curve points,
10,000 external groups/curves, and 16,384 text characters. Raster work is limited
to 200 million pixel operations, with cancellation checks during geometry,
texture, pixel and overlay processing. Excess work, unsupported data and
cancellation fail before publishing an image. Extreme displayed poses beyond a
normalized camera extent of one billion fail explicitly.

The native `render` suite tests actual pixels against an independent source-ray
camera oracle for orthographic and perspective visibility and textured UVs. It
checks measured box silhouettes, clipping, hidden/exploded placement, fixed-frame
artifact motion, modes, authored materials/normals/alpha, texture samplers,
floor/shadow pixels, ASCII text and hatch holes. Invalid indices, unsupported
textures/glyphs, pathological hatch work and cancellation preserve existing
outputs. `render_rgba` supplies the same pixels directly to native animation
encoding; `render_png` adds exact byte length and SHA-256 metadata.

`tests/browser_visual_mcp.cjs` is optional end-to-end developer evidence using the
real native stdio service, shipped CadBridge/CadLiveState/CadRenderer/CadCapture
assets, and a local MCP Apps protocol fixture in real Chromium:

```sh
node tests/browser_visual_mcp.cjs /path/to/agent-3d-cad /path/to/chrome .local/evidence/visual-workflow/browser-native
```

It verifies actual browser PNG transfer through `cad_screenshot`, native image
content/file/SHA identity, frozen source model qualification, persisted camera
and rendering, responsive MCP ping during capture, hidden-view failure, native
GLB texture review/decode, and rejection of a changed display during capture.
The local protocol fixture also explicitly rejects the first PNG chunk and the
first hidden-view failure notification with `workspace_busy` before native
forwarding; the shipped capture coordinator must retry identical bytes and
source-qualified offset, or the same qualified failure, before the actual service
accepts them. These deterministic pre-write rejections are identified in evidence.
An independent PNG CRC/zlib/filter decoder compares `cad_render` output against
the actual browser capture for a known unlit top-camera box. The accepted raster
tolerance is two pixels at silhouette boundaries and three 8-bit channel levels
inside the shape; this fixture does not claim full shaded image equivalence.
Evidence records source headers, context, actual PNGs and comparison metrics.
These local developer fixtures do not establish target-host installation or
plugin reload acceptance.

`tests/visual_mcp_sdk_smoke.py` adds a bounded official MCP Python SDK 2.3.0
session using the developer requirements in `tests/mcp_sdk_requirements.txt`:

```sh
python tests/visual_mcp_sdk_smoke.py /path/to/agent-3d-cad .local/evidence/visual-workflow/visual-sdk.json
```

It independently validates discovered tool inputs and outputs, typed native PNG
and saved-sequence GIF image blocks, GLB and image resource bytes, MIME types and
hashes, GIF frame delays and loop semantics, and concurrent screenshot
requests/app fixture PNG chunks/ping. JSON text must retain metadata without
duplicating image base64. The screenshot uses a known image fixture; the browser
test above supplies actual renderer pixel evidence. The optional evidence path
also saves the delivered PNGs, GIF and animated GLB outside its temporary native
workspace. Python, Playwright and the MCP SDK remain developer dependencies.
