# Contributing

This is a native preview and the original-code license is still an owner decision
(see [NOTICE](NOTICE)); please open an issue to discuss a change before sending one.

- Read [AGENTS.md](AGENTS.md) (invariants), [docs/SPEC.md](docs/SPEC.md) and
  [docs/PROTOCOL.md](docs/PROTOCOL.md) first.
- Build and test with the commands in [README.md](README.md). Every behavior
  change needs a test that fails without it; do not weaken existing tests.
- Geometry stays in `src/kernel.cpp`; MCP and CLI both call `Service`.
- Update [docs/HANDOFF.md](docs/HANDOFF.md) with exact evidence for meaningful work.
- Keep POSIX and Windows implementations in step.
