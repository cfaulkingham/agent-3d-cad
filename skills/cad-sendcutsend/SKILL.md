---
name: cad-sendcutsend
description: Preflight native Agent CAD STEP or DXF parts for SendCutSend against current material, thickness, cutting and bending guidance; prepare traceable files and a concrete findings report.
---

# SendCutSend preflight

Review the requested parts and prepare the files supported by the current
supplier workflow. This skill is a supplier-specific review, not an order or
automatic claim of upload acceptance. Use
[cad-manufacturing](../cad-manufacturing/SKILL.md) for general process review.

1. Read the actual source/revision or inspect the provided artifact. Record
   quantity, material, stock thickness, finish, requested operations and critical
   dimensions. Do not silently choose another alloy/thickness to pass a check.
2. Read [supplier rules and evidence](references/supplier-rules.md), then open
   the current relevant SendCutSend material and service pages. Record the exact
   stock row, process, numeric limits, units, URL and access date. Missing or
   inaccessible stock data remains unresolved; a general rule is not a substitute
   for a stock-specific rule.
3. For an unbent native part use `cad_query` summary/topology and
   `cad_fabrication_review` with `process: sheet_laser`, the correct stock-normal
   orientation, and actual `sheet_thickness_mm` plus
   `sheet_thickness_tolerance_mm`. Add known envelope/wall limits only when their
   native meaning matches the requirement. `sheet_laser` tests prismatic shape
   and thickness; it does not measure every hole/web or certify laser versus
   router/waterjet suitability. Use current `cad_measure` face/edge distances or
   exact analytic topology for individually identified features.
4. For formed parts inspect native sheet definition, bend radius/allowance and
   the supplier's bend table. Do not run the unbent prismatic-form rule on the
   complete formed body and describe that expected failure as a vendor rejection.
   Developed blank correctness and supplier bend agreement are separate checks.
5. Export the selected source part. A STEP candidate uses
   `cad_export(document_id,revision,feature_id,format: step)` and must contain the
   intended single sheet solid. For a flat DXF candidate, request one view normal
   to the stock with `formats: [dxf]`, `hidden_lines: false`, empty dimensions
   and no BOM/callouts. Use `cad_drawing` only when the document output is that
   source part: it has no top-level `feature_id` selector. For assembly sources,
   use `cad_manufacture` with the selected `feature_id` and
   `options.part_drawing` set to that recipe to obtain a scoped part drawing.
   The per-view DXF is 1:1 mm, but may still
   contain center/annotation layers: **inspect the actual entities/layers**.
   A projection is not automatically a clean laser profile. Where native tools
   cannot remove unwanted entities or prove closed/duplicate-free contours,
   return the candidate with that concrete blocker; use available CAD editing
   tools if authorized and re-inspect their actual output.
6. Report one row per part with source/hash, stock row, verified checks,
   failures, unknowns, and linked candidate files. A useful verdict is
   `ready for supplier upload check`, `needs design/file changes`, or
   `insufficient supplier/geometry evidence`. The first still requires the
   supplier's actual parser/quote review. Preserve the revised file hash and
   repeat affected checks after requested edits.

Proceed with authorized preparation and redesign. Upload a design, submit a
quote, or place an order only within the user's actual request; preparing a
preflight does not itself authorize those external actions.
