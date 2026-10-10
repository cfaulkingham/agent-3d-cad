"""Package the native engine as a local Agent Plugins bundle; no Tauri runtime."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

repo = Path(__file__).resolve().parent.parent
root, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
manifest_path = Path('share/agent-3d-cad/provenance.json')
provenance = json.loads((root / manifest_path).read_text(encoding='utf-8'))
paths = [entry['path'] for entry in provenance['files']]
actual = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
if not paths or len(paths) != len(set(paths)) or actual != set(paths) | {manifest_path.as_posix()}:
    raise SystemExit('Bundle provenance must cover every file exactly once')
if 'desktop' in provenance or (root / 'desktop').exists():
    raise SystemExit('Plugins must use the core native bundle, without a standalone viewer')
for file in root.rglob('*'):
    if file.is_symlink() and (file.is_dir() or not file.resolve().is_relative_to(root)):
        raise SystemExit(f'Unsupported bundle link: {file}')
def sha(file):
    with file.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
for entry in provenance['files']:
    file = (root / entry['path']).resolve()
    if not file.is_relative_to(root) or sha(file) != entry['sha256']:
        raise SystemExit(f"Bundle integrity failure: {entry['path']}")
version = (repo / 'VERSION').read_text(encoding='utf-8').strip()
if provenance['project_version'] != version:
    raise SystemExit('Native bundle must match VERSION')
system = provenance['system']
arch = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'x64', 'AMD64': 'x64'}.get(provenance['architecture'])
if system not in ['Darwin', 'Windows', 'Linux'] or arch is None:
    raise SystemExit('Unsupported native platform')
name = f'agent-cad-plugin-{version}-{system}-{arch}'
destination = output / name
archive_path = output / (name + '.zip')
if destination.exists() or archive_path.exists():
    raise SystemExit('Plugin output already exists')
binary = 'bin/agent-3d-cad' + ('.exe' if system == 'Windows' else '')
if not (root / binary).is_file():
    raise SystemExit('Native executable is missing')
shutil.copytree(root, destination, symlinks=False)
plugin = json.loads((repo / 'plugins/agent-cad/plugin.json').read_text(encoding='utf-8'))
plugin['version'] = version
(destination / 'plugin.json').write_text(json.dumps(plugin, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
mcp = json.loads((repo / 'plugins/agent-cad/mcp.json').read_text(encoding='utf-8'))
mcp['mcpServers']['agent-3d-cad']['command'] = './' + binary
(destination / 'mcp.json').write_text(json.dumps(mcp, indent=2) + '\n', encoding='utf-8')
catalog = destination / '.agents/plugins/marketplace.json'
catalog.parent.mkdir(parents=True)
catalog.write_text(json.dumps({
    'name': 'agent-cad-local', 'interface': {'displayName': 'Agent CAD local testing'},
    'plugins': [{'name': plugin['name'], 'source': {'source': 'local', 'path': '.'},
        'policy': {'installation': 'AVAILABLE', 'authentication': 'ON_INSTALL'}, 'category': 'Productivity'}]
}, indent=2) + '\n', encoding='utf-8')
shutil.copytree(repo / 'plugins/agent-cad/skills', destination / 'skills')
shutil.copytree(root / 'share/agent-3d-cad/skills/native-cad', destination / 'skills/native-cad')
(destination / 'assets').mkdir()
shutil.copy2(repo / 'desktop/icons/icon.png', destination / 'assets/icon.png')
for notice in ['LICENSE', 'NOTICE']:
    shutil.copy2(repo / notice, destination / notice)
provenance['plugin'] = {'format': 'Agent Plugins', 'format_version': '1.0.0', 'viewer': 'embedded MCP App', 'transport': 'stdio'}
provenance['files'] = [{'path': file.relative_to(destination).as_posix(), 'sha256': sha(file)}
    for file in sorted(destination.rglob('*')) if file.is_file() and file.relative_to(destination) != manifest_path]
(destination / manifest_path).write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
with zipfile.ZipFile(archive_path, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
    for file in sorted(destination.rglob('*')):
        if file.is_file():
            archive.write(file, name + '/' + file.relative_to(destination).as_posix())
print(archive_path)
