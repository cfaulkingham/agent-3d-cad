# Third-party components in native preview bundles

Open CASCADE Technology 8.0.1 is distributed under its upstream LGPL 2.1
license and Open CASCADE exception. Exact license texts are in `occt/`.
Upstream source: https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1
The pinned build includes the `agentcad-hlr-midpoint-v2` modification to five
files of OCCT's `TKHLR` hidden-line toolkit. Its reproducible patch, scope and
complete modified sources are retained as `occt/PatchOcctHlr.cmake`,
`occt/OCCT-HLR-PATCH.md` and `occt/modified/`. The modification identifier in
`occt/agentcad-hlr-midpoint-v2.json` is the same one named here; a repository
test fails if this notice, the patch description and the installer disagree.

AgentCAD's additions inside those five OCCT files are derivative modifications
of LGPL 2.1 code and are distributed under the license terms stated in the files
they modify. Their upstream copyright and license notices, including any Open
CASCADE exception they state, are retained in each modified file. This statement concerns only
the modified OCCT files; it does not select a license for the project's other,
original code.

Corresponding source for the modified libraries: the pinned upstream archive
`https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.tar.gz`
(SHA-256 `0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a`)
plus `occt/PatchOcctHlr.cmake` reproduces them exactly; the complete modified
files are also included under `occt/modified/`. To replace the shared TKHLR
library, build OCCT 8.0.1 with the `cmake/dependencies` recipe (or an
equivalent exception-enabled build), apply the patch with
`cmake -DOCCT_SOURCE_DIR=<unpacked source> -P PatchOcctHlr.cmake`, and install
the resulting shared libraries in the bundle's native-library directory
(`lib/` beside `bin/` on macOS and Linux, where the executable's run path points;
`bin/` next to the executable on Windows). Verify the build against
the recorded SDK and notice manifest before redistributing it.

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
of the actual installed native files/resources/notices. The original project code
is MIT-licensed (`LICENSE`, shipped beside this file). These build artifacts are development
previews, with no assertion of public release authorization, signing,
notarization, or completed legal review.
