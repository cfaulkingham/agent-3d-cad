"""Independent Draft 2020-12 validation of real progressive native/live payloads.

Development only: .deps/mcp-sdk-test/bin/python tests/progressive_schema_tests.py build/agent-3d-cad
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from jsonschema import Draft202012Validator
from schema_validators import SchemaValidators

exe = str(Path(sys.argv[1]).resolve())
catalog = json.loads(subprocess.check_output([exe, 'tools'], text=True))
tools = {t['name']: t for t in catalog}
validators = SchemaValidators()
checks = 0
for name in ('cad_create', 'cad_query', 'cad_open', 'cad_viewer'):
    for direction in ('inputSchema', 'outputSchema'):
        Draft202012Validator.check_schema(tools[name][direction])
        checks += 1

def validate(name, direction, value):
    global checks
    validators.get((name, direction), tools[name][direction]).validate(value)
    checks += 1

def invalid(name, direction, value):
    global checks
    assert not validators.get((name, direction), tools[name][direction]).is_valid(value), value
    checks += 1

with tempfile.TemporaryDirectory(prefix='cad-progressive-schema-') as workspace:
    def call(name, args):
        validate(name, 'inputSchema', args)
        deadline = time.monotonic() + 5
        while True:
            run = subprocess.run([exe, 'call', name, '--workspace', workspace, '--input', '-'], input=json.dumps(args), text=True, capture_output=True, timeout=60)
            if run.returncode == 0:
                result = json.loads(run.stdout)
                validate(name, 'outputSchema', result)
                return result
            if 'workspace_busy' not in run.stderr or time.monotonic() > deadline:
                raise AssertionError(run.stderr)
            time.sleep(.02)

    request = json.loads((Path(__file__).resolve().parent.parent / 'examples/nested-assembly.create.json').read_text())
    request['document_id'] = 'progressiveSchema'
    request['model']['features'][1] = {'id':'rod', 'type':'cylinder', 'radius':3.123456789, 'height':6.345678912}
    call('cad_create', request)
    base = {'document_id':request['document_id'], 'revision':1}
    manifest = call('cad_query', dict(base, kind='component_manifest'))
    assert 'mesh' not in manifest and len(manifest['components']['occurrences']) == 5
    checks += 1
    component_args = dict(base, kind='component_mesh', evaluation_id=manifest['evaluation_id'], geometry_id='rod', level=1)
    component = call('cad_query', component_args)
    assert component['evaluation_id'] == manifest['evaluation_id']
    checks += 1
    for field in ('evaluation_id', 'geometry_id', 'level'):
        bad = dict(component_args)
        del bad[field]
        invalid('cad_query', 'inputSchema', bad)
    for level in (-1, 3, .5, True):
        invalid('cad_query', 'inputSchema', dict(component_args, level=level))
    invalid('cad_query', 'inputSchema', dict(base, kind='component_manifest', level=0))
    bad = copy.deepcopy(manifest)
    bad['components']['unknown'] = True
    invalid('cad_query', 'outputSchema', bad)
    bad = copy.deepcopy(component)
    bad['component_mesh']['level'] = 3
    invalid('cad_query', 'outputSchema', bad)

    topology = call('cad_query', dict(base, kind='topology', feature_id='rod'))
    for kind, metric in (('faces', 'area'), ('edges', 'length')):
        entity = next(v for v in topology['topology'][kind] if 'selector' in v and metric in v['selector'])
        for mutate in ('expression', 'count'):
            bad = copy.deepcopy(topology)
            selector = next(v['selector'] for v in bad['topology'][kind] if v['id'] == entity['id'])
            if mutate == 'expression':
                selector[metric]['value'] = {'parameter':'angle'}
            else:
                selector['expected_count'] = 2
            invalid('cad_query', 'outputSchema', bad)
    authored = copy.deepcopy(next(v['selector'] for v in topology['topology']['edges'] if 'selector' in v and 'length' in v['selector']))
    authored['length']['value'] = {'parameter':'angle'}
    authored['expected_count'] = 2
    validate('cad_query', 'inputSchema', dict(base, kind='curve', feature_id='rod', curve={'edge':authored}))

    call('cad_open', {'document_id':request['document_id'], 'view_id':'progressiveSchema'})
    deadline = time.monotonic() + 60
    while True:
        sync = call('cad_viewer', {'action':'sync','view_id':'progressiveSchema','progressive':True})
        if sync['state'] == 'ready':
            break
        assert sync['state'] == 'loading' and time.monotonic() < deadline, sync
        time.sleep(.03)
    assert sync['progressive'] is True
    checks += 1
    args = {'action':'mesh','view_id':'progressiveSchema','evaluation_id':sync['evaluation_id'],'offset':0}
    chunks = []
    while True:
        transfer = call('cad_viewer', args)
        chunks.append(transfer['data'])
        if transfer['next_offset'] is None:
            break
        args['offset'] = transfer['next_offset']
    frozen = json.loads(''.join(chunks))
    validate('cad_query', 'outputSchema', frozen)
    args = {'action':'component','view_id':'progressiveSchema','evaluation_id':sync['evaluation_id'],'geometry_id':'rod','level':2,'request_id':'schemaRequest','offset':0}
    chunks = []
    deadline = time.monotonic() + 60
    while True:
        transfer = call('cad_viewer', args)
        if transfer['component_state'] == 'loading':
            assert time.monotonic() < deadline, transfer
            time.sleep(.03)
            continue
        assert transfer['component_state'] == 'ready', transfer
        chunks.append(transfer['data'])
        if transfer['next_offset'] is None:
            break
        args['offset'] = transfer['next_offset']
    frozen_component = json.loads(''.join(chunks))
    validate('cad_query', 'outputSchema', frozen_component)
    assert frozen_component['evaluation_id'] == frozen['evaluation_id']
    checks += 1
    invalid('cad_viewer','inputSchema', dict(args, level=3))
    for field in ('geometry_id', 'level', 'request_id'):
        bad = dict(args)
        del bad[field]
        invalid('cad_viewer','inputSchema', bad)
    cancelled = call('cad_viewer', dict(args, cancel=True))
    assert cancelled['component_state'] == 'cancelled'
    checks += 1
print(f'{checks} independent progressive schema checks passed')
