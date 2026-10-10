"""Isolated native package replacement/removal/reinstall; no host registration.

Usage: python tests/package_lifecycle_test.py PLUGIN_DIRECTORY [CLAUDE_MCPB]
The same build is replaced, so this does not establish cross-version migration.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

source = Path(sys.argv[1]).resolve()
claude_archive = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
checks = 0


def check(value, label):
    global checks
    assert value, label
    checks += 1


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix='cad-package-lifecycle-') as temporary:
    root = Path(temporary).resolve()
    workspace = root / 'Documents with spaces' / 'Agent CAD'
    installs = root / 'isolated applications'; installs.mkdir()
    environment = dict(os.environ, PATH='')
    for key in list(environment):
        if key.startswith(('CSF_', 'DYLD_', 'LD_LIBRARY_')):
            environment.pop(key)

    def call(package, name, args, fail=False):
        exe = package / 'bin' / ('agent-3d-cad.exe' if os.name == 'nt' else 'agent-3d-cad')
        p = subprocess.run([str(exe), 'call', name, '--workspace', str(workspace), '--input', '-'],
                           input=json.dumps(args), text=True, encoding='utf-8', capture_output=True,
                           env=environment, cwd=package, timeout=90)
        check(p.returncode != 0 if fail else p.returncode == 0, p.stderr)
        return json.loads(p.stderr if fail else p.stdout)

    first = installs / 'first'; shutil.copytree(source, first)
    second = installs / 'replacement'; shutil.copytree(source, second)
    model = {'schema_version':1, 'units':'mm', 'parameters':{'height':6},
             'features':[{'id':'body','type':'box','size':[20,30,{'parameter':'height'}]}], 'output':'body'}
    call(first, 'cad_create', {'document_id':'saved_project','model':model})
    original = call(first, 'cad_read', {'document_id':'saved_project'})
    check(call(second, 'cad_read', {'document_id':'saved_project'}) == original, 'Replacement reopens saved source')
    call(second, 'cad_apply', {'document_id':'saved_project', 'expected_revision':1,
                             'operations':[{'op':'set_parameter','name':'height','value':8}]})
    current = call(second, 'cad_read', {'document_id':'saved_project'})
    exported = call(second, 'cad_export', {'document_id':'saved_project','revision':2,'format':'step'})
    export = Path(exported['path']); export_hash = digest(export)
    check(current['revision'] == 2 and export.stat().st_size > 100, 'Replacement preserves edit/export workflow')
    shutil.rmtree(first)
    check(call(second, 'cad_read', {'document_id':'saved_project'}) == current, 'Removing prior package preserves current project')
    head = workspace / 'documents/saved_project/HEAD.json'; head_hash = digest(head)
    shutil.rmtree(second)
    check(digest(head) == head_hash and digest(export) == export_hash, 'Removing every package preserves project and export bytes')
    third = installs / 'reinstalled'; shutil.copytree(source, third)
    check(call(third, 'cad_read', {'document_id':'saved_project'}) == current, 'Reinstall reopens committed source')
    check(call(third, 'cad_read', {'document_id':'saved_project','revision':1}) == original, 'Reinstall preserves historical revisions')
    call(third, 'cad_apply', {'document_id':'saved_project','expected_revision':2,
                            'operations':[{'op':'set_parameter','name':'height','value':0}]}, fail=True)
    check(call(third, 'cad_read', {'document_id':'saved_project'}) == current, 'Rejected edit after reinstall preserves HEAD')
    if claude_archive:
        claude = installs / 'claude-manifest-fixture'; claude.mkdir()
        with zipfile.ZipFile(claude_archive) as archive:
            for entry in archive.infolist():
                path = (claude / entry.filename).resolve()
                check(path.is_relative_to(claude) and not entry.is_dir(), 'MCPB entries remain inside isolated package')
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(archive.read(entry))
                path.chmod((entry.external_attr >> 16) & 0o777 or 0o644)
        manifest = json.loads((claude / 'manifest.json').read_text())
        settings = manifest['server']['mcp_config']
        command = settings['command'].replace('${__dirname}', str(claude))
        args = [value.replace('${user_config.workspace}', str(workspace)) for value in settings['args']]
        messages = [
            {'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'isolated-package-lifecycle','version':'1'}}},
            {'jsonrpc':'2.0','method':'notifications/initialized'},
            {'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'cad_read','arguments':{'document_id':'saved_project'}}}]
        p = subprocess.run([command, *args], input=''.join(json.dumps(x)+'\n' for x in messages),
                           text=True, capture_output=True, encoding='utf-8', cwd=claude, env=environment, timeout=90)
        check(p.returncode == 0, p.stderr)
        replies = [json.loads(line) for line in p.stdout.splitlines()]
        check(replies[-1]['result']['structuredContent'] == current, 'Claude manifest expansion reopens same workspace in native MCP process')
        shutil.rmtree(claude)
        check(digest(head) == head_hash and digest(export) == export_hash, 'Removing emulated Claude package preserves source and exports')
print(f'{checks} isolated package lifecycle checks passed; same-build replacement only, no host GUI installation claimed')
