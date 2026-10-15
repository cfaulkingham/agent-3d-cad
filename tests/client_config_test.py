import json
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib

exe = str(Path(sys.argv[1]).resolve())
version = (Path(__file__).resolve().parent.parent / 'VERSION').read_text().strip()
assert json.loads(subprocess.check_output([exe, '--version']))['version'] == version
with tempfile.TemporaryDirectory(prefix='cad-config-') as temp:
    workspace = str(Path(temp) / 'CAD projects ü')
    for client in ('claude', 'codex', 'opencode'):
        text = subprocess.check_output([exe, 'config', '--client', client, '--workspace', workspace], text=True, encoding='utf-8')
        if client == 'codex':
            server = tomllib.loads(text)['mcp_servers']['agent-3d-cad']
            command = [server['command'], *server['args']]
        elif client == 'claude':
            server = json.loads(text)['mcpServers']['agent-3d-cad']
            command = [server['command'], *server['args']]
        else:
            command = json.loads(text)['mcp']['agent-3d-cad']['command']
        assert Path(command[0]).resolve() == Path(exe).resolve()
        assert command[1:] == ['serve', '--workspace', workspace]
    removed = subprocess.run([exe, 'viewer', '--workspace', workspace], capture_output=True, text=True)
    assert removed.returncode != 0
    assert json.loads(removed.stderr)['error']['code'] == 'usage'
    assert not Path(workspace).exists(), 'Removed launcher must not create a workspace or live view'
    assert 'agent-3d-cad viewer' not in subprocess.check_output([exe, '--help'], text=True)
print('Version and all three client configurations round-trip correctly without creating a workspace.')
