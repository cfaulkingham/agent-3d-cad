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
Windows x64. All five platforms passed the tagged
[release workflow](https://github.com/cfaulkingham/agent-3d-cad/actions/runs/37691102208):
32 native suites and two Tauri integration tests per platform, plus schema/MCP,
installer, relocation and packaging checks. Both Linux runtime-only checks passed.
The downloaded Apple Silicon package also passed native-window rendering,
face/edge selection sharing, revision refresh and native STEP Save-dialog checks.
Actual Windows/Linux GUI and Claude/OpenCode installation trials remain unverified.
Standalone Copy request does not automatically post into a chat. Default drawings
are standard A4 views without inferred dimensions. ChatGPT web needs its separate
MCP connection/tunnel setup.

Keep workspaces outside application/extension directories when upgrading.
Exact geometry remains OpenCascade 8.0.1; kernel/source notices and complete Rust
dependency source archives accompany the applicable bundles.

Release preparation: the Claude bundles were rebuilt from the verified core
archives to restore their file-provenance manifests; every other contained file
is byte-identical to the CI-built extension. All 15 assets passed release checksum
validation, and all 13 packages passed full file-provenance and architecture checks.
The release remains based on tagged source `2065a69`; follow-up packaging guards
on main prevent empty inventories in future builds. See `docs/HANDOFF.md` for
the exact evidence and remaining host-validation work.
