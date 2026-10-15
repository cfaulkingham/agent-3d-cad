# Install Agent CAD

Choose your host below. Agent CAD runs locally and includes its native CAD engine
and viewer. No Python, Node, Rust or compiler is required.

## Get a package

Download from [Releases](https://github.com/cfaulkingham/agent-3d-cad/releases):

- **Claude Desktop:** use the `.mcpb` for your OS and CPU.
- **Codex / ChatGPT desktop plugin:** use `agent-cad-plugin-…zip` when available.
- **Grok Build, OpenCode and other MCP clients:** use the native
  `agent-3d-cad-VERSION-OS-CPU` archive (`.tar.gz` on macOS/Linux, `.zip` on Windows).

Extract archives to a permanent folder and keep their contents together.
The native executable is `bin/agent-3d-cad` (`bin/agent-3d-cad.exe` on Windows).
Replace `/absolute/path/to/agent-3d-cad` below with that file's full path.
On Windows, use forward slashes in JSON paths, such as `C:/AgentCAD/bin/agent-3d-cad.exe`.

**Version note:** these steps use current source/CI packages, which default to
**Documents/Agent CAD**. For `0.1.0-preview.1`, replace `--default-workspace` with
`--workspace "/absolute/path/to/your/CAD projects"` (two separate entries in JSON
arrays), and select that folder during Claude installation. Local plugin ZIPs are
produced by current CI; do not expect them in the original preview. Historical
standalone archives are no longer needed. See [distribution](DISTRIBUTION.md)
for checksum-verifying installers and package validation.

## Codex / ChatGPT desktop

Install from a folder on your computer. No public marketplace submission or
approval is required.

Extract the local plugin ZIP to a permanent folder, then register that folder:

```sh
codex plugin marketplace add "/absolute/path/to/extracted/agent-cad-plugin-VERSION-OS-CPU"
```

Refresh or restart the desktop host, open its plugin browser, find **Agent CAD**
under **Agent CAD local testing**, and install/enable it. Start a new chat and ask
it to open your CAD library. Keep the extracted folder for future plugin updates.
The word `marketplace` in the command refers to the package's local plugin
catalog. This is the complete local installation route.
[Official local plugin setup](https://developers.openai.com/plugins/build/plugins#how-local-marketplaces-work).

Alternatively, with a native archive, register the MCP server:

```sh
codex mcp add agent-3d-cad -- "/absolute/path/to/agent-3d-cad" serve --default-workspace
codex mcp list
```

Use one route to avoid duplicate CAD tools. Restart the host or start a new session
after registration. The MCP route provides the tools and embedded viewer;
the plugin also installs the bundled skills.
[Official Codex MCP setup](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## Claude Desktop

1. Open **Settings → Extensions → Advanced settings → Install Extension**.
2. Select the downloaded `.mcpb` and complete the prompts.
3. Enable Agent CAD and start a new chat.

The extension includes the native engine and viewer. Its optional project-folder
setting can point to existing projects outside the extension installation.
[Official extension setup](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop).

## Grok Build

After extracting the native archive:

```sh
grok mcp add agent-3d-cad -- "/absolute/path/to/agent-3d-cad" serve --default-workspace
grok mcp doctor agent-3d-cad
```

Start a new session, or refresh the server in `/mcps`.
[Official Grok MCP setup](https://docs.x.ai/build/features/mcp-servers).

## OpenCode

Merge this entry into your `opencode.json` or `opencode.jsonc`, preserving existing
settings, then restart OpenCode:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "agent-3d-cad": {
      "type": "local",
      "command": ["/absolute/path/to/agent-3d-cad", "serve", "--default-workspace"],
      "enabled": true
    }
  }
}
```

This follows the [current OpenCode MCP guide](https://opencode.ai/docs/mcp-servers/).
If using the separate OpenCode V2 preview, follow its
[V2 MCP guide](https://dev.opencode.ai/v2/docs/mcp-servers/): place the server under
`mcp.servers` and omit `enabled`.

## Other agents

Add a **local / stdio MCP server** using these fields:

| Field | Value |
|---|---|
| Command | Full path to `bin/agent-3d-cad` (`.exe` on Windows) |
| Arguments | `serve`, `--default-workspace` |

Use your client's own MCP settings format. For Claude-compatible JSON,
`agent-3d-cad config --client claude --workspace PATH` prints a ready-to-merge
entry; `--client codex` and `--client opencode` generate their respective formats.
These commands print settings without changing them. Point your agent to the
bundled `share/agent-3d-cad/skills/native-cad/SKILL.md` for modeling guidance.

## Try it

> Create an editable 80 × 50 × 6 mm plate with four mounting holes. Save it as
> mounting_plate, show it, then make it 8 mm thick and export STEP.

In a host with MCP Apps, select a face or edge in the viewer and describe your
edit in the main chat. Other clients can ask for an offline HTML view (`cad_view`)
or PNG (`cad_render`). Offline views are snapshots; regenerate them after edits.

## Existing projects and troubleshooting

- **Missing projects:** replace `--default-workspace` with `--workspace` and the
  full path to your existing project folder. Use that same folder in every host.
- **No tools:** check the executable path, OS/CPU package and the client's MCP
  connection status, then restart its server/session.
- **No inline viewer:** tools require MCP; an interactive viewer additionally
  requires MCP Apps. Ask for offline HTML or PNG if the host lacks it.
- **Upgrade or remove:** install the new package separately and update the host's
  connection. Keep projects outside installation folders so they survive removal.
- **OS blocks a preview:** publisher signing/notarization remain unfinished.
  Installers do not remove quarantine or other OS protections.
- **ChatGPT web:** a local executable path cannot connect this service to the web
  app. This project ships local stdio; a remote connection is a separate setup.

Host configuration references checked 2026-10-15. Grok Build and OpenCode setup
is documentation-based; full host acceptance remains separate. See
[HANDOFF](HANDOFF.md) for executed tests and [1.0 acceptance](RELEASE_1_0.md).
