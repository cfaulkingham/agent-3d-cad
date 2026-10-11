"""Exercise shipped orientation data against native geometry, without a slicer.

Usage: python tests/manufacturing_skills_test.py NATIVE_EXE [SKILLS_DIRECTORY]
Python is an independent developer test driver, never a skill runtime dependency.
"""
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
skills = (Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / 'skills').resolve()
frames = json.loads((skills / 'cad-manufacturing/assets/orientations.json').read_text())
checks = 0


def require(value, label):
    global checks
    checks += 1
    assert value, label


def near(a, b, label):
    require(math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-6), f'{label}: {a} != {b}')


require(len(frames) == 6 and len({f['id'] for f in frames}) == 6, 'Six distinct frames')
require({tuple(f['build_direction']) for f in frames} ==
        {tuple(sign if k == axis else 0 for k in range(3)) for axis in range(3) for sign in (-1, 1)},
        'Exactly the six signed axes')
for frame in frames:
    near(sum(x * z for x, z in zip(frame['x_direction'], frame['build_direction'])), 0, 'Perpendicular frame')
    near(sum(x * x for x in frame['x_direction']), 1, 'Unit X direction')

with tempfile.TemporaryDirectory(prefix='cad-manufacturing-skills-') as directory:
    workspace = Path(directory) / 'workspace'

    def call(name, arguments, error=None):
        result = subprocess.run([str(exe), 'call', name, '--workspace', str(workspace), '--input', '-'],
                                input=json.dumps(arguments), capture_output=True, text=True, timeout=60)
        if error:
            require(result.returncode != 0, f'{name} rejects unsupported input')
            require(json.loads(result.stderr)['error']['code'] == error, result.stderr)
            return
        require(result.returncode == 0, result.stderr)
        return json.loads(result.stdout)

    model = {'schema_version': 1, 'units': 'mm', 'parameters': {}, 'features': [
        {'id': 'stem', 'type': 'box', 'size': [4, 4, 6]},
        {'id': 'cap', 'type': 'box', 'size': [12, 4, 2], 'origin': [-4, 0, 6]},
        {'id': 'tee', 'type': 'fuse', 'left': 'stem', 'right': 'cap'}], 'output': 'tee'}
    source = call('cad_create', {'document_id': 'orientation_part', 'model': model})
    before = call('cad_read', {'document_id': 'orientation_part'})
    # Analytic exposed planar areas, independently derived from the two boxes.
    expected = {'z_plus': ([12, 4, 8], 32), 'z_minus': ([12, 4, 8], 0),
                'x_plus': ([4, 8, 12], 24), 'x_minus': ([4, 8, 12], 24),
                'y_plus': ([12, 8, 4], 0), 'y_minus': ([12, 8, 4], 0)}
    report_hashes = set()
    for frame in frames:
        profile = {'process': 'fdm', 'orientation': {k: frame[k] for k in ('build_direction', 'x_direction')},
                   'build_envelope_mm': [20, 10, 10], 'minimum_wall_mm': 1, 'overhang_angle_deg': 45}
        result = call('cad_fabrication_review', {'document_id': 'orientation_part', 'revision': 1,
                      'feature_id': 'tee', 'options': {'profile': profile}})
        raw = Path(result['path']).read_bytes()
        require(hashlib.sha256(raw).hexdigest() == result['sha256'] and len(raw) == result['bytes'], 'Real review bytes match ledger')
        report_hashes.add(result['sha256'])
        saved = json.loads(raw)
        require(saved['options']['profile'] == profile, 'Each frame is preserved as actual input')
        require(result['model_sha256'] == source['model_sha256'] and result['revision'] == 1, 'Review binds unchanged source')
        findings = {c['id']: c for c in result['report']['parts'][0]['checks']}
        size, area = expected[frame['id']]
        for actual, analytic in zip(findings['build_envelope']['evidence']['size_mm'], size):
            near(actual, analytic, 'Exact oriented extent')
        near(findings['fdm_overhang']['evidence']['area_exceeding_limit_mm2'], area, 'Orientation-dependent overhang')
        require(findings['build_envelope']['status'] == ('fail' if size[2] > 10 else 'pass'), 'Machine-height rejection uses measured orientation')
        near(findings['sampled_wall_thickness']['evidence']['minimum_sampled_chord_mm'], 2, 'Finite wall sample remains source-qualified')
        require(findings['global_minimum_wall']['status'] == findings['mesh_self_intersections']['status'] == 'unknown',
                'Unmeasured criteria never become passes')
        require(result['report']['status'] in ('fail', 'unknown'), 'Native completion is not production approval')
    require(len(report_hashes) == 6, 'Six independently saved review records')
    require(call('cad_read', {'document_id': 'orientation_part'}) == before, 'Orientation search commits no geometry or revision')
    for unsupported in ('sla', 'sls', 'mjf', 'pbf'):
        call('cad_fabrication_review', {'document_id': 'orientation_part', 'revision': 1,
             'options': {'profile': {'process': unsupported, 'orientation': {'build_direction': [0, 0, 1], 'x_direction': [1, 0, 0]}}}},
             error='invalid_argument')

    exported = call('cad_export', {'document_id': 'orientation_part', 'revision': 1, 'format': 'step'})
    raw_step = Path(exported['path']).read_bytes()
    step_hash = hashlib.sha256(raw_step).hexdigest()
    inspected = call('cad_inspect_step', {'path': exported['path'], 'expected_sha256': step_hash})
    require(inspected['source_sha256'] == step_hash, 'STEP diagnosis binds downloaded bytes')
    purchase = {'supplier': 'Developer fixture CAD source', 'part_number': 'TEE-FIXTURE',
                'source_url': 'https://example.invalid/tee-fixture'}
    imported = call('cad_import', {'document_id': 'sourced_part', 'path': exported['path'],
                                  'expected_sha256': step_hash, 'purchase': purchase})
    read = call('cad_read', {'document_id': 'sourced_part'})
    feature = read['model']['features'][0]
    require(feature['purchase'] == dict(purchase, artifact_sha256=step_hash), 'Native provenance adds actual raw-byte identity')
    require(feature['sha256'] == step_hash, 'Opaque source preserves the same hash')
    call('cad_import', {'document_id': 'mismatched_part', 'path': exported['path'],
                       'expected_sha256': '0' * 64, 'purchase': purchase}, error='artifact_mismatch')
    require(call('cad_read', {'document_id': 'sourced_part'}) == read, 'Rejected mismatch leaves imported source unchanged')

print(f'{checks} manufacturing skill workflow checks passed: six actual native orientations, unknown coverage, unchanged source and STEP provenance.')
