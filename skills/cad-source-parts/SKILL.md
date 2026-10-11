---
name: cad-source-parts
description: Find purchasable or catalog CAD components, download and inspect STEP, and preserve supplier, part number, source URL and verified bytes in native Agent CAD assemblies and manufacturing packages.
---

# Source a reusable component

Use the user's chosen supplier/catalog or an available connected catalog/search
tool. If none is selected, search manufacturer data first; a CAD catalog such
as step.parts can supply a model but is not automatically the physical seller.
No catalog connector, download runtime or supplier account is required by the
native CAD engine. Discover host browsing/download capabilities as needed.

1. Establish interface dimensions, range/rating, material, intended quantity,
   part variant and purchasing constraints from the task. Find candidate part
   records and datasheets; compare only relevant differences. Verify current
   stock/price only if the task needs them, with source/date/currency/quantity.
   Do not infer availability from a CAD download.
2. Select the exact part/variant and download its actual STEP using the host's
   supported connector/browser/file download capability. Capture manufacturer,
   physical supplier if selected, part number, record URL, CAD download URL,
   access date, units and available license/notices. If download is gated,
   identify the real gate; do not substitute an invented URL or renamed mesh.
3. Inspect the file with `cad_inspect_step(path, expected_sha256?)`, or
   `cad_artifact` for a read-only source review. Check units, valid
   solids and overall/interface dimensions against the datasheet. A complete
   STEP model may omit threads or internals; record those simplifications.
4. Import the artifact with a new meaningful document ID:

   ```json
   {"document_id":"selected_component","path":"/absolute/downloaded.step","purchase":{"supplier":"Actual supplier or named CAD source","part_number":"Exact selected part","source_url":"https://actual-source.example/selected-part"}}
   ```

   Use actual values. Supply `expected_sha256` when the source publishes a
   checksum; a locally computed hash establishes local byte identity only.
   The native importer always hashes the raw STEP and stores the measured
   `purchase.artifact_sha256`; hash mismatch fails before publishing a revision.
5. Read the imported source and query the relevant dimensions. The saved opaque
   `import_step` preserves original bytes and exact solids, not a reconstructed
   parametric feature history. Use [native-cad](../native-cad/SKILL.md) to capture
   it in the consuming assembly with revision-pinned `set_component`. Position
   it with explicit datum/transform evidence and inspect interference/clearance.
6. Check `cad_bom` for the correct variant, quantity and source identity. Native
   `cad_manufacture` retains the original `source.step` with the artifact ledger.
   Return the chosen part record, dimension comparison, limitations and saved
   component/package links. Preserve supplier notices with any redistributed
   source material and respect its actual terms.

Original purchase identity propagates through unchanged features and rigid
copies. Geometry-changing cuts/patterns do not silently remain the original
purchased part. Conflicting explicit BOM purchase data fails; fix the source of
the mismatch instead of deleting provenance to make a call pass. Source URLs
and supplier identity are caller assertions, not native authenticity checks.
Finding or importing a component does not place an order.
