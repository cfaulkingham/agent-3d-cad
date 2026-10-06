# Dependency versions and provenance

## OpenCascade

- Product: Open CASCADE Technology (OCCT).
- Pinned release: **8.0.1**, upstream tag `V8_0_1`.
- Latest stable verification: **2026-10-06**.
- Official release: <https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8_0_1>.
- Official announcement (2026-07-31): <https://occt3d.com/news/open-cascade-technology-8-0-1-is-now-available/>.
- Release listing: <https://github.com/Open-Cascade-SAS/OCCT/releases>.
- Source URL: <https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.tar.gz>.
- SHA-256: `0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a`.

The main CMake project requires exactly 8.0.1 and the native translation unit
checks `OCC_VERSION_HEX` at compile time. The optional dependency build uses
unmodified upstream source and checksum verification; no copied kernel patch or
Python compatibility layer is used. Exceptions must remain enabled in Release
(`BUILD_RELEASE_DISABLE_EXCEPTIONS=OFF`). The dependency recipe builds shared
libraries and the required toolkit closure, with GUI integrations disabled.

The local initial build uses an existing OCCT 8.0.1 SDK. This is a development
configuration, not a runtime link to the Rust API and not proof of portable
packaging. See `HANDOFF.md` for exact validation scope.

## JSON and platform dependencies

- nlohmann JSON **3.12.0**, header-only, fetched by CMake when not installed.
- Source: <https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz>.
- SHA-256: `4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187`.
- Integration documentation: <https://json.nlohmann.me/integration/cmake/>.
- The dependency recipe builds shared FreeType **2.14.3** from the official
  [release archive](https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz).
  SHA-256: `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`.
  The [upstream checksum listing](https://sourceforge.net/projects/freetype/files/freetype2/2.14.3/)
  and a local archive download were verified on 2026-10-06. Optional external PNG,
  HarfBuzz, Brotli, zlib, and bzip2 integration are disabled in this recipe.
  FreeType's built-in gzip and other embedded components retain the exact
  supplemental notice-bearing files referenced by its upstream LICENSE.TXT.
  `AGENTCAD_SYSTEM_FREETYPE=ON` remains a developer override; it requires auditing
  its actual transitive dependencies and notices before distribution.
- Standard C++ runtime and native POSIX/Windows file and locking APIs are used.
- Linux OCCT's FreeType integration also links Fontconfig and Expat. The Ubuntu
  build uses the distribution development packages and bundles the runtime
  libraries with their exact package notices and version record. The pinned
  FreeType build remains the application's FreeType implementation. Bundles
  retain library and notice hashes in `provenance.json`.

No MCP SDK is embedded in M0. The limited stdio adapter targets the published
2025-11-25 protocol and is separately tested. It does not claim complete MCP
extension support or that 2025-11-25 is the latest protocol release.

## Upgrading

1. Check the official OCCT release list and exclude previews/development tags.
2. Record the release URL/date and independently verify the source archive hash.
3. Update the main CMake requirement, native compile check, dependency recipe,
   CI cache identity, tests, and these docs together.
4. Build with exceptions enabled; run analytic geometry, STEP read-back,
   persistence, failed-edit, process, and selection-regression tests.
5. Define how existing documents with the previous `kernel_version` are migrated.
   Never silently rebuild a saved document with a new kernel and call it the same
   geometry revision.
6. Revalidate packaged native dependencies and clean-machine operation.

## Licensing and release status

Original project code has no selected license yet. The owner should choose one
before public publication; no license was inferred from sibling projects.
No third-party source or library is checked into the project. Dependency downloads
stay under ignored build/cache directories.

OCCT carries its upstream LGPL 2.1 terms with the Open CASCADE exception;
nlohmann JSON carries its upstream MIT terms. Consult the exact release license
files and retain notices when distributing them. FreeType and its transitive
libraries have their own terms. Packaging must include the applicable notices
and satisfy obligations for the actual linkage/distribution choice. Portable
preview installs include the exact OCCT, JSON and FreeType license texts and
hash their files in `provenance.json`. Linux compiler runtime notices are
supplied from the actual build toolchain. See `DISTRIBUTION.md` for bundle
construction and remaining public-release requirements; preview packaging
does not decide the original-code license or assert completed legal review.
