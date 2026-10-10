"""Package the native engine as a local Agent Plugins bundle; no Tauri runtime."""
import json
from pathlib import Path
import shutil
import sys
import zipfile

repo = Path(__file__).resolve().parent.parent
root, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
from bundle_validation import MANIFEST, sha, validate_bundle
manifest_path = MANIFEST
version = (repo / 'VERSION').read_text(encoding='utf-8').strip()
try:
    provenance, system, arch, binary = validate_bundle(root, version)
except (ValueError, OSError, KeyError) as error:
    raise SystemExit(str(error)) from error
name = f'agent-cad-plugin-{version}-{system}-{arch}'
destination = output / name
archive_path = output / (name + '.zip')
if destination.exists() or archive_path.exists():
    raise SystemExit('Plugin output already exists')
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
shutil.copytree(root / 'share/agent-3d-cad/skills', destination / 'skills', dirs_exist_ok=True)
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
