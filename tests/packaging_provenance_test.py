"""Packaging must reject incomplete inventories before creating artifacts."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

repo = Path(__file__).resolve().parent.parent
version = (repo / 'VERSION').read_text().strip()
with tempfile.TemporaryDirectory(prefix='cad-packaging-provenance-') as temp:
    root = Path(temp)
    bundle = root / 'bundle'
    binary = bundle / 'bin/agent-3d-cad'
    binary.parent.mkdir(parents=True)
    binary.write_bytes(struct.pack('<II', 0xFEEDFACF, 0x0100000C) + bytes(248))
    binary.chmod(0o755)
    manifest = bundle / 'share/agent-3d-cad/provenance.json'
    manifest.parent.mkdir(parents=True)
    entry = {'path': 'bin/agent-3d-cad', 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}

    def save(files):
        manifest.write_text(json.dumps({'project_version': version, 'system': 'Darwin', 'architecture': 'arm64', 'files': files}))

    def run(helper, output):
        args = [sys.executable, str(repo / 'packaging' / helper), str(bundle), str(output)]
        if helper == 'package-desktop.py':
            args.append(str(binary))
        return subprocess.run(args, capture_output=True, text=True, encoding='utf-8')

    for case, files in [('empty', []), ('duplicate', [entry, entry]),
                        ('missing', [entry, {'path': 'missing', 'sha256': '0' * 64}])]:
        save(files)
        for helper in ['make-mcpb.py', 'package-desktop.py', 'make-plugin.py']:
            output = root / (case + helper)
            result = run(helper, output)
            assert result.returncode != 0 and 'cover every file exactly once' in result.stderr, result.stderr
            assert not output.exists()
    save([entry])
    (bundle / 'unlisted').write_text('unlisted bytes')
    for helper in ['make-mcpb.py', 'package-desktop.py', 'make-plugin.py']:
        output = root / ('extra-' + helper)
        result = run(helper, output)
        assert result.returncode != 0 and 'cover every file exactly once' in result.stderr, result.stderr
        assert not output.exists()
    (bundle / 'unlisted').unlink()
    skill = bundle / 'share/agent-3d-cad/skills/native-cad/SKILL.md'
    skill.parent.mkdir(parents=True)
    skill.write_text('Packaging fixture skill; not a runnable native binary.\n')
    skill_entry = {'path': skill.relative_to(bundle).as_posix(), 'sha256': hashlib.sha256(skill.read_bytes()).hexdigest()}
    save([entry, skill_entry])
    output = root / 'valid.mcpb'
    result = run('make-mcpb.py', output)
    assert result.returncode == 0, result.stderr
    with zipfile.ZipFile(output) as archive:
        assert archive.read('bin/agent-3d-cad') == binary.read_bytes()
        assert json.loads(archive.read('share/agent-3d-cad/provenance.json'))['files'] == [entry, skill_entry]
        extension = json.loads(archive.read('manifest.json'))
        assert extension['_meta']['org.agentcad.native'] == {'system':'Darwin','architecture':'arm64','architecture_selection':'distribution_required'}
        assert extension['compatibility'] == {'platforms':['darwin']}
        assert extension['user_config']['workspace']['required'] is False
        assert extension['user_config']['workspace']['default'] == ''
        args = extension['server']['mcp_config']['args']
        assert args == ['serve', '--default-workspace', '--workspace-setting', '${user_config.workspace}']
        # Match Claude's single substitution pass: the default must not leave
        # a nested host placeholder in the executable's workspace argument.
        substituted = [arg.replace('${user_config.workspace}', extension['user_config']['workspace']['default']) for arg in args]
        assert substituted == ['serve', '--default-workspace', '--workspace-setting', '']
    output = root / 'plugins'
    result = run('make-plugin.py', output)
    assert result.returncode == 0, result.stderr
    for plugin in output.iterdir():
        if plugin.is_dir():
            settings = json.loads((plugin / 'mcp.json').read_text())['mcpServers']['agent-3d-cad']
            assert settings == {'type': 'stdio', 'command': './bin/agent-3d-cad', 'args': ['serve', '--default-workspace']}
            assert (plugin / 'skills/setup/SKILL.md').is_file()
            assert (plugin / 'skills/native-cad/SKILL.md').is_file()
            catalog = json.loads((plugin / '.agents/plugins/marketplace.json').read_text())
            assert catalog['plugins'][0]['source'] == {'source': 'local', 'path': '.'}
            assert not (plugin / 'desktop').exists()
            generated = json.loads((plugin / 'share/agent-3d-cad/provenance.json').read_text())
            inventory = {item['path']: item['sha256'] for item in generated['files']}
            files = {file.relative_to(plugin).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest() for file in plugin.rglob('*') if file.is_file() and file.name != 'provenance.json'}
            assert inventory == files

print('All three packagers reject invalid inventories; complete Claude extension and native-only plugin accepted.')
