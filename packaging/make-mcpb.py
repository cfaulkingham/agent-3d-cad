"""Wrap a verified native bundle for Claude Desktop (developer tool only)."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

root, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
provenance = json.loads((root / 'share/agent-3d-cad/provenance.json').read_text())
paths = [entry['path'] for entry in provenance['files']]
actual = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
if not paths or len(paths) != len(set(paths)) or actual != set(paths) | {'share/agent-3d-cad/provenance.json'}:
    raise SystemExit('Bundle provenance must cover every file exactly once')
system = provenance['system']
if system not in ('Darwin', 'Windows'):
    raise SystemExit('Claude Desktop packages are built only for macOS and Windows')
binary = 'bin/agent-3d-cad' + ('.exe' if system == 'Windows' else '')
for entry in provenance['files']:
    file = (root / entry['path']).resolve()
    if not file.is_relative_to(root) or hashlib.sha256(file.read_bytes()).hexdigest() != entry['sha256']:
        raise SystemExit(f"Bundle integrity failure: {entry['path']}")
manifest = {
    'manifest_version': '0.3', 'name': 'agent-3d-cad', 'display_name': 'Agent CAD',
    'version': provenance['project_version'], 'description': 'Create, edit, view and export saved CAD projects.',
    'author': {'name': 'Colin Faulkingham'}, 'license': 'MIT', 'tools_generated': True,
    'homepage': 'https://github.com/cfaulkingham/agent-3d-cad',
    'server': {'type': 'binary', 'entry_point': binary, 'mcp_config': {
        'command': '${__dirname}/' + binary, 'args': ['serve', '--default-workspace', '--workspace-setting', '${user_config.workspace}']}},
    'compatibility': {'platforms': ['darwin' if system == 'Darwin' else 'win32']},
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
