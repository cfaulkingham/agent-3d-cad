# Native preview distributions

The install tree is a relocatable native application: `bin/agent-3d-cad`
(`.exe` on Windows), its native runtime libraries, and
`share/agent-3d-cad/{occt,examples,notices,provenance.json}`. The core bundle includes the embedded MCP App and offline HTML viewer. The
desktop archive adds a Tauri application window under `desktop/`. Creating or editing a model
requires no Python, Rust, Node, CMake, compiler, or sibling checkout.

The integrated viewer is also compiled into the executable as the MCP App
resource `ui://agent-3d-cad/viewer.html`, with MIME type
`text/html;profile=mcp-app`. CMake combines the reviewed `web/` HTML, CSS and
JavaScript assets; no JavaScript bundler, external web server, CDN or runtime
asset directory is needed. Resource metadata declares no external connection,
resource or frame domains. `cad_open` associates its result with this resource;
`cad_show` updates the existing view. Rendering the interactive app requires an
MCP Apps host. CLI clients can use the standalone Tauri window; both adapters
share the same native service, workspace, view IDs and revision-qualified picks.

## Building an archive

Build the pinned shared dependencies with `cmake/dependencies` first. That
recipe downloads checksum-verified OCCT 8.0.1 and FreeType 2.14.3, disables
optional external FreeType integrations, installs the required OCCT resources and exact
upstream notices, and leaves exceptions enabled. It applies the reproducible
`agentcad-hlr-midpoint-v2` optimization; bundles include its patch, explanation
and five complete modified files alongside the upstream notices. SDKs built
before this change must be rebuilt for the optimization and regression suite.
`AGENTCAD_SYSTEM_FREETYPE=ON`
is a developer override whose additional dependency notices require review.
`AGENTCAD_OCCT_ARCHIVE` and `AGENTCAD_FREETYPE_ARCHIVE` accept offline archives
without bypassing hash checks. Windows uses a native MSVC x64 developer prompt.

```sh
cmake -S cmake/dependencies -B build-deps -DAGENTCAD_DEPS_PREFIX="$PWD/.deps/native" -DAGENTCAD_JOBS=4
cmake --build build-deps --config Release --parallel 4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOpenCASCADE_DIR="$PWD/.deps/native/lib/cmake/opencascade" -DCMAKE_PREFIX_PATH="$PWD/.deps/native"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix "$PWD/bundle"
cmake -DBUILD_DIR="$PWD/build" -DTEST_DIR="$PWD/build/bundle-check" -P tests/bundle_smoke.cmake
cd build
cpack -C Release
```

On Linux the build host needs `patchelf` and Fontconfig development headers
(`libfontconfig1-dev` on Ubuntu); OCCT uses Fontconfig and Expat for its FreeType
integration even with X11 disabled. Bundles include these libraries and the C++ runtime and
use `$ORIGIN` lookup paths, while glibc and its loader remain platform
prerequisites. Supply the exact GCC runtime license/exception texts and the
Fontconfig and Expat copyright files with `AGENTCAD_EXTRA_NOTICE_FILES`.
The CI and Linux validation Dockerfile also include the installed Ubuntu package
versions in that notice tree; bundle provenance hashes every native library and
notice file. Match the build distribution to
the oldest supported target glibc. The CI Linux baseline is Ubuntu 24.04.
On macOS the installer rewrites dependency IDs and links relative to the
executable, audits them with `otool`, and ad-hoc signs changed binaries. This
does not substitute for release signing/notarization. A bundle inherits the
deployment target of its SDK; rebuilding the service alone cannot lower it.
CI selects Xcode 26.3, which provides the required C++20 `std::jthread` library,
and sets the dependencies and service deployment target to macOS 15.0. A local
build without a deployment target inherits its SDK defaults; the actual minimum
macOS version is recorded in bundle provenance.
On Windows required DLLs and the permitted MSVC redistributable runtime are
copied beside the executable. User model inputs never run these build tools.

`AGENTCAD_PORTABLE_BUNDLE=OFF` permits executable-only developer installs.
Portable install fails on missing dependencies, conflicts, missing upstream
notices or resources. For an externally installed JSON SDK, set
`AGENTCAD_JSON_LICENSE` to its exact 3.12.0 `LICENSE.MIT`. For an external OCCT
SDK, `AGENTCAD_NOTICE_DIRECTORY` must contain complete `occt/` and `freetype/`
notice directories, including FreeType's embedded component notices, with the
layout produced by the pinned recipe.

