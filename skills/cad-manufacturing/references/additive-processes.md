# Process-specific additive advice

Sources below were checked on 2026-10-11. Reopen the relevant current primary
guide for the chosen machine/material before giving a numeric recommendation.
Record source URL, access date, machine/material/profile and units beside each
limit. Do not transfer a threshold between processes or resin/powder families.

**SLA / resin.** Inspect cavities, drain access, support contact on functional
surfaces, unsupported spans and cleaning/cure access. Suggest drain/vent features
or a split part only when they preserve its function. The
[Formlabs Form 4 guide](https://formlabs.com/uk/white-papers/form-4-design-guide/)
connects minimum features and drainage to a specified printer/resin test setup.
Its thresholds are not universal resin limits. Native exact bounds/topology and
selected `cad_measure` distances can support geometry statements; resin cupping,
trapped-volume connectivity, supports, washing and cure adequacy are unmeasured.

**SLS polymer.** Inspect powder escape paths, cleaning access to holes/lattices,
thin sections, nesting spacing and moving clearances. A captured joint needs
depowdering access as well as a gap. The
[Formlabs Fuse SLS guide](https://formlabs.com/white-papers/fuse-series-sls-design-guide/)
distinguishes separate-part spacing, assembly clearance and integrated assembly
clearance. Apply the category matching the actual build. Powder support does not
prove that a cavity can be cleaned or that a thin part will resist distortion.

**MJF.** Establish material, dimensional fits, cleaning access, wall transitions,
thermal mass and intended finish. Use the relevant chapters of the
[HP MJF handbook](https://enable.hp.com/us-en-3d-mjf-handbook-download?asset=5_6&elq=00000000000000000000000000000000&elqTrackId=c320a74a465d45bf904cea2848ccc63f&elqaid=2120&elqat=2)
for accuracy, cleaning and mechanical requirements. Native nominal B-rep gaps
can expose a design interference; they do not predict as-printed fit. Where
accuracy matters, propose a fit coupon or qualified machining allowance with
an explicit basis, and retain the chosen nominal geometry.

**Metal powder-bed fusion.** Establish alloy/process and the supplier's build,
heat treatment, depowdering, support-removal and machining plan. The
[EOS process chain](https://www.eos.info/metal-solutions/metal-ecosystem)
includes these distinct stages. Minimize inaccessible supports and retain
machining access/allowances for critical surfaces. Do not extrapolate an FDM
45-degree rule: [EOS discusses thermal and process dependence](https://www.eos.info/content/blog/2021/metal-3d-printing-without-supports).
Residual stress, porosity, fatigue, thermal distortion, powder release and
support design are not simulated by the native review.

**What to run.** For these four advice modes, use `cad_query` summary/topology,
`cad_measure` on current qualified geometry and the viewer for scoped evidence.
`cad_measure` requires the actual query evaluation ID and supports exact
face/edge/part distances; it is not a general wall-thickness solver. Do not send
`sla`, `sls`, `mjf` or `pbf` to `cad_fabrication_review` or manufacturing process
enums. If a deliberately requested FDM geometric proxy is useful, retain its
`fdm` identity and state its scope; ignore its process-specific result for
approval. In `cad_manufacture`, use process `unspecified` and explicit notes
identifying the real additive process and outstanding criteria.

Report the real outcome as measured geometry, externally sourced guidance,
proposed changes and unmeasured process questions. A native tool completing
successfully cannot fill those gaps.
