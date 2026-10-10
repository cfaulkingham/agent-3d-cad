"""Live independent JSON Schema and captured reconstruction contract checks."""
import copy
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator
exe = str(Path(sys.argv[1]).resolve())
tools = {t['name']: t for t in json.loads(subprocess.check_output([exe, 'tools'], text=True))}
checks = 0
for t in tools.values():
    for side in ('inputSchema', 'outputSchema'):
        Draft202012Validator.check_schema(t[side]); checks += 1

def valid(name, side, value):
    global checks
    Draft202012Validator(tools[name][side]).validate(value); checks += 1

with tempfile.TemporaryDirectory(prefix='cad-mesh-contract-') as folder:
    root = Path(folder).resolve()
    folder = str(root)
    def call(name, args):
        valid(name, 'inputSchema', args)
        p = subprocess.run([exe, 'call', name, '--workspace', folder, '--input', '-'], input=json.dumps(args), text=True, capture_output=True, timeout=90)
        assert p.returncode == 0, p.stderr
        value = json.loads(p.stdout); valid(name, 'outputSchema', value); return value
    def write_mesh(path, faces):
        text = 'solid source\n'
        for face in faces:
            text += 'facet normal 0 0 0\nouter loop\n'
            for p in face:
                text += 'vertex ' + ' '.join(map(str, p)) + '\n'
            text += 'endloop\nendfacet\n'
        path.write_text(text + 'endsolid source\n')
    faces = []
    for i in range(64):
        a, b = 2*math.pi*i/64, 2*math.pi*(i+1)/64
        p, q = (5*math.cos(a), 5*math.sin(a), 0), (5*math.cos(b), 5*math.sin(b), 0)
        r, s = q[:2]+(12,), p[:2]+(12,)
        faces.extend([(p,q,r), (p,r,s)])
    path = root / 'cylinder.stl'; write_mesh(path, faces)
    review = call('cad_artifact', {'action':'review', 'path':str(path), 'format':'stl', 'units':'mm', 'expected_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    call('cad_artifact', {'action':'verify', 'review_path':review['path'], 'expected_sha256':review['sha256']})
    request = {'action':'recognize', 'review_path':review['path'], 'expected_sha256':review['sha256'], 'options':{'distance_tolerance_mm':.05, 'normal_tolerance_deg':10, 'weld_tolerance_mm':1e-7, 'min_triangles':8, 'max_patches':8, 'max_candidates':128}}
    result = call('cad_artifact', request)
    assert len(result['patches']) == 1 and not result['leftover_triangle_indices']; checks += 1
    patch = result['patches'][0]
    assert patch['analytic']['kind'] == 'cylinder' and abs(patch['analytic']['radius_mm']-5) < 1e-8; checks += 1
    guided = copy.deepcopy(request)
    guided['reconstruct'] = [{'patch_id':patch['id'], 'feature_id':'Cylinder', 'extent':'cylinder', 'axis_range_mm':patch['axis_range_mm']}]
    proposed = call('cad_artifact', guided)['proposals'][0]
    created = call('cad_create', {'document_id':'adopted', 'model':proposed['model']})
    assert abs(created['summary']['volume_mm3']-300*math.pi) < 1e-6; checks += 1
    call('cad_export', {'document_id':'adopted', 'revision':1, 'format':'step'})
    for mutate in (lambda r:r['options'].update(distance_tolerance_mm=0), lambda r:r['options'].update(max_candidates=513), lambda r:r.update(untrusted=True), lambda r:r['options'].pop('normal_tolerance_deg')):
        bad = copy.deepcopy(request); mutate(bad)
        assert not Draft202012Validator(tools['cad_artifact']['inputSchema']).is_valid(bad); checks += 1
    bad = copy.deepcopy(guided); bad['reconstruct'][0]['thickness_mm'] = 2
    assert not Draft202012Validator(tools['cad_artifact']['inputSchema']).is_valid(bad); checks += 1
print(f'{checks} mesh reconstruction schema checks / {len(tools)} tools')
print('Discovery bytes:', len(json.dumps({'tools':list(tools.values())}, separators=(',',':')).encode()))