## Tauri desktop and Claude extension builds

Tauri reuses the `web/` renderer inside the operating system webview. It does not
ship Chromium or Electron. A small Rust shell launches the bundled C++ service
through stdio and exposes bounded viewer operations through private IPC. Geometry
and exports stay in the existing C++ service and isolated workers. Qt Quick was
considered: reusing this renderer on Linux with Qt WebView would require Qt
WebEngine; a native Qt Quick 3D renderer would require separate rendering and
picking implementations. Tauri preserves the existing tested selection path.
See [Tauri webviews](https://v2.tauri.app/reference/webview-versions/) and
[Qt WebView](https://doc.qt.io/qt-6/qtwebview-index.html).

Developers additionally need Rust (CI pins 1.95.0), the platform GUI SDK and,
on Linux, `libwebkit2gtk-4.1-dev libgtk-3-dev librsvg2-dev`. No npm build is needed:
`desktop/build.rs` combines the same reviewed HTML/CSS/JS assets as CMake.

```sh
CAD_SERVICE_EXE="$PWD/bundle/bin/agent-3d-cad" cargo test --locked --manifest-path desktop/Cargo.toml
cargo build --locked --release --manifest-path desktop/Cargo.toml
python3 packaging/package-desktop.py bundle build/packages desktop/target/release/agent-cad-viewer
# macOS example; Windows uses the matching .exe and Windows-x64.mcpb names.
python3 packaging/make-mcpb.py bundle build/packages/agent-3d-cad-0.1.0-preview.1-Darwin-arm64.mcpb
```

Run packaging with Python 3.11+ from the same machine/toolchain that built the
binaries. The desktop helper verifies native provenance, preserves the full
resolved Rust dependency source archives and their notices, and hashes the
complete resulting tree. Output directories must be new. Its macOS app is
ad-hoc signed only. The Claude `.mcpb` uses a binary stdio server and persistent
workspace setting; it includes the core service and embedded viewer. The package
layout follows the [MCPB 0.3 manifest specification](https://github.com/modelcontextprotocol/mcpb/blob/main/MANIFEST.md).

For local GUI development, build without `--release` and launch the binary with
`CAD_SERVICE_EXE` pointing to the native service. `CAD_VIEWER_CONFIG_DIR` can
isolate recent-workspace settings for tests. Release binaries ignore both
overrides and locate the service relative to themselves.

## Version and release process

`VERSION` is the release identity (`X.Y.Z-preview.N` for previews). CMake uses it
for the executable, MCP server and archive names. Keep `desktop/Cargo.toml`,
its lockfile and `tauri.conf.json` aligned; the desktop build rejects drift.
Use tags such as `v0.1.0-preview.1`, matching `VERSION` exactly.

Every push runs the five native platform lanes. Each lane tests the service,
installer/client configuration, relocated bundle and Tauri stdio/export adapter,
then builds a core archive and desktop archive. macOS arm64/x64 and Windows x64
also produce Claude `.mcpb` packages. A `v*` tag creates a **draft prerelease** only
after all lanes pass. The release job rejects a missing or extra asset before
creating `SHA256SUMS` for the ten archives, three extensions and two installers.
Review the draft, platform evidence and signing before publishing. Stable release
publication is not automated by the preview workflow. No tag or release is
created merely by editing these files or running local builds.

## Install, use and uninstall

Use the release’s `install.sh VERSION` or `install.ps1 -Version VERSION`, or
extract the platform archive into any writable application directory. Keep
`bin`, `lib` (where present), `share`, and `desktop` together. The download
installers require only OS utilities and verify archive SHA-256 before extraction.
They never overwrite an existing version or change client settings. The desktop
runtime needs macOS WKWebView, Windows WebView2, or Linux GTK 3/WebKitGTK 4.1. Run the binary directly or
configure its absolute path as the MCP command with `serve --workspace PATH`.
The workspace can be anywhere writable and is separate from the application.
No global server registration, shell initialization, or administrator access
is necessary. Resource paths are discovered relative to the executable.

For a developer-assisted local installation, the repository includes an optional
CMake installer. Given an already-extracted trusted bundle, it verifies every
file against `provenance.json`, rejects unlisted files and escaping links, and
copies the bundle into a fresh version directory without replacing older installs:

```sh
cmake -DBUNDLE_DIR="/path/to/extracted-bundle" -DINSTALL_DIR="/path/to/applications" -DRESULT_FILE="/path/to/install-result.json" -P packaging/install-local.cmake
```

The result JSON records the absolute executable path and verified manifest hash.
`EXPECTED_PROVENANCE_SHA256` optionally pins a manifest hash obtained from a
trusted source. These hashes check integrity, not publisher authenticity. The
helper requires CMake only when installing this way; the installed native service
does not. The helper changes no MCP settings or shell configuration.

To register that native executable in Codex, run the following with the absolute
paths from the installation result and your chosen persistent model workspace:

```sh
codex mcp add agent-3d-cad -- /absolute/install/bin/agent-3d-cad serve --workspace /absolute/model-workspace
codex mcp get agent-3d-cad
```

This registration is a separate, explicit user action. It does not install a
Python or Node CAD runtime. Hosts supporting MCP Apps can open the bundled live
viewer through `cad_open`; `cad_show` reuses that view after model edits.

To upgrade, stop the service, extract the new bundle separately, and update
the MCP executable path. Preserve the workspace. A different kernel version
requires the explicit migration policy described in `DEPENDENCIES.md`.
To uninstall, stop the service, remove its MCP configuration entry and delete
the application directory. Delete workspaces separately only when their saved
documents and exports are no longer needed.

## Evidence and release boundary

For an isolated Linux build on a host with Docker, first put the three pinned
archives from `DEPENDENCIES.md` in `.deps/` as `OCCT-V8_0_1.tar.gz`,
`freetype-2.14.3.tar.xz`, and `json-3.12.0.tar.gz`, then run:

```sh
cmake -P packaging/stage-linux-validation.cmake
docker build --target dependencies --tag agentcad-linux-deps:local build-linux-container/context
docker build --target build --tag agentcad-linux-build:local build-linux-container/context
docker build --target runtime --tag agentcad-linux-runtime:local build-linux-container/context
```

This stages only project sources and the three archives, verifies every archive
hash inside the container, builds native dependencies without the host SDK, and
runs native CTest, JSON-schema, official MCP SDK and viewer-renderer developer checks. The final
stage contains a base OS and the native bundle, and asserts that language
runtimes and build tools are absent while testing rollback, saved viewing,
asynchronous jobs, reopened revisions, manufacturing exports and MCP framing.
The default architecture is the Docker daemon's native architecture; specify a
platform only when intentionally testing another supported architecture. These
commands create local images and do not publish them.

`tests/bundle_smoke.cmake` installs and relocates the tree into a path with
spaces, clears PATH and SDK loader/resource overrides, and repeats separate
process create/edit/reopen/query/STEP/STL exports and MCP JSON framing checks.
The Linux CI container repeats the plate workflow in a base OS image and
asserts there is no language runtime or build tool available. Native CI lanes
cover macOS arm64/x64, Linux x64/arm64 and Windows x64. A workflow definition
alone is not evidence that those runners passed; see `HANDOFF.md` for results.

The local relocation test is not a fresh-machine test: macOS system frameworks
are still present. Public release still requires actual platform-run evidence,
applicable source/relinking obligations, and release signing/notarization
where appropriate. No archive is uploaded or published by local packaging.
The original code is MIT-licensed; `LICENSE` and `NOTICE` ship in each bundle's
notices directory.
CI uploads preview archives and test evidence as workflow artifacts with a
14-day retention. Version tags additionally prepare the draft release described
above. The first public prerelease is
[`v0.1.0-preview.1`](https://github.com/cfaulkingham/agent-3d-cad/releases/tag/v0.1.0-preview.1);
it remains unsigned/unnotarized, with actual host-validation limits recorded in
`HANDOFF.md`. `tests/notice_consistency.cmake`
(CTest `notices`) fails if the shipped third-party notice, the OCCT patch
description and the installer's recorded modification identifier disagree, or
if the notice stops stating the pinned OCCT source hash and relink instructions.
