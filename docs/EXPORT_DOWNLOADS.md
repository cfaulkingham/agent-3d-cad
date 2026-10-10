# Export downloads

`cad_export` and `cad_drawing` return a `downloads` array of MCP resource links,
alongside the native workspace paths. Each link carries `type: "resource_link"`,
`uri`, `name`, `mimeType` and byte `size`. MCP tool responses also publish these
links as native content blocks, including completed `cad_job` exports.

STEP and STL exports produce one link. A 3MF result includes every plate and
its layout report. Drawings include each requested PDF/SVG sheet
and DXF view. The set is limited to 128 files; each file is nonempty and at most
64 MiB. Files exceeding the delivery limit remain in the export folder and are
reported in `download_errors` with a filename, code and message.

The URI is `cad-export://<manifest-sha256>/<filename>`. Capture copies the exact
native output to `downloads/<manifest-sha256>.bin` and publishes a matching JSON
manifest only after the bytes are stored. The manifest records the committed
document/revision, its complete record hash, filename, MIME type, byte count and
content hash. `resources/read` verifies the manifest, unchanged historical
source record and stored bytes before returning base64 resource contents.
Resources survive restart and later edits. Changing or removing the original
export does not change an already captured resource.

Resource reads accept only these opaque URIs; paths and arbitrary workspace
reads are not exposed. Capture admits only regular files below the native
exports directory. Symlinks in managed capture/read paths, malformed URIs,
changed bytes/manifests/source records and unavailable files fail explicitly.
The existing local-workspace trust boundary applies; this is not a hostile-user
filesystem sandbox. There is no automatic download-resource garbage collector.

The embedded viewer calls the MCP Apps
[`ui/download-file`](https://apps.extensions.modelcontextprotocol.io/api/interfaces/app.McpUiDownloadFileRequest.html)
method only when the host advertises
[`downloadFile`](https://apps.extensions.modelcontextprotocol.io/api/interfaces/app.McpUiHostCapabilities.html).
It submits the resource links and retains the workspace paths. Acknowledgement
is described as a download request, not proof that the user saved a file. Host
refusal, timeout, missing capability and partial capture retain the fallback.
The protocol's [`isError` result](https://apps.extensions.modelcontextprotocol.io/api/interfaces/app.McpUiDownloadFileResult.html)
also reports a declined/cancelled download; it is not treated as acknowledgement.
Late responses cannot overwrite status for a new source/revision. The
standalone app keeps its existing native Save dialog.

Native tests recover and compare exact bytes for STEP/STL/3MF/PDF/SVG/DXF and
multi-plate layouts; cover MCP, isolated jobs, cold restart, source edits,
original-export changes, bounds and corruption. Controller tests exercise host
capabilities, refusal and stale responses. These are implementation tests.
Actual target-host save actions and complete installation journeys remain
separate release gates in [RELEASE_1_0.md](RELEASE_1_0.md).
