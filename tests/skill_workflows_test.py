"""Execute the shipped skill's actual examples over stdio MCP, with analytic oracles.

Developer only; standard library by default. --schema additionally validates
every call and result using independent Draft 2020-12 jsonschema.
"""
import copy
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
repo = Path(__file__).resolve().parents[1]
skill_root = repo / 'skills'
guide = (skill_root / 'native-cad/references/verified-workflows.md').read_text()
examples = {name: json.loads(raw) for name, raw in re.findall(
    r'<!-- cad-example: ([a-z-]+) -->\s*```json\s*(.*?)\s*```', guide, re.S)}
checks = 0


def require(ok, message):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(message)


def near(actual, expected, label):
    require(math.isclose(actual, expected, rel_tol=1e-7, abs_tol=1e-6), f'{label}: {actual} != {expected}')


require(set(examples) == {'plate', 'plate-edit', 'plane', 'assembly', 'assembly-move'}, 'All shipped workflow examples are discovered')
with tempfile.TemporaryDirectory(prefix='cad-skill-mcp-') as directory:
    process = subprocess.Popen([exe, 'serve', '--workspace', directory], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    sequence = 0

    def rpc(method, params, notification=False):
        global sequence
        sequence += 1
        request = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if not notification:
            request['id'] = sequence
        process.stdin.write(json.dumps(request) + '\n')
        process.stdin.flush()
        if notification:
            return
        raw = process.stdout.readline()
        require(bool(raw), 'Native MCP returned a response')
        response = json.loads(raw)
        require(response.get('id') == sequence and 'error' not in response, str(response))
        return response['result']

    validators = {}
    try:
        initialized = rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'skill-workflows', 'version': '1'}})
        rpc('notifications/initialized', {}, notification=True)
        tools = {t['name']: t for t in rpc('tools/list', {})['tools']}
        if '--schema' in sys.argv:
            from jsonschema import Draft202012Validator
            validators = {name: (Draft202012Validator(t['inputSchema']), Draft202012Validator(t['outputSchema'])) for name, t in tools.items()}

        def call(name, arguments, error=False):
            if validators:
                validators[name][0].validate(arguments)
            result = rpc('tools/call', {'name': name, 'arguments': arguments})
            require(result['isError'] == error, f'{name}: {result}')
            value = result['structuredContent']
            if validators and not error:
                validators[name][1].validate(value)
            return value['error'] if error else value

        def example(name):
            item = examples[name]
            return call(item['tool'], item['arguments'])

        # This checks executable vocabulary in every shipped skill and its references,
        # not a second maintained list of tool names.
        named = set(re.findall(r'\bcad_[a-z_]+\b', initialized['instructions']))
        for path in skill_root.rglob('*.md'):
            named.update(re.findall(r'\bcad_[a-z_]+\b', path.read_text()))
        require(len(named) >= 25 and named <= tools.keys(), f'Skill/instructions name unknown tools: {named - tools.keys()}')

        created = example('plate')

        def plate_facts(receipt, thickness):
            summary = receipt['summary']
            near(summary['volume_mm3'], (80 * 50 - (4 - math.pi) * 25 - 4 * math.pi * 9) * thickness, 'Rounded drilled plate volume')
            for actual, expected in zip(summary['center_of_mass_mm'], [40, 25, thickness / 2]):
                near(actual, expected, 'Plate center of mass')
            near(summary['bounds_mm']['max'][2], thickness, 'Plate thickness')
            topology = call('cad_query', {'document_id': 'skill_plate', 'revision': receipt['revision'], 'kind': 'topology'})['topology']
            cylinders = [face for face in topology['faces'] if face['surface_kind'] == 'cylinder']
            # Through-hole cylindrical faces are complete circles, with area 2*pi*r*t;
            # the four R5 corner faces are quarter cylinders and have a different area.
            holes = [face['center_mm'] for face in cylinders if math.isclose(face['area_mm2'], 2 * math.pi * 3 * thickness, rel_tol=1e-7)]
            require(sorted((round(c[0], 6), round(c[1], 6)) for c in holes) == [(10, 10), (10, 40), (70, 10), (70, 40)], 'All four hole axes stay at their stated positions')

        plate_facts(created, 6)
        before = call('cad_read', {'document_id': 'skill_plate'})
        require(before['model'] == examples['plate']['arguments']['model'], 'Document preserves the exact shipped editable recipe')
        edited = example('plate-edit')
        plate_facts(edited, 12)
        require(edited['changes']['parameters']['modified'] == ['thickness'] and edited['changes']['features']['modified'] == [], 'Receipt distinguishes parameter change from authored feature edits')
        revision = edited['revision']
        for thickness in (3, 12, 6):
            call('cad_read', {'document_id': 'skill_plate', 'revision': revision})
            receipt = call('cad_apply', {'document_id': 'skill_plate', 'expected_revision': revision,
                           'operations': [{'op': 'set_parameter', 'name': 'thickness', 'value': thickness}]})
            plate_facts(receipt, thickness)
            revision = receipt['revision']
        require(example('plate-edit') == edited, 'Retry after the range walk returns the original receipt')
        require(call('cad_read', {'document_id': 'skill_plate', 'revision': 1}) == before, 'Parameter walk preserves historical source')

        snapshot = call('cad_read', {'document_id': 'skill_plate'})
        bad = copy.deepcopy(snapshot['model']['features'][1])
        bad['radius'] = 1000
        error = call('cad_apply', {'document_id': 'skill_plate', 'expected_revision': revision,
                     'operations': [{'op': 'replace_feature', 'id': 'rounded', 'feature': bad}]}, error=True)
        failure_id = error['details']['failure_package']['failure_id']
        require(call('cad_failure', {'action': 'get', 'failure_id': failure_id})['report']['context']['tool'] == 'cad_apply', 'Skill can inspect the native failure package')
        require(call('cad_failure', {'action': 'replay', 'failure_id': failure_id})['outcome'] == 'reproduced', 'Skill replays the captured failure without repeating the edit')
        reduced = call('cad_failure', {'action': 'minimize', 'failure_id': failure_id, 'max_attempts': 64})
        require(reduced['outcome'] == 'reduced', 'Skill reduces unrelated downstream features from the failed model')
        require(call('cad_failure', {'action': 'replay', 'failure_id': reduced['minimized_package']['failure_id']})['outcome'] == 'reproduced', 'Reduced skill failure still reproduces')
        require(call('cad_read', {'document_id': 'skill_plate'}) == snapshot, 'Failed edit, replay and minimization preserve HEAD')

        step = call('cad_export', {'document_id': 'skill_plate', 'revision': revision, 'format': 'step'})
        imported = call('cad_import', {'document_id': 'skill_roundtrip', 'path': step['path']})
        near(imported['summary']['volume_mm3'], created['summary']['volume_mm3'], 'STEP round-trip geometry')
        rendered = call('cad_render', {'source': {'document_id': 'skill_plate', 'revision': revision},
                        'settings': {'view': 'sheet', 'width': 320, 'height': 240}, 'inline_image': False})
        png = Path(rendered['path']).read_bytes()
        require(png[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', png[16:24]) == (320, 240), 'Measured workflow produces an actual four-view PNG')

        plane = example('plane')['summary']
        near(plane['volume_mm3'], 24, 'Explicit-workplane extrusion volume')
        for actual, expected in zip(plane['center_of_mass_mm'], [10, -1.5, 20]):
            near(actual, expected, 'Explicit-workplane axes and positive extrusion direction')

        assembly = example('assembly')
        near(assembly['summary']['volume_mm3'], 96, 'Four separate block volumes')
        parts = {p['id']: p for p in assembly['summary']['assembly']['parts']}
        require(sorted(parts) == ['left/a', 'left/b', 'right/a', 'right/b'], 'Nested occurrence paths match the skill')
        for name, x in zip(sorted(parts), (0, 10, 30, 40)):
            near(parts[name]['bounds_mm']['min'][0], x, 'Instance X position')
        moved = example('assembly-move')
        require(moved['changes']['occurrences']['modified'] == ['left/a', 'left/b'], 'Occurrence declarations identify only the moved group')
        for part in moved['summary']['assembly']['parts']:
            near(part['bounds_mm']['min'][1], 20 if part['id'].startswith('left/') else 0, 'Measured group Y placement')
        wider = call('cad_apply', {'document_id': 'skill_assembly', 'expected_revision': 2,
                     'operations': [{'op': 'set_parameter', 'name': 'width', 'value': 4}]})
        near(wider['summary']['volume_mm3'], 192, 'Shared source edit updates every repeated block')
        require(wider['summary']['solid_count'] == 4, 'Assembly preserves independent solids')
        for part in wider['summary']['assembly']['parts']:
            near(part['bounds_mm']['max'][0] - part['bounds_mm']['min'][0], 4, 'Each occurrence follows the shared width')

        # The compiled help recipe is also actual shipped prose, not a fixture copy.
        help_text = call('cad_help', {'topic': 'guide/native-help/rounding-edges-with-fillet'})['entry']['content']
        help_create = json.loads(re.search(r'```json\s*(.*?)\s*```', help_text, re.S)[1])
        help_model = call('cad_create', help_create)
        near(help_model['summary']['volume_mm3'], (40 * 30 - (4 - math.pi) * 4) * 6, 'Compiled help fillet recipe has its analytic volume')
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        stderr = process.stderr.read()
        require(process.returncode == 0, stderr)

print(f'{checks} executable skill checks passed over real MCP: plate/range walk, positions, workplanes, nested assemblies, rollback/replay, STEP and rendered sheet.')
