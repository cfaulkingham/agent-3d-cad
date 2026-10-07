# Third-party components in native preview bundles

Open CASCADE Technology 8.0.1 is distributed under its upstream LGPL 2.1
license and Open CASCADE exception. Exact license texts are in `occt/`.
Upstream source: https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1
The pinned build includes the `agentcad-hlr-midpoint-v1` modification. Its
reproducible patch, scope and complete modified sources are retained as
`occt/PatchOcctHlr.cmake`, `occt/OCCT-HLR-PATCH.md` and `occt/modified/`.

nlohmann JSON 3.12.0 is distributed under its upstream MIT license, retained
in `nlohmann-json/`. Source: https://github.com/nlohmann/json/tree/v3.12.0

FreeType 2.14.3 is included by the pinned build recipe. Its copyright notices,
FreeType License, and alternative GPLv2 terms are retained in `freetype/`.
The exact upstream notice-bearing files for its embedded BDF/PCF, hash, zlib,
and HarfBuzz-derived components are retained under `freetype/embedded/`.
Source: https://sourceforge.net/projects/freetype/files/freetype2/2.14.3/

The pinned recipe uses shared OCCT and FreeType libraries. Library replacement
and relinking must remain possible according to the applicable upstream terms.
Use the recorded source archives and `cmake/dependencies` build recipe to build
matching replacements. Additional native dependencies in nonstandard builds
must carry their own applicable notices. On Linux, libstdc++ and libgcc may be
included under GPLv3 with the GCC Runtime Library Exception; package builders
must supply their toolchain's exact license and exception texts with
`AGENTCAD_EXTRA_NOTICE_FILES`. Windows builds include the toolchain's permitted
C/C++ redistributable runtime files.

`provenance.json` records platform, compiler, pinned input identities, and hashes
of the actual installed native files/resources/notices. The original project
license remains an owner decision. These build artifacts are development
previews, with no assertion of public release authorization, signing,
notarization, or completed legal review.
