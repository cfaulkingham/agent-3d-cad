# Security policy

agent-3d-cad parses untrusted input: model documents and tool arguments (JSON),
embedded STEP files, workspace files, and MCP/CLI requests from agents. It is a
native preview with no supported release yet.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability. Use GitHub's
private vulnerability reporting for this repository (Security tab → "Report a
vulnerability"), or contact the repository owner through their GitHub profile.
Include a minimal reproducing document or request, the platform, and the
`agent-3d-cad --version` output.

## Scope and design boundaries

- Models contain structured intent only; the service never executes
  model-supplied code, shell commands or compiler invocations.
- Document identifiers, workspace paths and symlinks are validated; imports read
  only the file the caller names.
- Geometry runs in bounded worker processes with time and memory limits.
- The service opens no network listener.

Reports about crashes, hangs or unbounded resource use on crafted JSON/STEP input,
path traversal, symlink escapes, or lock/revision-integrity bypasses are in scope.
