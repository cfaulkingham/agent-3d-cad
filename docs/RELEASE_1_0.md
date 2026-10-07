# Agent CAD 1.0: two desktop plugins

Decision, 2026-10-07: focus 1.0 on **ChatGPT desktop and Claude Desktop**.
The CAD engine stays on the user's computer. The viewer runs inside the connected
chat as an MCP App. Standalone Tauri, CLI onboarding and other hosts are deferred.
The published `0.1.0-preview.1` is development evidence, not product readiness.
Keep its immutable tag and historical downloads; do not promote it to 1.0.

## The experience to ship

Install Agent CAD in the host, then ask for a model. No terminal, config editing,
executable paths, language runtime or separate viewer application. New projects
use Documents/Agent CAD automatically. Existing projects can be reopened without
copying them into the plugin cache. Updating or removing the plugin preserves
the editable project folder, saved revisions and independent exports.

The same chat opens the library, selects a saved model, inspects faces and edges,
sends a reference-qualified edit request and follows the committed revision.
Export STEP/STL and PDF/SVG/DXF drawings from the embedded viewer. An export must
be accessible to the user through the host; a local path printed by a tool is
not sufficient release acceptance. No browser window or Tauri app is involved.

## Host delivery

| Host | Package / connection | Current boundary |
|---|---|---|
| Claude Desktop | Native binary `.mcpb`, with an optional project-folder setting | Actual local update, create, embedded viewer, selected-edge context and PDF generation verified on macOS arm64; full edit/reopen/export/install journey remains required |
| ChatGPT desktop | Local Agent Plugins package containing the native MCP server, modeling skill and setup skill | Package built; host loading and rendering still required; local marketplace is a test/distribution path, not public-directory approval |

Claude documents binary desktop extensions and host-managed installation in
[its extension guide](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop).
The ChatGPT package uses the
[Agent Plugins 1.0 format](https://agent-plugins.org/specification).
OpenAI's [public plugin guidance](https://developers.openai.com/plugins/build/plugins)
currently calls for a public HTTPS MCP endpoint or contacting OpenAI for local
MCP support. Preserve local CAD; resolve that distribution gate before promising
installation from the public directory. Do not silently replace this product
with a hosted CAD service or require users to run their own tunnel.

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
- CI creates and tests separate plugin artifacts before any optional legacy
  Tauri work. Main/PR builds do not need Rust or GUI build dependencies.
  The old automatic draft-release contract applies only to `v0.*` preview tags.
  A `v1.*` tag does not automatically publish a release.

For developers, after installing a verified native bundle:

```sh
python3 packaging/make-plugin.py bundle build/plugins
python3 tests/plugin_smoke.py build/plugins/agent-cad-plugin-VERSION-Darwin-arm64
python3 packaging/make-mcpb.py bundle build/plugins/Agent-CAD-Claude.mcpb
```

These are build/test commands. They are not the end-user install flow.
Private ChatGPT testing follows the host's local marketplace import process;
the generated ZIP is a package artifact, not a claimed one-click installer.
After extraction, `.agents/plugins/marketplace.json` names the package's own
root as its local plugin source. Keep the extracted folder intact. This catalog
is for host testing; no directory approval is implied.

## Required evidence before 1.0

Run the complete journey in a fresh host profile on each supported host/platform:

1. Install through the host with no terminal or config edits; accept the default
   project folder. Record actual host version, package hash, OS and architecture.
2. Create an editable mounting plate and open the viewer inside that chat.
3. Pick a face and an edge; read their exact context in the connected chat.
   Apply a selective edge edit and verify the same viewer refreshes and clears
   the old selection. Failed geometry must preserve the previous revision.
4. Restart the host, reopen the saved project and inspect an older revision.
5. Export STEP/STL and a drawing. Retrieve the actual files through a supported
   host action and verify source/revision preservation.
6. Upgrade the extension and repeat reopen/edit/export. Remove the extension
   and verify saved projects remain. Reinstall and reopen them.
7. Verify publisher signing/notarization as applicable and the actual directory
   install/update route. Resolve CPU selection so users do not need architecture
   knowledge; a macOS universal package or host-selected build is still planned.

Record failures as unfinished work. Unit tests, SDK interoperability and native
package smoke are prerequisites; they do not substitute for these host checks.
The version remains a development preview until both host journeys pass.
