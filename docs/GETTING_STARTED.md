# Connect Agent CAD

Install a release bundle first (see the [README](../README.md)). Replace the
executable and workspace paths below with absolute paths. Use the same workspace
in every agent and viewer that should share your projects.

## ChatGPT desktop and Codex

In Settings → MCP servers → Add server, choose **STDIO**, then enter:

- Command: the installed `bin/agent-3d-cad` (`.exe` on Windows)
- Arguments: `serve`, `--workspace`, `/absolute/path/to/CAD workspace`

Save and restart the server. Alternatively, register with Codex CLI:

```sh
codex mcp add agent-3d-cad -- /absolute/install/bin/agent-3d-cad serve --workspace "/absolute/path/to/CAD workspace"
codex mcp get agent-3d-cad
```

To configure by hand, `agent-3d-cad config --client codex --workspace PATH` prints
a TOML block for `~/.codex/config.toml`. Merge the block; do not replace your whole
configuration. The current desktop and local Codex clients share the configuration
for the same host. [Official MCP setup](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

## Claude Desktop

Download the `.mcpb` matching your operating system **and CPU architecture**.
In Settings → Extensions → Advanced settings → Install extension, select it and
choose a persistent CAD workspace. Keep that folder outside the extension's own
installation. This uses the bundled native binary directly.
[Claude extension installation](https://support.claude.com/en/articles/10949351-getting-started-with-local-mcp-servers-on-claude-desktop).

Manual alternative: run `agent-3d-cad config --client claude --workspace PATH`.
Merge the generated `mcpServers.agent-3d-cad` entry into the configuration opened
by Claude's developer settings, then restart Claude. Typical locations are
`~/Library/Application Support/Claude/claude_desktop_config.json` on macOS and
`%APPDATA%\Claude\claude_desktop_config.json` on Windows.

The extension contains the CAD service and embedded MCP App. For a separate
desktop window as well, install the desktop archive and point it at the same
workspace. User workspaces survive extension replacement/removal.

## OpenCode desktop and CLI

Run `agent-3d-cad config --client opencode --workspace PATH` and merge its `mcp`
entry into your `opencode.json`. It uses OpenCode's local-server command array:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "mcp": {
    "agent-3d-cad": {
      "type": "local",
      "command": ["/absolute/install/bin/agent-3d-cad", "serve", "--workspace", "/absolute/CAD workspace"],
      "enabled": true
    }
  }
}
```

Use the standalone viewer if your client does not render MCP Apps.
[OpenCode MCP configuration](https://opencode.ai/docs/mcp-servers/).

## Grok Build, Muse, and other CLI agents

Use local stdio MCP if the client supports it: command = the native executable,
arguments = `serve --workspace ABSOLUTE_PATH`. Client-specific configuration names
and compatibility for these clients have not been validated; do not assume the
Claude JSON format applies to every client.

An agent with shell access can use the same service directly:

```sh
agent-3d-cad tools
agent-3d-cad call cad_list --workspace "/absolute/CAD workspace" --input - <<'JSON'
{}
JSON
agent-3d-cad viewer --workspace "/absolute/CAD workspace" --view main
```

Give the agent the bundled `share/agent-3d-cad/skills/native-cad/SKILL.md` for
modeling guidance. The viewer exposes selection and camera through `cad_context`;
the agent edits through `cad_apply`, and the window follows committed revisions.

## Reference faces and edges from chat

Open the viewer with the same absolute workspace used in your MCP settings.
Click a face or edge, then ask: “Use the selection in `cad_context` for view
`main`.” The agent receives exact revision-qualified geometry, resolves it with
`cad_resolve_selection`, and applies a supported edit. For separate concurrent
chats, launch with `--view my_chat` and name `my_chat` in the agent’s tool calls.

**Copy request** carries the selection and workspace into any chat. This is the
standalone window’s handoff; embedded MCP Apps can also use **Send to chat** when
the host supports it. Selecting geometry does not itself submit an edit request
or identify which chat should receive one. Saved edits refresh the open window.

## ChatGPT web

Web connections are a separate setup. OpenAI documents a public HTTPS MCP endpoint
or Secure MCP Tunnel, which can reach a configured stdio server. This project
ships local stdio, not a hosted authenticated MCP endpoint or an automatic tunnel
installer. A local executable path or the viewer window alone does not make it
available to ChatGPT web. Follow the current account/workspace connection flow
and verify the tools before relying on it.
[OpenAI connection guidance](https://developers.openai.com/plugins/deploy/connect-chatgpt).

## Viewer and troubleshooting

- `viewer_not_installed`: install the **desktop** archive. The core and `.mcpb`
  downloads contain the service and embedded viewer, not the Tauri executable.
- Empty library: the agent and viewer must use the same workspace. Use **Open
  workspace** to select an older project folder; **Recent workspaces** remembers it.
- No inline viewer: MCP tools and MCP Apps rendering are different capabilities.
  Use the standalone window, or `cad_view` for a self-contained offline HTML file.
- No direct chat delivery: standalone Quick Edit uses **Copy request**. Paste it
  into the agent, or ask the agent to read `cad_context` for the current view.
- Linux desktop: install your distribution's GTK 3 / WebKitGTK 4.1 runtime packages
  (`sudo apt install libgtk-3-0t64 libwebkit2gtk-4.1-0` on Ubuntu 24.04). Core
  service bundles do not require those GUI libraries.
- Windows desktop: install Microsoft's WebView2 Evergreen Runtime if it is absent.
  It is normally present on Windows 11. The portable archive does not install it.
- Unsigned preview blocked by the OS: current builds have no publisher signing
  or notarization. This is a release limitation; installers do not remove OS
  security protections or quarantine flags.

Configuration examples are based on the linked documentation checked 2026-10-07.
They do not establish successful end-to-end integration in every named client.
Executed native, Tauri, and MCP-host evidence is recorded in [HANDOFF](HANDOFF.md).
