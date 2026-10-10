"""Independent native workflow regression: STEP diagnosis, periodic meshing, print plates."""
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
import zipfile

exe, fixture = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
checks = 0

def require(value, message):
    global checks
    checks += 1
    if not value:
        raise AssertionError(message)

with tempfile.TemporaryDirectory(prefix='cad-print-workflow-') as temp:
    root = Path(temp)
    def call(name, args, error=None):
        deadline = time.monotonic() + 10
        while True:
            result = subprocess.run([str(exe), 'call', name, '--workspace', str(root / 'workspace'), '--input', '-'],
                                    input=json.dumps(args), capture_output=True, text=True, timeout=120)
            # Polling may briefly race a coordinator's state publication lock.
            if (name == 'cad_job' and args.get('action') == 'get' and result.returncode
                    and json.loads(result.stderr)['error']['code'] == 'workspace_busy'
                    and time.monotonic() < deadline):
                time.sleep(.02)
                continue
            break
        if error:
            require(result.returncode != 0, f'{name} should fail')
            data = json.loads(result.stderr)['error']
            require(data['code'] == error, data)
            return data
        require(result.returncode == 0, result.stderr)
        return json.loads(result.stdout)

    def plate(path):
        with zipfile.ZipFile(path) as archive:
            require(archive.testzip() is None, 'All ZIP CRCs validate')
            require(set(archive.namelist()) == {'[Content_Types].xml', '_rels/.rels', '3D/3dmodel.model'}, 'OPC parts complete')
            model = ET.fromstring(archive.read('3D/3dmodel.model'))
        ns = {'m': 'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'}
        require(model.attrib['unit'] == 'millimeter', '3MF uses millimeters')
        objects = model.findall('m:resources/m:object', ns)
        require(len(objects) == len(model.findall('m:build/m:item', ns)), 'Every separate solid is built')
        results = {}
        for obj in objects:
            vertices = [tuple(float(v.attrib[k]) for k in ('x', 'y', 'z')) for v in obj.findall('m:mesh/m:vertices/m:vertex', ns)]
            triangles = [tuple(int(t.attrib[k]) for k in ('v1', 'v2', 'v3')) for t in obj.findall('m:mesh/m:triangles/m:triangle', ns)]
            require(vertices and triangles and all(math.isfinite(v) for p in vertices for v in p), 'Finite nonempty mesh')
            edges = collections.Counter()
            signed = 0.0
            for t in triangles:
                require(len(set(t)) == 3 and max(t) < len(vertices), 'Valid triangle indices')
                a, b, c = [vertices[i] for i in t]
                signed += sum(a[k] * (b[(k+1)%3]*c[(k+2)%3] - b[(k+2)%3]*c[(k+1)%3]) for k in range(3))/6
                for a, b in zip(t, t[1:] + t[:1]):
                    edges[a, b] += 1
            require(all(count == 1 and edges[b, a] == 1 for (a, b), count in edges.items()), 'Each triangle edge has exactly one opposite neighbor')
            require(signed > 0, 'Closed mesh has positive oriented volume')
            results[obj.attrib['name']] = {'min': [min(p[k] for p in vertices) for k in range(3)],
                'max': [max(p[k] for p in vertices) for k in range(3)], 'volume': signed, 'triangles': len(triangles)}
        return results

    model = {'schema_version': 1, 'units': 'mm', 'parameters': {'width': 60}, 'features': [
        {'id': 'block', 'type': 'box', 'size': [{'parameter': 'width'}, 40, 8]},
        {'id': 'all', 'type': 'assembly', 'parts': [{'id': f'part{i}', 'input': 'block', 'placement': {'translation': [i*100, 0, 0]}} for i in range(12)]}], 'output': 'all'}
    call('cad_create', {'document_id': 'parts', 'model': model})
    export_args = {'document_id': 'parts', 'revision': 1, 'format': '3mf'}
    original = call('cad_export', export_args)
    require(len(plate(original['path'])) == 12, '3MF preserves all distinct instances')
    packed = call('cad_export', dict(export_args, layout={'bed_mm': [100, 100], 'margin_mm': 5, 'spacing_mm': 3}))
    report = json.loads(Path(packed['layout_path']).read_text())
    require(len(packed['plates']) == 6, 'Six plates hold twelve large blocks')
    seen = set()
    for item in packed['plates']:
        require(hashlib.sha256(Path(item['path']).read_bytes()).hexdigest() == item['sha256'], 'Plate ledger hash agrees')
        meshes = plate(item['path'])
        require(not seen.intersection(meshes), 'No part appears on two plates')
        seen.update(meshes)
        for name, mesh in meshes.items():
            require(all(mesh['min'][k] >= 5-1e-7 and mesh['max'][k] <= 95+1e-7 for k in range(2)), 'Mesh stays within 5 mm bed margin')
            require(abs(mesh['min'][2]) < 1e-7 and abs(mesh['volume'] - 19200) < 1e-6, 'Part lies on bed and retains volume')
        values = list(meshes.values())
        for i, a in enumerate(values):
            for b in values[:i]:
                require(any(a['max'][k]+3 <= b['min'][k]+1e-7 or b['max'][k]+3 <= a['min'][k]+1e-7 for k in range(2)), 'At least 3 mm neighbor spacing')
    require(len(seen) == 12, 'All source instances packed exactly once')
    require(call('cad_read', {'document_id': 'parts'})['revision'] == 1, 'Layout does not edit model')
    require(call('cad_export', dict(export_args, layout=report['layout'])) == packed, 'Deterministic repeat verifies complete existing package')
    placements = [{k: p[k] for k in ('source_id', 'plate', 'x_mm', 'y_mm', 'rotation_deg')} for p in report['placements']]
    call('cad_apply', {'document_id': 'parts', 'expected_revision': 1, 'operations': [{'op': 'set_parameter', 'name': 'width', 'value': 61}]})
    repeated = call('cad_export', dict(export_args, revision=2, layout=dict(report['layout'], placements=placements)))
    again = json.loads(Path(repeated['layout_path']).read_text())
    require([{k: p[k] for k in placements[0]} for p in again['placements']] == placements, 'Explicit layout survives dimension edit')
    bad = json.loads(json.dumps(placements)); bad[0]['x_mm'] = 99
    call('cad_export', dict(export_args, layout=dict(report['layout'], placements=bad)), 'invalid_argument')
    call('cad_export', dict(export_args, layout={'bed_mm': [10, 10], 'margin_mm': 1}), 'invalid_argument')
    call('cad_job', {'action': 'submit', 'request_id': 'bad', 'tool': 'cad_export', 'arguments': dict(export_args, format='stl', layout={'bed_mm': [100, 100]})}, 'invalid_argument')
    require(Path(packed['path']).exists(), 'Rejected layouts preserve earlier package')
    scoped = call('cad_export', dict(export_args, feature_id='block'))
    require(len(plate(scoped['path'])) == 1, 'Feature-scoped export works')

    step = call('cad_export', dict(export_args, format='step'))
    inspected = call('cad_inspect_step', {'path': step['path']})
    require(inspected['solid_count'] == 12 and inspected['valid'] and inspected['meshable'], 'Native STEP inspection reports all solids')
    subset_args = {'document_id': 'subset', 'path': step['path'], 'expected_sha256': inspected['source_sha256'], 'solid_indices': [2, 5]}
    subset = call('cad_import', subset_args)
    require(subset['summary']['solid_count'] == 2 and abs(subset['summary']['volume_mm3']-38400) < 1e-6, 'Explicit pinned extraction imports only selected solids')
    call('cad_import', dict(subset_args, document_id='absent', solid_indices=[100]), 'selection_missing')
    call('cad_import', dict(subset_args, document_id='hash', expected_sha256='0'*64), 'artifact_mismatch')
    call('cad_import', {'document_id': 'unpinned', 'path': step['path'], 'solid_indices': [1]}, 'invalid_argument')
    for indices in ([1, 1], [0], [1.5], []):
        call('cad_import', dict(subset_args, document_id='badindices', solid_indices=indices), 'invalid_argument')
    Path(step['path']).unlink()
    require(call('cad_query', {'document_id': 'subset', 'revision': 1})['summary']['solid_count'] == 2, 'Subset cold rebuild uses embedded bytes')

    # This exact valid conical STEP face fails the ordinary OCCT tessellator.
    diagnostic = call('cad_inspect_step', {'path': str(fixture)})
    require(diagnostic['valid'] and diagnostic['meshable'], 'Periodic retry meshes valid original cone')
    imported = call('cad_import', {'document_id': 'roller', 'path': str(fixture)})
    mesh = call('cad_query', {'document_id': 'roller', 'revision': 1, 'kind': 'mesh'})
    require(set(mesh['mesh']['triangle_faces']) == {f['id'] for f in mesh['topology']['faces']}, 'Every original face remains selectable after mesh retry')
    before = imported['summary']
    saved_source = (root / 'workspace/documents/roller/revisions/1.json').read_bytes()
    stl = call('cad_export', {'document_id': 'roller', 'revision': 1, 'format': 'stl'})
    data = Path(stl['path']).read_bytes()
    require(len(data) == 84 + struct.unpack_from('<I', data, 80)[0]*50, 'Complete binary STL')
    require(struct.unpack_from('<I', data, 80)[0] > 0, 'STL contains complete print tessellation')
    roller = call('cad_export', {'document_id': 'roller', 'revision': 1, 'format': '3mf'})
    value = next(iter(plate(roller['path']).values()))
    roller_plate = call('cad_export', {'document_id': 'roller', 'revision': 1, 'format': '3mf', 'layout': {'bed_mm': [256, 256]}})
    require(abs(next(iter(plate(roller_plate['path']).values()))['min'][2]) < 1e-8, 'Curved print mesh rests on the bed even when analytic extrema are not tessellated')
    require(abs(value['volume']-before['volume_mm3'])/before['volume_mm3'] < .015, 'Roller mesh volume follows the exact solid within tessellation tolerance')
    after = call('cad_query', {'document_id': 'roller', 'revision': 1})['summary']
    require(all(before[key] == after[key] for key in ('solid_count', 'face_count', 'edge_count')), 'Meshing preserves exact topology counts')
    require(all(abs(before[key]-after[key]) < 1e-8 for key in ('volume_mm3', 'area_mm2')), 'Meshing preserves exact measured volume and area within integration tolerance')
    require(all(abs(before['bounds_mm'][end][k]-after['bounds_mm'][end][k]) < 1e-8 for end in ('min','max') for k in range(3)), 'Meshing preserves exact bounds')
    require((root / 'workspace/documents/roller/revisions/1.json').read_bytes() == saved_source, 'Meshing preserves the complete saved revision byte-for-byte')
    job = call('cad_job', {'action': 'submit', 'request_id': 'inspect_async', 'tool': 'cad_inspect_step', 'arguments': {'path': str(fixture)}})
    for _ in range(300):
        job = call('cad_job', {'action': 'get', 'job_id': 'inspect_async'})
        if job['state'] not in ('queued', 'running', 'cancelling'):
            break
        time.sleep(.02)
    require(job['state'] == 'succeeded' and job['result'] == diagnostic, 'Inspection works through durable jobs without a document ID')
print(f'{checks} native print workflow checks passed')
