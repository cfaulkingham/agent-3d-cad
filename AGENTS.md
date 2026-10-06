# agent-3d-cad agent instructions

Build an editable CAD service for agents. Users run a packaged native service;
they do not install Python, Rust, Node, or a compiler to create/edit models.
Developer builds may use CMake, a C++ compiler, and native dependencies.

## Start here

1. Read `docs/HANDOFF.md` for the actual implementation state and next task.
2. Read `docs/SPEC.md` for product/architecture invariants.
3. Read `docs/PROTOCOL.md` before changing tool or document contracts.
4. Read `docs/ROADMAP.md` for milestone acceptance gates.
5. Build and run the relevant CTest suites; commands are in `README.md`.

## Implementation rules

- C++20 service; OpenCascade **8.0.1**, verified latest stable on 2026-10-06.
  Pin releases and archive hashes. No floating OCCT branch, OCCT 7 fallback, or
  dependency on sibling repositories. Upgrade only with recorded release evidence
  and geometry regression results. `docs/DEPENDENCIES.md` is the upgrade checklist.
- Keep geometry inside `kernel.cpp` behind `BuiltModel`. Service and transport
  deal with documents and JSON, never exposed OCCT pointers or transient indices.
- MCP and CLI call the same `Service`. Do not duplicate behavior in adapters.
- A model document is the editable source; STEP/STL are independent outputs.
  Preserve units, kernel version, feature IDs, and committed revisions.
- Build and validate before publishing HEAD. Failed modeling must not mutate
  an existing revision. Check `expected_revision` while holding the writer lock.
- Do not claim stable face/edge naming from enumeration indices. Missing or
  ambiguous design references must fail explicitly.
- Do not execute model-supplied code, shell commands, or compiler invocations.
- Stdout is JSON only in MCP mode. OCCT diagnostics go to stderr.
- Use feature-level errors. Do not silently remove failed features, repair
  geometry in a way that changes intent, or substitute meshes for exact solids.
- Kernel operations are serial inside bounded worker processes. Never share OCCT
  state between threads. Watchdogs only monitor resources; publication stays in
  coordinators. Maintain both POSIX and Windows storage/process implementations.
- Validate geometry within tolerances, not STEP byte equality. Tests use isolated
  temporary workspaces and no hardcoded user paths. Never weaken tests to make
  a compatibility claim that has not been verified.
- Update `HANDOFF.md` after meaningful work: completed behavior, exact test evidence,
  limitations, next tasks, and decisions. Mark planned APIs as planned.
- Keep new code independent of build123d's Python semantics. Reusing sibling code
  requires a specific technical reason and preservation of applicable notices.

No release or remote repository has been created by the initial bootstrap.
License selection for original project code is still an owner decision; do not
infer it from neighboring repositories.
