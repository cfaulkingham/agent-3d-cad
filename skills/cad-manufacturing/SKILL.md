---
name: cad-manufacturing
description: Review and improve native Agent CAD parts for FDM, CNC, sheet cutting, molding, SLA, SLS, MJF, or metal additive manufacturing, including measured build-orientation comparisons.
---

# Native manufacturing review

Turn the requested process and functional requirements into measured findings,
specific design changes, and a source-qualified handoff. Native CAD needs no
Python, Node, compiler, or slicer. Discover the connected tool schemas before
calling them; use [native-cad](../native-cad/SKILL.md) for modeling operations.

1. Read the saved document with `cad_read`. Record document/revision/output and
   selected leaf source, actual process, material, machine/stock, functional
   surfaces, fits and load direction. Use supplied limits; fetch the current
   machine/material supplier guide for missing process guidance. Unknown inputs
   remain unknown; a useful geometric review can proceed without all of them.
2. Use `cad_query` summary/topology for exact geometry. Call
   `cad_fabrication_review` with a supported explicit profile and the committed
   revision. The supported process enum is **fdm, cnc, sheet_laser, molding**.
   Read [native checks and redesign](references/native-checks.md) for mappings,
   call structure, coverage and remediation.
3. For an FDM orientation recommendation, make the actual six review calls from
   [orientation comparison](references/orientation.md), or a user-requested
   subset. Report each candidate's measured envelope, non-bed overhang area,
   sampled wall evidence, failed checks and unresolved criteria. A camera view
   or visual guess is not an orientation measurement.
4. For SLA, SLS, MJF or metal additive, read
   [process guidance](references/additive-processes.md). These are advice modes,
   not additional native process enum values. Never report an FDM overhang test
   as an SLA/PBF process result. Cite current primary sources for recommendations
   and distinguish measured geometry from manual/unknown process criteria.
5. If redesign is requested, change the relevant parameters/features with
   `cad_apply(expected_revision: ...)`, preserving functional constraints.
   Preview geometric consequences as needed, then rerun the same measured checks
   on the committed revision. Compare source-qualified before/after findings;
   do not hide a failed or unsupported check by changing its limit.
6. Deliver findings by part and severity, their witness/coverage and source,
   the actual changes, and remaining process decisions. For a requested handoff,
   call `cad_manufacture` with explicit per-part assumptions and the same
   `fabrication_review` options. Its manifest preserves failed/unknown checks.
   Saved review, geometry and package links should use the tool's artifact and
   download resources, not invented paths.

Use bounded `cad_job` submissions for substantial reviews/packages; retrieve the
actual terminal result and preserve report paths/hashes. Report success means
the computation completed, not that the part is approved for production.

For supplier-specific sheet preflight use
[cad-sendcutsend](../cad-sendcutsend/SKILL.md); for catalog components use
[cad-source-parts](../cad-source-parts/SKILL.md); for slicing and Studio/Connect
handoff use [cad-print-handoff](../cad-print-handoff/SKILL.md).
