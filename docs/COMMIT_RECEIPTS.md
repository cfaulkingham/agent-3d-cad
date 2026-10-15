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
  "summary": {},
  "changes": {
    "base_revision": 1,
    "features": {"created": [], "modified": [], "removed": []},
    "parameters": {"created": [], "modified": ["thickness"], "removed": []},
    "feature_order_changed": false,
    "output_changed": false,
    "affected_features": ["base", "plate"],
    "occurrences": {"created": [], "modified": [], "removed": []},
    "affected_occurrences": []
  }
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

## Change lists

`changes` compares the new source with its immediate predecessor, not with the
current HEAD when the receipt is later retrieved. Creation/import has
`base_revision: null`; restore compares the new revision with the HEAD it
replaced. Arrays contain sorted, unique persistent IDs. A no-op edit can commit
a revision with empty lists and false order/output flags.

`features` and `parameters` compare authored declarations. A changed parameter
does not mean its feature declarations were edited. `affected_features` lists
new or existing features whose geometry dependency fingerprints changed,
including downstream consumers. It is conservative impact information, not a
claim that exact shape bytes or measured geometry changed. Unused parameters
do not affect geometry; BOM/purchasing and saved-pose metadata can modify
declarations without changing geometry fingerprints.

`occurrences` compares leaf paths in the output assembly, including each leaf's
authored ancestor part declarations. Moving an ancestor modifies its descendant
paths; removing/replacing an output assembly removes its former leaf paths.
`affected_occurrences` additionally includes leaves with an affected source or
ancestor assembly. This is deliberately conservative for mates and shared
subassemblies: use measured summary transforms to establish actual movement.
These are source paths, never face/edge enumeration IDs or stable topology claims.

Change calculation performs no second geometry build. It runs before publication
under the same revision check. New durable request receipts persist the complete
change list. Old receipts and legacy job results reconstruct missing lists from
their exact historical source and predecessor without rewriting stored bytes.
Missing historical source is an explicit failure; it is never replaced with HEAD.

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
