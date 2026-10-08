# Pinned editable components

Use `set_component` inside `cad_apply` or `cad_preview` to reuse an exact saved
source revision. Source documents must be in the same workspace when capturing
or explicitly updating a component. Querying, editing and exporting the saved
consumer never reads its source documents: its snapshot is self-contained.

```json
{"op":"set_component","id":"hinge_module",
 "source_document_id":"nested_assembly_demo","source_revision":1,
 "source_feature_id":"module",
 "bindings":{"angle":{"parameter":"module_angle"}}}
```

`id` becomes the component's local output feature ID, so assembly parts use
`"input":"hinge_module"`. `source_feature_id` defaults to that source revision's
output. The source can be a solid or an articulated assembly; intermediate
sketches cannot be component outputs. All required upstream features are copied
as ordinary editable local features, with deterministic namespaced IDs. Sketch,
Boolean, selector, assembly and BOM references are remapped together. Internal
part and mate names stay unchanged. Reusing this local output multiple times
shares its source dimensions and motion; import under another component ID to
create an independently editable variant.

The optional `bindings` object maps source parameter names to consumer scalars:
numeric literals, consumer parameter references, or the existing bounded unit-
declared arithmetic expressions. Only parameters used by the selected output
may be bound. Unbound used parameters receive namespaced local numeric entries.
Bindings and all operations in the batch validate against the final consumer
document. Normal scalar units and bounds apply. Changing a bound consumer
parameter updates geometry without being classified as a local source edit.

The document's optional `components` array records each component's source
document/revision/feature/kernel, the complete source model in `snapshot`, its
SHA-256, `bindings`, and explicit source-to-local `feature_map` and
`parameter_map`. These maps name editable features and parameters, never
evaluated topology. Long IDs use a bounded hash suffix; callers should read the
stored maps. The checksum covers the service's canonical JSON serialization of
the snapshot, including any earlier component provenance. Integrity, ownership,
and maps are validated when reading and rebuilding a saved document.

Source changes never propagate automatically. Call `set_component` again with
the same local `id` and an explicit `source_revision` to update. The source output
is selected anew unless `source_feature_id` is supplied. Omitted bindings retain
the component's existing mappings; supplying `{}` removes them. The local root
ID stays stable. The update replaces the captured dependency block in its prior
position; downstream references must remain valid. Batch related consumer edits
in the same transaction when an upstream dependency disappears or changes type.

Materialized features remain editable through normal operations, including
child motion and named poses. Summary `components` lists source identity,
checksum, `modified`, and changed local feature/parameter IDs. The viewer labels
the root feature with its pinned source revision and local-edit state. Updating
a component with local edits fails with `component_modified` and the affected
IDs. Preview an update with `discard_local_changes: true` to review replacing
those edits; only an explicit save commits the replacement. This also resets
unbound local parameter overrides to the requested source's values.

`detach_component(id)` removes source tracking and keeps its local editable
features/parameters. `remove_component(id)` removes both tracking and its owned
features/parameters. Any remaining consumers or output references must be repaired
in the same batch. Neither operation modifies the source document.

Capture/update uses the ordinary expected-revision transaction and bounded job
path. Failed validation, kernel work, stale revisions and cancellation do not
publish a partial component or mutate source/history. Explicit request IDs
deduplicate a replay. Copies of the consumer model can be created in another
workspace without its libraries; refreshing there requires the named source
revision to be available again. Components have no live filesystem links.

Bounds: 64 tracked components per document, four levels of embedded source
provenance, and the existing 1 MiB JSON, 256 local-feature, 128 local-parameter,
assembly-expansion, kernel and worker budgets. Snapshots count toward the JSON
budget even when only part of their dependency graph is materialized. Removing
or detaching components does not retroactively alter historical revisions.

Example: create `nested-assembly.create.json`, then
`component-assembly.create.json`, and apply `component-assembly.edit.json`.
The resulting fixture reuses the source module twice and binds both child angles
to the consumer's `module_angle`. It preserves the pinned module's BOM metadata,
articulation and exact solids for STEP/STL, drawings and robot handoff.
