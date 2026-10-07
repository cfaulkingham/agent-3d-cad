![agent-3d-cad — Native, editable CAD for AI agents. Create, inspect, refine, export.](docs/assets/readme-banner.png)

# Agent CAD

Create and refine real CAD models with your AI agent. Keep editable projects,
select faces and edges, and export STEP, STL, and PDF/SVG/DXF drawings.
The native CAD engine runs locally. You do not install Python, Node, Rust,
CMake, or a compiler to use a release bundle.

**Preview:** release packaging is being prepared. No public release is available
yet; [Actions](https://github.com/cfaulkingham/agent-3d-cad/actions) currently holds
CI previews. The installation commands below work once the matching version is
published. See [validation status](docs/HANDOFF.md) for what has actually been tested.

## Install

Choose a download from [Releases](https://github.com/cfaulkingham/agent-3d-cad/releases):

| Download | Use it for |
|---|---|
| `agent-3d-cad-desktop-…` | CLI agents plus the standalone **Tauri** viewer and project library |
| `agent-3d-cad-…` | MCP hosts with an embedded viewer, or a headless native service |
| `agent-3d-cad-….mcpb` | Claude Desktop extension with a workspace folder picker |

Build targets are macOS 15+ (Apple Silicon and Intel), Linux with glibc 2.39+
(x64 and arm64; Ubuntu 24.04 baseline), and Windows x64. The standalone Tauri
viewer uses the OS webview: macOS includes it; Windows needs Microsoft WebView2;
Linux needs WebKitGTK 4.1 and GTK 3. Platform validation is tracked separately
from the build targets. Signing/notarization are not yet configured.

Download `install.sh` or `install.ps1` from the **same release**, then run:

```sh
# macOS / Linux — installs the standalone viewer and native service
bash install.sh 0.1.0-preview.1
# Add --core for the smaller service-only bundle.
```

```powershell
# Windows PowerShell
.\install.ps1 -Version 0.1.0-preview.1
# Add -Core for the service-only bundle.
```

The installer detects your architecture, verifies the archive against the release's
`SHA256SUMS`, and installs into a new version directory in your user account.
It prints the executable path and setup commands. Keep your model workspace
outside the application directory so upgrades preserve your projects.
You can also extract an archive manually; keep `bin`, `lib`, `share`, and
`desktop` (when present) together.

## Connect your agent

Use the installed executable path wherever `agent-3d-cad` appears below.
Choose one persistent workspace, for example `~/Documents/Agent CAD`, and use
its **absolute path** in the client settings and standalone viewer.

| Client | Setup |
|---|---|
| ChatGPT desktop / Codex | Add a local STDIO MCP server, or use `codex mcp add` |
| Claude Desktop | Install the matching `.mcpb` from Settings → Extensions, then select your workspace |
| OpenCode desktop / CLI | Add a local server in `opencode.json` |
| Other CLI agents, including Grok Build and Muse | Use their local stdio MCP setup when supported, or call the executable directly |
| ChatGPT web | Separate remote/tunnel connection; a local executable path is insufficient |

Generate the right settings without hand-escaping paths:

```sh
agent-3d-cad config --client codex --workspace "/absolute/path/to/CAD workspace"
agent-3d-cad config --client claude --workspace "/absolute/path/to/CAD workspace"
agent-3d-cad config --client opencode --workspace "/absolute/path/to/CAD workspace"
```

These commands print settings to merge into your existing configuration.
[Client setup](docs/GETTING_STARTED.md) has exact locations, registration commands,
ChatGPT web guidance, and the distinction between documented configuration and
a client that has been tested with this service.

## Create your first project

Ask your connected agent:

> Create an editable 80 × 50 × 6 mm mounting plate with four mounting holes.
> Save it as mounting_plate and show it in the viewer. Then make it 8 mm thick
> and export a STEP file.

In an MCP Apps host, the agent opens the embedded viewer with `cad_open`.
For a CLI agent, open the standalone app once:

```sh
agent-3d-cad viewer --workspace "/absolute/path/to/CAD workspace"
```

This opens an application window, with no browser tab or local web server.
Choose a saved project, search the library, or use **Open workspace** and
**Recent workspaces** to return to earlier work. To open a particular model:

```sh
agent-3d-cad viewer --workspace "/absolute/path/to/CAD workspace" --document mounting_plate
```

Keep the window open while the agent works. Saved revisions appear automatically.
Select a face or edge and copy an edit request into your agent; the agent can
also read `cad_context` for the exact selection and revision. Choose **Export**
for STEP, STL, or 2D drawings. The standalone app uses native save dialogs;
the embedded viewer reports files saved in the workspace. Default drawings use
an A4 sheet with standard views; dimensions and custom layouts are requested
through the agent.

Saved projects are editable model documents. STEP/STL exports are independent
outputs; exporting does not replace the source project. Invalid edits preserve
the last committed revision. Independent chats should choose distinct `--view`
IDs when they need separate selection context.

## Upgrade or remove

Install the next version alongside the old one, close old viewer/server processes,
and update your client's executable path. Keep the same workspace. The current
preview may reject older geometry that does not meet newer validation rules;
it preserves the stored model and reports the feature that needs attention.

To uninstall, remove the MCP entry (or Claude extension) and application directory.
Your workspaces remain until you separately delete them. Tauri remembers recent
workspace paths in its per-user application configuration directory.

## More

- [Client setup and troubleshooting](docs/GETTING_STARTED.md)
- [Viewer and saved projects](docs/LIVE_VIEWER.md)
- [Modeling tools and CLI protocol](docs/PROTOCOL.md)
- [Drawings](docs/DRAWINGS.md) and [assemblies](docs/ASSEMBLIES.md)
- [Build, test, and contribute](docs/DEVELOPMENT.md)
- [Release process and packaging](docs/DISTRIBUTION.md)

The original code is [MIT-licensed](LICENSE). Third-party components keep their
own licenses; see [NOTICE](NOTICE) and [third-party notices](packaging/THIRD_PARTY.md).
