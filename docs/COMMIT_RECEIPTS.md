# Committed revision receipts

The gap-closure preview changes the replies of `cad_create`, `cad_apply`,
`cad_restore`, and `cad_import`. They return a closed commit receipt:

```json
{
  "schema_version": 1,
  "document_id": "plate",
  "revision": 2,
  "kernel_version": "8.0.1",
  "model_sha256": "64 lowercase hexadecimal characters",
  "summary": {}
}
```

`summary` retains its complete typed geometry, assembly, sheet and component
contract; the empty object above is only an abbreviated example.
`cad_import_sketch` also supplies its existing `feature_id` and `source_sha256`.
Every receipt includes `model_sha256`. The full editable source is returned by
`cad_read` with the receipt's `document_id` and `revision`:

```json
{"document_id":"plate","revision":2}
```

Use that exact revision rather than HEAD when another writer may have advanced
the document. `cad_read` does not build geometry or include a summary. Its
record and the immutable saved document format remain unchanged. The model
hash is SHA-256 of the native compact, sorted-key UTF-8 JSON serialization of
the model, without a trailing newline. It covers embedded source bytes as part
of their model representation.

This is a preview API change: clients accessing `mutation_result.model` must
fetch `cad_read` at the returned revision. The same Service produces these
replies through CLI, MCP and jobs. Durable mutation retries and interrupted-job
recovery return the same historical receipt. Existing durable full-model job
results are projected on read without changing their stored result bytes;
older sketch receipts recover their model hash from the exact saved revision.
Missing historical source cannot be replaced with an invented hash.

The change avoids repeating large editable documents in every mutation result
and every nested job-result schema. Input validation and the complete source
schema remain strict and standalone. Discovery still uses its existing size
gate. Its structural factoring moves only context-independent assertions into
local reference siblings; properties, items, closure and evaluation annotations
remain at their original nodes. Independent Draft 2020-12 tests check exact
reversibility, literal JSON preservation, scopes, pointer targets and accepted
and rejected instances.
