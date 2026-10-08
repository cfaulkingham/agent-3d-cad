# Native dependency caching

The editable document and its revisions remain authoritative. Disposable native
workspace caches accelerate evaluation without changing tool contracts, requiring
an end-user runtime, or introducing a component-library dependency.

## What rebuilds

A feature fingerprint includes its own geometry intent, referenced parameter
values, upstream fingerprints, and the native build/SDK/kernel identity. Each
worker evaluates the complete ordered graph, restoring unchanged features and
building changed features. Complete model snapshots provide a fast path for
unchanged graphs. Missing, corrupt, busy, unwritable or oversized entries are
optional misses; deleting the cache loses no editable work.

For a sketch → extrusion → chamfer → hole → moving module → parent assembly:

| Edit | Native features rebuilt |
|---|---|
| Hole radius | Hole, moving module, parent assembly |
| Extrusion thickness | Extrusion, chamfer, hole, moving module, parent assembly |
| Sketch width | Sketch and every dependent feature |
| Child joint angle | Moving module and parent assembly |
| Parent occurrence placement | Parent assembly |
| Unrelated spare part | Spare part; output projections remain reusable |
| Unused parameter, BOM or saved-pose metadata | Shapes reused; current metadata validated and reported |
| Component pin with unchanged materialized geometry | Shapes reused; current pin reported |

All declared features are validated, including branches outside the output.
Assembly named poses are validated even when no shape changes. Bound component
parameters participate wherever materialized features reference them; source
provenance alone cannot freeze a consumer's metadata on an older pin.

Drawing projection keys follow the output feature's dependency closure plus
orientation, section/hatch/explode/balloon definitions and hidden-line choice.
Current labels, dimensions, tolerances, layout and file formats are rendered from
those projections. Each query still receives fresh evaluation identity; saved
revision checks and selection rules remain unchanged.

## Restoration and limits

Private snapshot format 2 serializes exact B-reps without triangulations, topology
counts and evaluation-local provenance. Assembly compounds retain independently
copied children, including coincident occurrences. Restoration validates geometry,
recomputes hierarchy and motion from current intent, checks occurrence transforms,
and rebuilds face/edge ownership from actual deserialized children. Cached
enumeration indices do not become persistent design references.

Kernel state stays inside one serial worker process. Cache callbacks transfer
JSON, not OCCT handles. Workers read shared entries and stage candidates privately;
coordinators publish only after worker success and cancellation checks. A failing
feature or drawing does not publish its preceding staged entries.

Individual entries have SHA-256 envelopes and a 64 MiB encoded limit. Complete
snapshots have a 32 MiB raw B-rep limit. Each worker's feature stages have a 32 MiB
aggregate encoded budget. The shared cache retains at most 128 entries and
256 MiB, with locked bounded eviction. These are optimization limits; exceeding
them skips caching rather than changing modeled geometry.

## Developer evidence

`cad_dependency_cache_tests` compares selective rebuilds against cold native
geometry, inspects repeated ownership, restores damaged entries, independently
reads cached STEP exports and checks source/consumer history. `cad_cache_tests`
covers drawings, concurrent publication, native worker failures and quotas.

`cad_dependency_cache_benchmark EVIDENCE_DIRECTORY` records a 128-hole panel with
two parameterized adapter occurrences. Three adapter-width edits compare native
incremental evaluation with separate cold workspaces, checking geometry and
exact reuse decisions. Its report includes durations, model intent, executable
SHA-256, kernel version and native build identity. Timing is not a test threshold.

Internal diagnostics include `feature_keys`, `feature_hits`, staged entry names,
and each projection worker's feature decisions. These are developer evidence;
they are not public MCP fields or a cache-management API. `HANDOFF.md` records
executed results and the limits of local platform evidence.
