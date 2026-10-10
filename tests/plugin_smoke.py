"""Exercise the packaged plugin in an isolated project folder, without host registration."""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from package_documentation_test import verify as verify_documentation

root = Path(sys.argv[1]).resolve()
verify_documentation(root)
manifest = json.loads((root / 'plugin.json').read_text(encoding='utf-8'))
mcp = json.loads((root / 'mcp.json').read_text(encoding='utf-8'))
assert manifest['$schema'] == 'https://agent-plugins.org/schemas/1.0.0/plugin.schema.json'
assert mcp['$schema'] == 'https://agent-plugins.org/schemas/1.0.0/mcp.schema.json'
settings = mcp['mcpServers']['agent-3d-cad']
assert settings['type'] == 'stdio' and settings['args'] == ['serve', '--default-workspace']
assert settings['command'].startswith('./')
exe = (root / settings['command']).resolve()
assert exe.is_relative_to(root) and exe.is_file()
assert not (root / 'desktop').exists()
assert (root / 'skills/native-cad/SKILL.md').is_file()
for name in set(re.findall(r'\b[A-Z][A-Z_0-9]*\.md', (root / 'skills/native-cad/SKILL.md').read_text())) - {'SKILL.md'}:
    assert (root / 'skills/native-cad' / name).is_file(), f'Missing skill reference: {name}'
assert (root / manifest['extensions']['com.openai']['onboardingSkill']).is_file()
inventory = json.loads((root / 'share/agent-3d-cad/provenance.json').read_text(encoding='utf-8'))
actual = {file.relative_to(root).as_posix() for file in root.rglob('*') if file.is_file()}
assert actual == {entry['path'] for entry in inventory['files']} | {'share/agent-3d-cad/provenance.json'}
for item in inventory['files']:
    assert hashlib.sha256((root / item['path']).read_bytes()).hexdigest() == item['sha256'], item['path']

def rpc(method, params, identifier):
    return {'jsonrpc': '2.0', 'id': identifier, 'method': method, 'params': params}

with tempfile.TemporaryDirectory(prefix='cad-plugin-') as temp:
    workspace = Path(temp) / 'Documents with spaces' / 'Agent CAD'
    environment = dict(os.environ, PATH='')
    for key in list(environment):
        if key.startswith(('CSF_', 'DYLD_', 'LD_LIBRARY_')):
            environment.pop(key)
    def session(calls):
        messages = [rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name':'plugin-smoke','version':'1'}}, 1),
            {'jsonrpc':'2.0','method':'notifications/initialized'}]
        messages += [rpc(method, params, index + 2) for index, (method, params) in enumerate(calls)]
        # Explicit override proves the plugin startup path while keeping real user files untouched.
        result = subprocess.run([str(exe), *settings['args'], '--workspace', str(workspace)],
            input=''.join(json.dumps(message) + '\n' for message in messages), capture_output=True,
            text=True, encoding='utf-8', env=environment, cwd=root, timeout=120)
        assert result.returncode == 0, result.stderr
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        assert [reply['id'] for reply in replies] == list(range(1, len(calls)+2))
        for reply in replies:
            assert 'error' not in reply, reply
            assert not reply['result'].get('isError'), reply
        return [reply['result'] for reply in replies[1:]]
    def tool(name, args): return ('tools/call', {'name':name, 'arguments':args})
    model = {'schema_version':1,'units':'mm','parameters':{'height':6},
        'features':[{'id':'base','type':'box','size':[80,50,{'parameter':'height'}]}],'output':'base'}
    replies = session([('tools/list', {}), ('resources/read', {'uri':'ui://agent-3d-cad/viewer.html'}),
        tool('cad_create', {'document_id':'first_project','model':model}),
        tool('cad_open', {'document_id':'first_project','view_id':'plugin_review'}),
        tool('cad_apply', {'document_id':'first_project','expected_revision':1,
            'operations':[{'op':'set_parameter','name':'height','value':8}]}),
        tool('cad_read', {'document_id':'first_project'})])
    tools = {tool['name']:tool for tool in replies[0]['tools']}
    assert tools['cad_open']['_meta']['ui']['resourceUri'] == 'ui://agent-3d-cad/viewer.html'
    assert 'CadLiveState' in replies[1]['contents'][0]['text']
    original = replies[-1]['structuredContent']
    assert original['revision'] == 2
    # A new process/session must reopen the same documents and historical revisions.
    replies = session([tool('cad_list', {}), tool('cad_read', {'document_id':'first_project','revision':1}),
        tool('cad_export', {'document_id':'first_project','revision':2,'format':'step'}),
        tool('cad_export', {'document_id':'first_project','revision':2,'format':'stl'}),
        tool('cad_drawing', {'document_id':'first_project','revision':2,'drawing':{'formats':['pdf','svg','dxf']}}),
        tool('cad_read', {'document_id':'first_project'})])
    assert replies[0]['structuredContent']['documents'][0]['document_id'] == 'first_project'
    assert replies[1]['structuredContent']['revision'] == 1
    for result, suffix in [(replies[2]['structuredContent'], '.step'), (replies[3]['structuredContent'], '.stl')]:
        path = Path(result['path']); assert path.suffix == suffix and path.stat().st_size > 100
    artifacts = replies[4]['structuredContent']['artifacts']
    assert [artifact['format'] for artifact in artifacts].count('dxf') == 4
    assert {artifact['format'] for artifact in artifacts} == {'pdf','svg','dxf'}
    for artifact in artifacts:
        path = Path(artifact['path']); assert path.stat().st_size > 100
        if artifact['format'] == 'pdf': assert path.read_bytes().startswith(b'%PDF-')
    assert replies[-1]['structuredContent'] == original, 'Exports changed the source'


    print_file = session([tool('cad_export', {'document_id':'first_project','revision':2,'format':'3mf',
        'layout':{'bed_mm':[256,256],'margin_mm':8,'spacing_mm':3}})])[0]['structuredContent']
    assert len(print_file['plates']) == 1 and Path(print_file['path']).read_bytes().startswith(b'PK')
    step_file = session([tool('cad_export', {'document_id':'first_project','revision':2,'format':'step'})])[0]['structuredContent']
    diagnosis = session([tool('cad_inspect_step', {'path':step_file['path']})])[0]['structuredContent']
    assert diagnosis['valid'] and diagnosis['meshable'] and diagnosis['solid_count'] == 1
print('Packaged plugin: complete provenance and skill references, empty-PATH MCP/viewer discovery, create/edit/reopen/history, STEP inspection and STEP/STL/3MF/PDF/SVG/four DXF exports passed. No host installation is implied.')
