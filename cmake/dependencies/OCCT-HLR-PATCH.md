# OCCT 8.0.1 HLR midpoint optimization

Modification: `agentcad-hlr-midpoint-v2`, 2026-10-07.

The upstream source archive and OCCT version remain pinned to 8.0.1.
`PatchOcctHlr.cmake` checks the exact upstream hash of each of five files, applies
its changes, and verifies the resulting hashes. It also upgrades the exact known
v1 sources. Reapplication is safe; unknown source versions are rejected. Apply with
`cmake -DOCCT_SOURCE_DIR=<unpacked source> -P PatchOcctHlr.cmake`.

Two `HLRBRep_Hider` midpoint checks formerly counted every intersection and
then discarded the count. They now ask whether any qualified occluder exists at
the same point. For BSpline supports, the first attempt uses a denser starting
grid based on knot count times degree, with the original count as a floor and
512 samples per axis as a ceiling. The existing exact solver, depth tolerances,
periodic UV adjustment and trimmed-face classification still qualify each root.
If this attempt cannot confirm occlusion, it repeats the original-grid query.

The v2 patch streams exact roots for these checks and for `Compare`, whose
original classification already stopped at its first qualified root. It retains
the same sorted starting points, parameter bounds, duplicate suppression, exact
solver, point construction and tolerances. Each candidate must pass the existing
depth, periodic-UV and trimmed-face checks before root solving can stop. Rejecting
a candidate continues to later roots; rejecting every candidate exhausts the
query. `Compare` keeps its original grid. No new guesses or visibility tests are
introduced. The additive `PerformUntil` method returns only an existence result,
not a complete intersection inventory. Existing methods retain their contracts.

All callers that use intersection counts retain the original solver and grid.
The two sampling grids are retained for the current loaded face, so alternating
between dense checks and original-grid fallback/counting does not rebuild the
same surface samples. Each query selects the exact requested dimensions; no
polyhedra are copied or swapped. The cache owns at most two polyhedra and changing
faces or destroying the intersector releases both. No process-wide cache is added.
Existing public method symbols and object layouts are preserved. The installed
header marks the additive method with `AGENTCAD_OCCT_HLR_STREAMING`; native
regressions compare streaming and complete inventories, rejected and later
accepted candidates, grid/face changes, and exact source-surface residuals.
Surface supports,
source geometry, topology, projection curves and editable documents are unchanged.
The grid only supplies initial guesses to the exact solver; it is not an output
mesh or a substitute for exact solids. This optimization retains the numerical
limits of OCCT HLR and does not establish globally complete root recovery.

The installed notices include this explanation, the reproducible patch script
and all five complete modified source/header files with upstream copyright and
license notices. Portable bundles retain and hash them in their provenance
inventory. OCCT's LGPL 2.1 and Open CASCADE exception continue to apply. This does
not select a license for the project's original code.

The SDK's `agentcad-hlr-midpoint-v2.json` records hashes of its installed TKHLR
libraries. Portable packaging checks the selected runtime library against that
manifest before recording the modification and SDK hash in provenance.

The service cache identity includes SDK binaries. Switching SDKs invalidates
disposable geometry and projection caches; saved documents remain OCCT 8.0.1.
