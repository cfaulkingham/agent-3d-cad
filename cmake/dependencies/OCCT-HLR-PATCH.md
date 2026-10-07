# OCCT 8.0.1 HLR midpoint optimization

Modification: `agentcad-hlr-midpoint-v1`, 2026-10-06.

The upstream source archive and OCCT version remain pinned to 8.0.1.
`PatchOcctHlr.cmake` checks the exact upstream hash of each of five files, applies
its changes, and verifies the resulting hashes. Reapplication is safe; unknown
source versions are rejected. Apply with
`cmake -DOCCT_SOURCE_DIR=<unpacked source> -P PatchOcctHlr.cmake`.

Two `HLRBRep_Hider` midpoint checks formerly counted every intersection and
then discarded the count. They now ask whether any qualified occluder exists at
the same point. For BSpline supports, the first attempt uses a denser starting
grid based on knot count times degree, with the original count as a floor and
512 samples per axis as a ceiling. The existing exact solver, depth tolerances,
periodic UV adjustment and trimmed-face classification still qualify each root.
If this attempt cannot confirm occlusion, it repeats the original-grid query.

All callers that use intersection counts retain the original solver and grid.
Grid changes rebuild the face's cached polyhedron; changing faces invalidates it.
Existing public method symbols and object layouts are preserved. Surface supports,
source geometry, topology, projection curves and editable documents are unchanged.
The grid only supplies initial guesses to the exact solver; it is not an output
mesh or a substitute for exact solids. This optimization retains the numerical
limits of OCCT HLR and does not establish globally complete root recovery.

The installed notices include this explanation, the reproducible patch script
and all five complete modified source/header files with upstream copyright and
license notices. Portable bundles retain and hash them in their provenance
inventory. OCCT's LGPL 2.1 and Open CASCADE exception continue to apply. This does
not select a license for the project's original code.

The SDK's `agentcad-hlr-midpoint-v1.json` records hashes of its installed TKHLR
libraries. Portable packaging checks the selected runtime library against that
manifest before recording the modification and SDK hash in provenance.

The service cache identity includes SDK binaries. Switching SDKs invalidates
disposable geometry and projection caches; saved documents remain OCCT 8.0.1.
