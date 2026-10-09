# Repository and documentation guide

Start with [the product README](../README.md) for installation and everyday use,
[HANDOFF.md](HANDOFF.md) for implementation state and validation evidence, and
[DEVELOPMENT.md](DEVELOPMENT.md) for build and test commands.

## Source layout

| Path | Contents |
|---|---|
| `src/`, `include/agentcad/` | Native C++ service, kernel, persistence and transport implementation and interfaces |
| `web/` | Viewer sources shared by the embedded MCP App and standalone shell |
| `desktop/` | Optional Tauri shell source, Rust manifest, lockfile and application icons |
| `cmake/` | Native build, embedded assets, portable bundles and pinned dependency recipe |
| `tests/` | Native and transport regressions, UI checks, packaging checks and intentional test fixtures |
| `examples/` | Editable model requests, drawings and workflow examples |
| `packaging/` | Install, archive, plugin and release tooling; third-party notice policy |
| `plugins/`, `skills/` | Local plugin metadata and the bundled agent workflows |
| `third_party/` | Vendored TinyXML2 source, license and provenance |
| `docs/` | Product contracts, feature guides, development instructions and historical design plans |
| `.github/` | CI and preview-release workflow |
| Root files | Project/build metadata, installer entry points, contribution/security guidance and licenses |

`web/` is the authoritative viewer source. `desktop/ui/` and `desktop/gen/` are
generated. The ZIP, 3MF, GLB and STL files in `tests/artifact_fixtures/` are
intentional regression inputs and belong in source control.

## Local output layout

These directories are ignored by Git:

| Path | Purpose |
|---|---|
| `build/` | Current native build, executables and disposable test output |
| `build/dependencies/` | Dependency compilation when a new SDK is needed |
| `build/packages/` | Generated archives and plugin packages |
| `.deps/` | Installed dependency SDK, pinned source inputs and optional developer environments |
| `.local/workspaces/` | Saved local demonstration/review workspaces and their revisions |
| `.local/evidence/` | Logs, screenshots, reports and installation/configuration backups worth retaining |
| `.superpowers/` | Temporary browser design sessions, when used |

Keep model workspaces and lasting evidence outside `build/`. Before removing a
build tree, relocate any such data; generated binaries and package copies can be
rebuilt from the recorded source and dependencies. Local configuration backups
may contain private settings and must stay ignored.

The 2026-10-09 cleanup moved earlier workspaces and evidence beneath `.local/`,
retaining their original relative paths. For example,
`build-standalone-studio/demo` is now
`.local/workspaces/build-standalone-studio/demo`, and
`build/shared-ui-20261008/config-before.toml` is now
`.local/evidence/build/shared-ui-20261008/config-before.toml`. The exact inventory,
source-to-destination mapping and SHA-256 hashes are in
`.local/cleanup-20261009.json`. This local archive is not part of a fresh clone.

Historical paths in HANDOFF and earlier design plans describe the original runs.
Workspace files were preserved without rewriting their contents. Reopen a model
with its relocated `--workspace` path; derived outputs containing old absolute
paths may need regeneration.

## Product and architecture

- [1.0 host acceptance](RELEASE_1_0.md), [release notes](RELEASE_NOTES.md) and
  [milestone roadmap](ROADMAP.md)
- [Product and architecture invariants](SPEC.md), [tool/document protocol](PROTOCOL.md)
  and [agent instructions](../AGENTS.md)
- [Development](DEVELOPMENT.md), [distribution](DISTRIBUTION.md) and
  [dependency pins and provenance](DEPENDENCIES.md)
- [Client setup and troubleshooting](GETTING_STARTED.md)

## Modeling and assemblies

- [Assemblies](ASSEMBLIES.md), [revision-pinned components](COMPONENTS.md) and
  [dependency caching](DEPENDENCY_CACHE.md)
- [Modeling and motion scope](MODELING_MOTION.md), [sequence playback](PLAYBACK.md)
  and [robot exports](ROBOT_EXPORT.md)
- [Drawings](DRAWINGS.md) and [sourced purchased parts](PURCHASED_PARTS.md)

## Viewing and inspection

- [Live viewer](LIVE_VIEWER.md) and [external artifact review](ARTIFACT_REVIEW.md)
- [Clipping and exploded inspection](PRESENTATION.md), [exact measurements](MEASUREMENTS.md)
  and [exact sections](SECTIONS.md)
- [Appearance and saved views](APPEARANCE.md) and [review annotations](ANNOTATIONS.md)

## Manufacturing and process handoff

- [Manufacturing packages](MANUFACTURING.md) and [fabrication review](FABRICATION_REVIEW.md)
- [G-code review](GCODE_REVIEW.md), [slicing](SLICING.md) and
  [offline printer handoff](PRINTER_HANDOFF.md)
- [Combined composition, fabrication and review scope](COMPOSITION_FABRICATION_REVIEW.md)

Historical viewer redesign material remains in `docs/superpowers/specs/` and
`docs/superpowers/plans/`. Those documents record design intent; HANDOFF and the
current feature guides record implemented behavior and evidence.
