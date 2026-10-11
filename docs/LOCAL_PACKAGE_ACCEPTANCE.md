# Local native package acceptance

The developer checks below use isolated directories and explicit temporary
workspaces. They do not register a plugin, modify a real host application, install
into global application folders, publish a release, or establish host GUI readiness.
The release gates remain in [RELEASE_1_0.md](RELEASE_1_0.md).

The native bundle and installed skill carry the same curated product guides,
including parametric expressions, parity operations, mesh reconstruction, and
export downloads. Markdown file links resolve in the archive and plugin layouts.
Product guides are copied unchanged. `HANDOFF.md` is an explicit repository-ledger
reference, with a configure-time commit link when the verified upstream is known;
that commit may be unpublished and does not claim the latest repository state.
The HLR patch reference retains its relative location. The package provenance
inventory hashes these files along with the binaries and dependency notices.

Both plugin packagers require a complete verified inventory and matching VERSION.
They compare native executable and shared-library headers with declared OS/CPU:
64-bit ELF for Linux, thin or derived universal 64-bit Mach-O for macOS, and PE32+
for Windows. Supported release pairs are Linux/macOS arm64/x64, macOS universal,
and Windows x64. This rejects mislabeled
or mixed-CPU bundles before archive creation. Header inspection is a packaging
consistency check; target-OS execution, dependency loading, signing and acceptance
remain separate checks.

`packaging/compose-macos-universal.py` combines two verified core Mac bundles.
Every executable and dylib must contain exactly baseline ARM64 and Intel x64
slices. Both inputs must have the same deterministic production source manifest,
bound to a successfully linked executable; packaging rejects changed source or
a substituted/stale binary. Common resources must match byte for byte. The
composer preserves both original provenance and patched HLR SDK records, each
embedded cache identity, native dependencies and deployment targets. It checks
section and unsigned-payload hashes across `lipo` and ad-hoc re-signing, then
publishes a new complete inventory. Failed composition publishes no output.
Ad-hoc signing is local execution evidence, not publisher signing/notarization.
The one combined executable lets macOS select its native slice; no CPU launcher
or end-user language runtime is included.

The optional preview download installer selects the native arm64 release when an
x64 shell reports Rosetta translation, following Apple's
[documented translation flag](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment).
Offline fixtures exercise supported POSIX architecture aliases, translated and
native macOS selection, baseline rejections, immutable version installation, and
checksum failures. These fixtures do not execute foreign-platform binaries.

The Claude archive records CPU identity in namespaced informational metadata.
[MCPB 0.3](https://github.com/modelcontextprotocol/mcpb/blob/main/MANIFEST.md)
defines OS compatibility and overrides but no CPU compatibility selector. The
metadata does not add host enforcement: thin distribution must select the correct
archive. A verified universal bundle records `architecture_selection:
native_universal` and uses the same binary entry point on both Mac CPUs.
Actual ChatGPT plugin distribution and CPU routing likewise require host evidence.

Developer commands, run from a built checkout:

```sh
python3 tests/package_architecture_tests.py
python3 tests/source_identity_tests.py
python3 tests/universal_validation_tests.py
# macOS only: real lipo/codesign fixtures, never executed
python3 tests/universal_composer_tests.py
python3 tests/packaging_provenance_test.py
python3 tests/download_installer_test.py
ctest --test-dir build -R '^installer$' --output-on-failure
cmake -DBUILD_DIR="$PWD/build" -DTEST_DIR="$PWD/build/bundle-smoke" -P tests/bundle_smoke.cmake
cmake --install build --config Release --prefix "$PWD/build/package-audit/bundle"
python3 tests/package_documentation_test.py build/package-audit/bundle .
python3 packaging/make-plugin.py build/package-audit/bundle build/package-audit/plugins
python3 packaging/make-mcpb.py build/package-audit/bundle build/package-audit/claude.mcpb
```

With independently built source-stamped thin core bundles, compose and package:

```sh
python3 packaging/compose-macos-universal.py ARM64_CORE X64_CORE NEW_UNIVERSAL_CORE
python3 packaging/make-plugin.py NEW_UNIVERSAL_CORE NEW_PLUGIN_OUTPUT
python3 packaging/make-mcpb.py NEW_UNIVERSAL_CORE NEW_CLAUDE_OUTPUT.mcpb
```

Synthetic header tests and real compiler/linker fixtures establish rejection and
payload preservation, not CAD runtime acceptance. The same combined archives
must pass relocated no-SDK modeling, worker/cache identity, STEP/resource
readback, rollback and isolated lifecycle checks on real ARM and Intel Mac
runners. Executed evidence and any pending CI results remain in HANDOFF.

Pass the resulting plugin directory to `tests/plugin_smoke.py` and
`tests/package_documentation_test.py`. Pass that directory and, on macOS/Windows,
the Claude archive to `tests/package_lifecycle_test.py`. The lifecycle check starts
native processes with an empty PATH and explicit isolated workspace, replaces the
package with another copy of the same build, removes both copies, reinstalls, and
verifies source/history/export preservation and failed-edit rollback. It also
expands the Claude manifest into a local native MCP process. This establishes
same-build package replacement and file persistence, not cross-version migration
or a real host-managed update/uninstall.

Still required for release: actual ChatGPT and Claude install/restart/reopen,
embedded viewer selection/editing, host-mediated download/save of exported bytes,
host-managed update/remove/reinstall, verified per-OS/CPU execution, and signing,
notarization and distribution acceptance. CI matrix definitions alone are not
passing evidence. No host acceptance result is inferred from local native tests.
