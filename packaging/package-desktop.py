"""Assemble a Tauri window plus a verified native CAD bundle. Developer-only."""
import hashlib
import json
import platform
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
native, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
binary = Path(sys.argv[3]).resolve()
version = (repo / 'VERSION').read_text(encoding='utf-8').strip()
system = platform.system()
arch = {'aarch64': 'arm64', 'arm64': 'arm64', 'x86_64': 'x64', 'AMD64': 'x64'}[platform.machine()]
name = f'agent-3d-cad-desktop-{version}-{system}-{arch}'
destination = output / name
manifest_path = Path('share/agent-3d-cad/provenance.json')
manifest = json.loads((native / manifest_path).read_text(encoding='utf-8'))
if manifest['project_version'] != version or manifest['system'] != system:
    raise SystemExit('Native bundle version/platform mismatch')
def sha(file):
    with file.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
for item in manifest['files']:
    file = (native / item['path']).resolve()
    if not file.is_relative_to(native) or sha(file) != item['sha256']:
        raise SystemExit(f"Native integrity failure: {item['path']}")
if destination.exists():
    raise SystemExit(f'Output already exists: {destination}')
shutil.copytree(native, destination, symlinks=True)
shell = destination / 'desktop'
shell.mkdir()
if system == 'Darwin':
    app = shell / 'Agent CAD.app/Contents'
    (app / 'MacOS').mkdir(parents=True)
    shutil.copy2(binary, app / 'MacOS/agent-cad-viewer')
    info = {'CFBundleName': 'Agent CAD', 'CFBundleDisplayName': 'Agent CAD',
            'CFBundleIdentifier': 'com.agentcad.viewer', 'CFBundleExecutable': 'agent-cad-viewer',
            'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': version.split('-')[0],
            'CFBundleVersion': version.split('-')[0], 'LSMinimumSystemVersion': '15.0',
            'NSHighResolutionCapable': True}
    (app / 'Info.plist').write_bytes(plistlib.dumps(info))
    subprocess.run(['codesign', '--force', '--sign', '-', str(app.parent)], check=True)
else:
    shutil.copy2(binary, shell / ('agent-cad-viewer.exe' if system == 'Windows' else 'agent-cad-viewer'))
    if system == 'Windows':
        # A GUI launched directly also needs the shipped MSVC runtime beside it.
        for file in (native / 'bin').glob('*.dll'):
            if file.name.lower().startswith(('vcruntime', 'msvcp', 'concrt')):
                shutil.copy2(file, shell / file.name)

# Preserve complete, checksum-locked Rust dependency sources and all notices.
# Crate archives remain compressed; this also covers inline copyright headers.
host = subprocess.check_output(['rustc', '-vV'], text=True, encoding='utf-8').split('host: ')[1].splitlines()[0]
metadata = json.loads(subprocess.check_output(['cargo', 'metadata', '--manifest-path', str(repo / 'desktop/Cargo.toml'),
    '--locked', '--offline', '--filter-platform', host, '--format-version', '1'], text=True, encoding='utf-8'))
resolved = {node['id'] for node in metadata['resolve']['nodes']}
notices = destination / 'share/agent-3d-cad/notices/tauri'
sources = notices / 'sources'
sources.mkdir(parents=True)
index = []
for package in metadata['packages']:
    if package['id'] not in resolved or not package['source']:
        continue
    if not package['source'].startswith('registry+'):
        raise SystemExit('Desktop dependencies must be checksum-locked registry crates')
    source = Path(package['manifest_path']).parent
    archive = source.parents[2] / 'cache' / source.parent.name / (source.name + '.crate')
    target = sources / archive.name
    shutil.copy2(archive, target)
    index.append({'name': package['name'], 'version': package['version'], 'license': package['license'],
                  'repository': package['repository'], 'source_archive': 'sources/' + target.name, 'sha256': sha(target)})
(notices / 'dependencies.json').write_text(json.dumps(index, indent=2) + '\n', encoding='utf-8')
shutil.copy2(repo / 'desktop/Cargo.lock', notices / 'Cargo.lock')
(notices / 'README.txt').write_text('Tauri desktop dependencies\n\nComplete original Cargo registry source archives, including their license and\ncopyright notices, are preserved in sources/. Each .crate is a gzip tar archive.\ndependencies.json records the declared license and archive hash; Cargo.lock pins\nversions and registry checksums. These sources are not needed at runtime.\nThe OS supplies WKWebView (macOS), WebView2 (Windows), or WebKitGTK (Linux).\n', encoding='utf-8')
manifest['desktop'] = {'framework': 'Tauri', 'version': '2.12.1', 'transport': 'stdio', 'webview': 'system'}
manifest['files'] = []
for file in sorted(destination.rglob('*')):
    if file.is_file() and file.relative_to(destination) != manifest_path:
        if not file.resolve().is_relative_to(destination):
            raise SystemExit(f'Escaping bundle link: {file}')
        manifest['files'].append({'path': file.relative_to(destination).as_posix(), 'sha256': sha(file)})
(destination / manifest_path).write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
extension = 'zip' if system == 'Windows' else 'tar.gz'
archive = output / f'{name}.{extension}'
subprocess.run(['cmake', '-E', 'tar', 'cf' if system == 'Windows' else 'czf', str(archive),
                '--format=zip' if system == 'Windows' else '--format=gnutar', '--', name], cwd=output, check=True)
print(archive)
