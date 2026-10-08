# Manufacturing packages

`cad_manufacture` exports an explicit committed revision into a complete,
independently movable directory. Its editable source, unique leaf geometry,
drawings, inventory, purchasing data and process assumptions remain qualified
to that revision. It does not change HEAD or historical artifacts.

```json
{
  "document_id": "nested_assembly_demo",
  "revision": 1,
  "options": {
    "parts": [
      {"feature_id": "block", "process": "cnc", "material": "Aluminum",
       "notes": ["Stock, tooling and production tolerances are unspecified."]},
      {"feature_id": "rod", "process": "unspecified"}
    ],
    "part_drawing": {
      "views": [{"id": "front", "orientation": "front"}],
      "dimensions": [{"view": "front", "kind": "width"}]
    }
  }
}
```

These process/material choices illustrate caller assumptions. The example does
not establish production suitability. Use `cad_job` for a substantial package;
its native worker is bounded and cancellable like other geometry jobs.

## Contents and coordinates

For an assembly, the package exports every distinct leaf source once, with
rolled-up quantities and occurrence paths. Reused subassemblies retain every
saved leaf transform in `occurrences`. Each source STEP/STL and drawing stays in
that source feature's original coordinates, including any authored translation.
Assembly STEP/STL retain the complete saved pose. File directories use generated
names so model-internal Windows device identifiers remain portable.

```text
manifest.json
source.json
bom.json
bom.csv
parts/part-1/part.step
parts/part-1/part.stl
parts/part-1/drawing.pdf
parts/part-1/drawing.svg
parts/part-1/<view>.dxf
parts/part-1/drawing.json
assembly/assembly.step
assembly/assembly.stl
assembly/drawing.pdf
assembly/drawing.svg
assembly/<view>.dxf
assembly/drawing.json
```

`source.json` includes the complete editable model with its source identity;
pinned components keep their self-contained snapshots and checksums. Its `model`
can create a document in another workspace without the original component library.
The manifest binds the source model's native canonical JSON SHA-256. Every listed
artifact has a relative path, byte length and SHA-256 of its actual file bytes.
The manifest itself is outside that list; the reported package byte total includes
it. No absolute workspace/export paths appear in the manifest's artifact references.

Part entries identify their original feature, directory, inventory metadata,
quantity, occurrence paths, native measurements and process assumptions. Assembly
entries identify their source feature and measured saved pose. The drawing ledger
retains caller recipe, resolved parameters, measured dimensions and projection
tolerance. A default drawing title identifies its feature; document/revision
identity stays on the sheet. Drawing dimensions and manufacturing tolerances use
the ordinary [drawing contract](DRAWINGS.md); no production tolerance is invented.

Single-solid or multi-solid feature outputs are also supported. Their package has
one source row; `solid_count` explicitly describes that source. Optional
`feature_id` selects an earlier solid or assembly. A sketch is not manufacturing
solid output. Part assumptions must name selected leaf sources, not groups or
occurrence paths.

## Options

- `formats`: unique nonempty subset of `step`, `stl`; default both.
- `drawings`: boolean, default true. False disables drawing generation and rejects
  conflicting recipes. Native default drawings use standard views.
- `part_drawing`: shared part recipe. A part entry's `drawing` overrides it.
- `assembly_drawing`: assembly recipe; invalid for a non-assembly selection.
- `parts`: at most 256 unique `{feature_id, process?, material?, notes?, drawing?}`
  entries. Process is `unspecified`, `fdm`, `cnc`, `sheet_laser`, `molding` or
  `purchased`. Omitted processes are explicitly `unspecified`. Material defaults
  only from supplied BOM metadata; an explicit part assumption overrides it.
- `notes`: global caller notes. Global and per-part notes permit 16 entries of
  240 UTF-8 bytes each; part material permits 64 printable UTF-8 bytes.
- `fabrication_review`: the explicit options from
  [measured fabrication review](FABRICATION_REVIEW.md). Includes a hashed
  `review.json` with source/build identity and preserves its findings in the manifest.

Without a requested review the manifest records
`process_review.status: not_evaluated`. A requested review records
`process_review: {status, report_path}`; failed/unknown measured checks retain
that status. Findings do not suppress geometry exports. These outputs and
assumptions do not imply measured manufacturability, mesh/process certification,
slicer output, machine setup or authorization to start hardware.
[Measured review](FABRICATION_REVIEW.md) is a separate native capability;
sourced-part import preserves verified artifact identity and original bytes;
installed slicing is described in [SLICING.md](SLICING.md), while intentional
printer handoff remains under
[the active fabrication scope](COMPOSITION_FABRICATION_REVIEW.md).

## Purchasing identity

[Sourced imports](PURCHASED_PARTS.md) automatically retain verified purchasing
identity for unchanged leaf geometry and rigid copies, including pinned component
reuse. Their original `source.step` joins the hash ledger beside independent
exports; each part records relative `source_artifact` provenance. This also works
for a scoped standalone imported solid. Conflicting assembly purchasing metadata
fails validation; source URLs remain caller assertions.

Saved assembly BOM metadata accepts optional `purchase`:

```json
{
  "input": "rod",
  "purchase": {
    "supplier": "Example supplier",
    "part_number": "ROD-2",
    "source_url": "https://example.invalid/catalog/ROD-2",
    "artifact_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  }
}
```

This is fixture data, not a sourced component recommendation. Supplier, supplier
part number and HTTP(S) source URL are required nonempty printable ASCII, bounded
to 120, 64 and 512 bytes. Optional `artifact_sha256` is a lowercase SHA-256 supplied
by the caller. The service records this provenance; it does not fetch the URL or
verify a supplier claim. Source `part_number` and supplier `part_number` are
distinct fields. Repeated shared sources with conflicting purchase records fail
explicitly; use distinct source features for distinct purchasable items.

BOM JSON and nested structure preserve `purchase`. CSV appends `supplier`,
`supplier_part_number`, `source_url` and `artifact_sha256` to its seven existing
columns. Missing purchasing fields are blank; free-text CSV formula prefixes are
neutralized like other BOM text.

## Failure and publication

One native worker builds/restores geometry and produces a private directory.
Part drawings use that same evaluated graph, scoped to the exact source feature.
The service publishes only the complete generation after success and cancellation
checking under the document publication lock. Failure, timeout or cancellation
before publication removes the private generation and leaves existing packages
and editable history intact. A historical export remains historical even if HEAD
advances while it runs. Completed durable job replay returns the same saved result.

Limits are 256 unique sources, 2,560 listed files, 64 MiB per file and 256 MiB per
package including its manifest, in addition to existing geometry/drawing/worker
limits. Exceeding a limit fails the package; it never silently omits a part or
failed drawing. There is no dependency on a slicer, Python, Node or a compiler in
the packaged native workflow. `HANDOFF.md` records executed acceptance evidence.
