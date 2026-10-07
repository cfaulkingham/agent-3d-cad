# Agent CAD 0.1.0-preview.1

Local editable CAD through MCP and a native CLI, with a standalone Tauri viewer.

- Open saved projects, search models, and return to recent workspaces.
- Pick faces and edges, share revision-qualified references with an agent through
  `cad_context` or Copy request, and follow committed edits in the same window.
- Export STEP, STL, and native PDF/SVG/DXF drawings using the viewer.
- Generate client configuration for ChatGPT desktop/Codex, Claude and OpenCode.
- Install versioned core/desktop archives with SHA-256 checks, or use the
  platform-specific Claude Desktop `.mcpb` extension.

Preview limitations: packages are not publisher-signed or notarized. Tauri uses
the system webview; Windows requires WebView2 and Linux requires GTK 3/WebKitGTK
4.1. Native core runtime requirements remain macOS 15+, glibc 2.39+ Linux, and
Windows x64. The release workflow builds five platforms; review the attached
CI evidence and HANDOFF for actual desktop/host validation before publishing.
Standalone Copy request does not automatically post into a chat. Default drawings
are standard A4 views without inferred dimensions. ChatGPT web needs its separate
MCP connection/tunnel setup.

Keep workspaces outside application/extension directories when upgrading.
Exact geometry remains OpenCascade 8.0.1; kernel/source notices and complete Rust
dependency source archives accompany the applicable bundles.
