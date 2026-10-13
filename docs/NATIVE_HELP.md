# Searchable native help

`cad_help` searches the native tool and feature contracts and bundled capability
guides. Help is compiled into the executable: it works offline and after moving
the native bundle, without a source checkout or language runtime. It does not
build geometry, edit models or read caller-selected files.

## Find and read

```json
{"query":"round the edges","limit":5}
```

Results include stable topic keys, kind, title and source. Pass a returned topic
to read its guidance and related guides:

```json
{"topic":"feature/fillet"}
```

Tools return exact standalone input/output schemas; features and edit operations
return their standalone input schema with all referenced definitions. These are
generated from the running native contracts, not duplicated handwritten schemas.
Guide topics return the compiled Markdown text and its repository source line.
Related topics lead to examples and limitations. Help is reference material;
semantic validation and kernel checks still decide whether an operation succeeds.

Search matches case-insensitive words in names, headings and content. Common
terms such as round/fillet, bevel/chamfer, hollow/shell, subtract/cut and
snapshot/render are expanded. Results are ranked deterministically, with names
weighted above body text. No arguments returns the first ten topics; `limit`
allows 1–20 matches and `total`/`truncated` describe omitted results. Use a more
specific query to narrow the index. An unknown search returns an empty list;
an unknown exact topic returns `not_found`.

Guide content is paged at 12,000 UTF-8 bytes. If `entry.next_offset` is non-null,
request the same topic with that byte `offset`. Boundaries preserve complete
UTF-8 characters. Schemas are returned whole, independent of guide pagination.
Query and topic are mutually exclusive, each at most 256 characters. `limit`
belongs to search and `offset` belongs to topic reads. Help is immediate and
does not use `cad_job`.

## Rounding edges with fillet

Start with a small editable model in millimeters:

```json
{"document_id":"help_plate","model":{"schema_version":1,"units":"mm",
"parameters":{"thickness":6},"features":[
{"id":"base","type":"box","size":[40,30,{"parameter":"thickness"}]},
{"id":"rounded","type":"fillet","input":"base","radius":2,
"edges":{"type":"geometric","feature_id":"base","curve_kind":"line",
"expected_count":4,"direction":{"vector":[0,0,1],"tolerance":0.000001}}}],
"output":"rounded"}}
```

Call `cad_create` with this object. The rule rounds the four vertical edges of
the box. Selection tolerance is in radians here; lengths and radius are in mm.
The expected count is deliberate. Missing or ambiguous matches fail rather than
choosing a different edge. Reduce a radius only when that change fits the user's
intent; geometry failures must not silently remove or change a feature.

## Edit and verify

Read the document before editing. After the preceding create at revision 1:

```json
{"document_id":"help_plate","expected_revision":1,"operations":[
{"op":"set_parameter","name":"thickness","value":8}]}
```

Pass this to `cad_apply`; a successful edit commits revision 2. Check the returned
bounds, volume, validity and solid count against the intended change. Measure
critical dimensions with `cad_measure`, and inspect multiple views:

```json
{"source":{"document_id":"help_plate","revision":2},
"settings":{"view":"sheet","width":1024,"height":768}}
```

This `cad_render` call creates one PNG containing labeled front, top, right and
isometric views at a common orthographic scale. Use measured values for exact
dimensions; an image is a visual check, not a dimensional drawing. All four views
refer to the same source revision. Export that revision with `cad_export` when
the measurements and images agree with the request.
