# Contributing

This is a native preview. The project is MIT-licensed (see [LICENSE](LICENSE) and
[NOTICE](NOTICE)); by contributing you agree your contribution is under the same
license. Please open an issue to discuss a change before sending one.

- Read [AGENTS.md](AGENTS.md) (invariants), [docs/SPEC.md](docs/SPEC.md) and
  [docs/PROTOCOL.md](docs/PROTOCOL.md) first.
- Use the [repository and documentation guide](docs/README.md) to find the
  relevant code, contracts and local output directories.
- Build and test with the commands in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Every behavior
  change needs a test that fails without it; do not weaken existing tests.
- Geometry stays in `src/kernel.cpp`; MCP and CLI both call `Service`.
- Update [docs/HANDOFF.md](docs/HANDOFF.md) with exact evidence for meaningful work.
- Keep POSIX and Windows implementations in step.
