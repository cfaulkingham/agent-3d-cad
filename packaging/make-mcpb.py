"""Wrap a verified native bundle for Claude Desktop (developer tool only)."""
import json
from pathlib import Path
import sys
import zipfile

root, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
from bundle_validation import validate_bundle
version = (Path(__file__).resolve().parent.parent / 'VERSION').read_text(encoding='utf-8').strip()
try:
    provenance, system, arch, binary = validate_bundle(root, version)
except (ValueError, OSError, KeyError) as error:
    raise SystemExit(str(error)) from error
if system not in ('Darwin', 'Windows'):
    raise SystemExit('Claude Desktop packages are built only for macOS and Windows')
manifest = {
    'manifest_version': '0.3', 'name': 'agent-3d-cad', 'display_name': 'Agent CAD',
    'version': provenance['project_version'], 'description': 'Create, edit, view and export saved CAD projects.',
    'author': {'name': 'Colin Faulkingham'}, 'license': 'MIT', 'tools_generated': True,
    'homepage': 'https://github.com/cfaulkingham/agent-3d-cad',
    'server': {'type': 'binary', 'entry_point': binary, 'mcp_config': {
        'command': '${__dirname}/' + binary, 'args': ['serve', '--default-workspace', '--workspace-setting', '${user_config.workspace}']}},
    'compatibility': {'platforms': ['darwin' if system == 'Darwin' else 'win32']},
    # Informational provenance only: MCPB 0.3 has no CPU selection constraint.
    '_meta': {'org.agentcad.native': {'system': system, 'architecture': arch,
        'architecture_selection': 'native_universal' if arch == 'universal' else 'distribution_required'}},
    'user_config': {'workspace': {'type': 'directory', 'title': 'Project folder (optional)',
        'description': 'Ready to use in Documents/Agent CAD. Change this only to reopen an existing CAD workspace. Projects remain when the extension is updated or removed.',
        # Claude 2.26454.2 substitutes this value into args but does not expand
        # a nested ${DOCUMENTS} default. An empty value lets the native engine
        # resolve the OS folder without host-specific placeholder expansion.
        'required': False, 'default': ''}}
}
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('manifest.json', json.dumps(manifest, indent=2))
    for file in sorted(root.rglob('*')):
        if file.is_file():
            if not file.resolve().is_relative_to(root):
                raise SystemExit(f'Escaping bundle symlink: {file}')
            # Store regular-file bytes and native mode; no symlink support needed in the host.
            archive.write(file, file.relative_to(root).as_posix())
print(output)
