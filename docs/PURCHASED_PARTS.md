# Sourced and purchased parts

`cad_import` can bind caller supplier identity to the exact bytes of a local
STEP artifact. Search a catalog or supplier, select the actual part, download
its STEP file and preserve the source record before importing. The native
service does not search catalogs, fetch URLs, assert availability or place orders.
An agent can use its connected catalog or browsing tools for that first step.

```json
{
  "document_id": "purchased_washer",
  "path": "/absolute/local/washer.step",
  "purchase": {
    "supplier": "Caller-selected supplier or explicitly identified CAD source",
    "part_number": "Caller-selected supplier part identity",
    "source_url": "https://example.invalid/parts/washer"
  }
}
```

If the supplier publishes a checksum, also supply `expected_sha256` as its
64-character lowercase SHA-256. The native importer hashes the unchanged raw
file before geometry evaluation; disagreement fails with `artifact_mismatch`
and expected/actual hashes, publishing no revision. Optional
`purchase.artifact_sha256` is another precondition and must also match. A
successful import always saves the measured hash in both the opaque feature's
`sha256` and its `purchase.artifact_sha256`.

Supplier, part number and source URL remain caller assertions. Hash agreement
proves byte identity, not manufacturer authenticity, dimensional accuracy,
standards compliance, licensing, inventory or fitness for a machine. Identify a
CAD catalog as the CAD source when no physical supplier has been selected;
do not present the catalog as a verified seller. Keep source records and notices.

## Editable source and reuse

The saved `import_step` feature contains the complete original STEP string,
its hash and optional purchasing identity. No path or URL is needed to rebuild.
Direct document creation/editing must provide `purchase.artifact_sha256` and it
must equal the feature's verified content hash. Ordinary unsourced imports remain
compatible. STEP imports retain the existing 512 KiB, UTF-8, complete-root and
valid-solid limits; no feature history is reconstructed.

Use `set_component` with an explicit source document/revision to incorporate an
imported part into another document. The self-contained captured source retains
its purchase/hash through feature remapping. Repeated/nested assembly instances
derive their quantities and occurrence paths normally. Source changes, including
supplier assertions, cannot silently change a pinned consuming revision.

Pure `transform` and `instance` chains inherit the original purchased identity.
Cuts, holes, patterns and other geometry-changing operations do not silently
claim to be the unchanged purchased part. Their editable source still retains
the upstream original. This rule does not infer quantity or procurement identity
from solid enumeration.

## BOM and manufacturing packages

`cad_bom` derives `purchase` for unchanged sourced leaf features, including
rigid copies and remapped components. Hierarchical leaf entries expose the same
identity. Callers may still supply item numbers, local part numbers, descriptions
and materials on assembly BOM rows. An explicit row purchase must exactly match
the verified imported source; contradictory metadata fails model validation and
preserves the current revision. Explicit legacy purchase metadata on unsourced
geometry remains a caller assertion.

Supplier CSV fields retain quoting and formula neutralization. Recorded part
numbers are not inferred into local part numbers, materials or process limits.

`cad_manufacture` preserves sourced-part metadata for scoped standalone solids
and assemblies. Each unchanged sourced leaf gets an additional `source.step`
containing the original bytes, and `source_artifact: {path, sha256,
source_feature_id}` in its part manifest. The file is included in the ordinary
portable artifact hash/byte ledger and package budgets. Independent `part.step`
and `part.stl` exports represent the selected feature's geometry; they can differ
from the source bytes, particularly after rigid transforms. Repeated source
instances export once per selected leaf source.

The complete editable `source.json` retains imported content and captured pins.
Package generation and import use existing bounded native workers, atomic
publication, durable jobs and retry receipts. Failed work preserves HEAD/history
and earlier artifacts. No executable input, network dependency, slicing or
physical printing is introduced into ordinary CAD creation.

`examples/purchased-washer.import.json` identifies the catalog's M5 washer and
published artifact hash. Download that selected artifact and replace `path` with
its explicit local path before calling the tool. Its supplier field explicitly
identifies the CAD catalog; no physical seller is selected by the example.
