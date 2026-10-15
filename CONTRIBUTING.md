# Contributing

This is a native preview. The project's original code is MIT-licensed (see
[LICENSE](LICENSE) and [NOTICE](NOTICE); third-party components, notably
Open CASCADE Technology under LGPL 2.1 with the Open CASCADE exception, keep
their own licences, so distributed bundles are not MIT-licensed as a whole).
By contributing original code you agree your contribution is under the MIT
License. Please open an issue to discuss a change before sending one.

- Read [AGENTS.md](AGENTS.md) (invariants), [docs/SPEC.md](docs/SPEC.md) and
  [docs/PROTOCOL.md](docs/PROTOCOL.md) first.
- Use the [repository and documentation guide](docs/README.md) to find the
  relevant code, contracts and local output directories.
- Build and test with the commands in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Every behavior
  change needs a test that fails without it; do not weaken existing tests.
- Geometry stays in `src/kernel.cpp`; MCP and CLI both call `Service`.
- Update [docs/HANDOFF.md](docs/HANDOFF.md) with exact evidence for meaningful work.
- Keep POSIX and Windows implementations in step.
