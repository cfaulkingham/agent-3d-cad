# Agent CAD 1.0: two desktop plugins

Decision, 2026-10-07: focus 1.0 on **ChatGPT desktop and Claude Desktop**.
The CAD engine stays on the user's computer. The viewer runs inside the connected
chat as an MCP App. The standalone app was removed on 2026-10-15.
Codex, Grok Build, OpenCode and other local MCP clients now have concise setup
in `GETTING_STARTED.md`; their documented connection paths do not establish
additional host acceptance. Distribution uses downloaded local packages;
public marketplace submission or approval is outside the release scope.
The published `0.1.0-preview.1` is development evidence, not product readiness.
Keep its immutable tag and historical downloads; do not promote it to 1.0.

## The experience to ship

Install Agent CAD from a local package, then ask for a model. Codex / ChatGPT
uses the local plugin-folder registration in [the setup guide](GETTING_STARTED.md);
Claude uses its extension installer. No language runtime, compiler or separate
viewer application is needed. New projects use Documents/Agent CAD automatically. Existing projects can be reopened without
copying them into the plugin cache. Updating or removing the plugin preserves
the editable project folder, saved revisions and independent exports.

The same chat opens the library, selects a saved model, inspects faces and edges,
sends a reference-qualified edit request and follows the committed revision.
Export STEP/STL and PDF/SVG/DXF drawings from the embedded viewer. An export must
be accessible to the user through the host; a local path printed by a tool is
not sufficient release acceptance. The viewer stays inside the host.

## Host delivery

| Host | Package / connection | Current boundary |
|---|---|---|
| Claude Desktop | Native binary `.mcpb`, with an optional project-folder setting | Actual local update, create, selected-edge edit, live viewer refresh, saved-document listing and STEP/STL/PDF generation verified on macOS arm64; file retrieval and complete restart/reopen/install journey remain required |
| Codex / ChatGPT desktop | Extracted local Agent Plugins package containing the native MCP server and skills | Register the folder and enable it in the host; complete local install/update/reopen acceptance remains required |

Claude documents binary desktop extensions and host-managed installation in
[its extension guide](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop).
The ChatGPT package uses the
[Agent Plugins 1.0 format](https://agent-plugins.org/specification).
OpenAI documents [local plugin catalogs](https://developers.openai.com/plugins/build/plugins#how-local-marketplaces-work)
for installation and private distribution. Register the extracted package with
`codex plugin marketplace add` and enable Agent CAD in the host. The CAD service
stays on the user's computer. This local route requires no public-directory
submission, remote HTTPS endpoint or tunnel.

## Implemented foundation

- `serve --default-workspace` selects Documents/Agent CAD. An explicit
  `--workspace` overrides it for existing projects and isolated tests.
- Windows uses the OS Documents known folder, including redirected locations;
  macOS uses the user's Documents folder. Linux packaging remains engine evidence,
  outside the initial two-host GUI acceptance scope.
- Claude's project-folder setting is optional and defaults to Documents/Agent CAD.
  The engine resolves this folder directly: the host leaves nested `${DOCUMENTS}`
  defaults and empty setting variables unexpanded. Dedicated optional-setting
  startup accepts the host's exact unset marker; other relative paths fail.
- `packaging/make-plugin.py` wraps a verified core bundle with `plugin.json`,
  `mcp.json`, a local marketplace catalog, setup/modeling skills and an icon. It includes native dependencies
  and their existing notices, hashes every file, and rejects desktop bundles.
- CI creates and tests native engine, plugin and Claude extension artifacts.
  No builds need Rust or standalone GUI dependencies.
  Automatic draft-release creation applies only to `v0.*` preview tags.
  A `v1.*` tag does not automatically publish a release.

For developers, after installing a verified native bundle:

```sh
python3 packaging/make-plugin.py bundle build/plugins
python3 tests/plugin_smoke.py build/plugins/agent-cad-plugin-VERSION-Darwin-arm64
python3 packaging/make-mcpb.py bundle build/plugins/Agent-CAD-Claude.mcpb
```

These are build/test commands. They are not the end-user install flow.
Local Codex / ChatGPT installation follows the setup guide's folder registration
and host enablement steps. After extraction, `.agents/plugins/marketplace.json`
names the package's own root as its local plugin source. Keep the extracted
folder intact for installation and updates.

## Required evidence before 1.0

The integration branch implements the MCP Apps
[`ui/download-file` request](https://apps.extensions.modelcontextprotocol.io/api/interfaces/app.McpUiDownloadFileRequest.html)
when the host advertises
[`downloadFile`](https://apps.extensions.modelcontextprotocol.io/api/interfaces/app.McpUiHostCapabilities.html).
Exported files are bounded, source-qualified MCP resources with MIME types and
filenames; arbitrary local file reads are not exposed. See
[EXPORT_DOWNLOADS.md](EXPORT_DOWNLOADS.md). Verify the host's actual save/download
action with STEP, STL and drawings. Capability support in either target host is
not yet established. Implementation tests alone do not close this gate.

Run the complete journey in a fresh host profile on each supported host/platform:

1. Install the downloaded local package using the setup guide, including Codex's
   folder-registration command where applicable; accept the default project
   folder. Record actual host version, package hash, OS and architecture.
2. Create an editable mounting plate and open the viewer inside that chat.
3. Pick a face and an edge; read their exact context in the connected chat.
   Apply a selective edge edit and verify the same viewer refreshes and clears
   the old selection. Failed geometry must preserve the previous revision.
4. Restart the host, reopen the saved project and inspect an older revision.
5. Export STEP/STL and a drawing. Retrieve the actual files through a supported
   host action and verify source/revision preservation.
6. Upgrade the extension and repeat reopen/edit/export. Remove the extension
   and verify saved projects remain. Reinstall and reopen them.
7. Verify publisher signing/notarization as applicable and the local package
   install/update route. Resolve CPU selection so users do not need architecture
   knowledge. The native macOS universal composer is implemented; the same
   composed archives require real ARM/Intel runtime and host installation
   evidence. See [LOCAL_PACKAGE_ACCEPTANCE.md](LOCAL_PACKAGE_ACCEPTANCE.md).

Record failures as unfinished work. Unit tests, SDK interoperability and native
package smoke are prerequisites; they do not substitute for these host checks.
The version remains a development preview until both host journeys pass.
