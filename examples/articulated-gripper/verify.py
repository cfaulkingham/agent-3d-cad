"""Independent developer acceptance runner; end users open the JSON directly.

python examples/articulated-gripper/verify.py /absolute/agent-3d-cad --output /new/evidence-directory
The chosen output must not contain a workspace. Calls and artifacts are retained.
No app, printer, supplier, or mutable user project is contacted.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    exe, out = args.executable.resolve(), args.output.resolve()
    workspace = out / 'workspace'
    if workspace.exists():
        parser.error('Choose a fresh output directory; an existing workspace is never reset.')
    (out / 'calls').mkdir(parents=True, exist_ok=True)
    (out / 'artifacts').mkdir(exist_ok=True)
    evidence = {'executable': str(exe), 'executable_sha256': sha(exe), 'checks': 0,
                'poses': {}, 'limitations': {}, 'orientations': [], 'artifacts': []}
    counter = 0

    def save(name, value):
        (out / name).write_text(json.dumps(value, indent=2) + '\n')

    def check(value, label):
        evidence['checks'] += 1
        if not value:
            raise AssertionError(label)

    def near(actual, expected, label, tolerance=1e-6):
        check(abs(actual - expected) <= tolerance, f'{label}: {actual} != {expected}')

    def call(tool, request, label=None, error=None, target_workspace=None):
        nonlocal counter
        counter += 1
        prefix = f'calls/{counter:03d}-{label or tool}'
        save(prefix + '.request.json', {'tool': tool, 'arguments': request})
        response = subprocess.run([str(exe), 'call', tool, '--workspace', str(target_workspace or workspace), '--input', '-'],
                                  input=json.dumps(request), capture_output=True, text=True, timeout=320)
        (out / (prefix + '.stderr.txt')).write_text(response.stderr)
        if error:
            check(response.returncode != 0, f'{tool} must reject invalid input')
            result = json.loads(response.stderr)['error']
            save(prefix + '.error.json', result)
            check(result['code'] == error, f'{tool} expected {error}: {result}')
            return result
        check(response.returncode == 0, f'{tool}: {response.stderr}')
        result = json.loads(response.stdout)
        save(prefix + '.response.json', result)
        return result

    def job(tool, request, label):
        print(label, flush=True)
        request_id = 'gripper_' + label.replace('-', '_')
        submitted = call('cad_job', {'action': 'submit', 'request_id': request_id, 'tool': tool,
                         'arguments': request, 'budget': {'timeout_ms': 300000, 'memory_mb': 4096}}, label + '-submit')
        job_id = submitted['job_id']
        deadline = time.monotonic() + 310
        while time.monotonic() < deadline:
            response = subprocess.run([str(exe), 'call', 'cad_job', '--workspace', str(workspace), '--input', '-'],
                        input=json.dumps({'action': 'get', 'job_id': job_id}), capture_output=True, text=True, timeout=15)
            if response.returncode:
                error = json.loads(response.stderr)['error']
                if error['code'] == 'workspace_busy':
                    time.sleep(.1)
                    continue
                raise AssertionError(error)
            result = json.loads(response.stdout)
            if result['state'] in ('queued', 'running', 'cancelling'):
                time.sleep(.1)
                continue
            save('calls/' + label + '-terminal.json', result)
            check(result['state'] == 'succeeded', f"{label}: {result['state']} {result.get('error', '')}")
            return result['result']
        raise TimeoutError(f'Live job {job_id} exceeded observation window; inspect this handle before rerunning.')

    def retain(result, name):
        source = Path(result['path'])
        check(source.is_file(), f'{name} exists')
        if 'sha256' in result:
            check(sha(source) == result['sha256'], f'{name} exact output hash')
        destination = out / 'artifacts' / name
        shutil.copy2(source, destination)
        evidence['artifacts'].append({'path': str(destination.relative_to(out)), 'sha256': sha(destination), 'bytes': destination.stat().st_size})
        return destination

    create = json.loads(Path(__file__).with_name('gripper.create.json').read_text())
    doc = create['document_id']
    created = job('cad_create', create, 'create')
    evidence['model_sha256'] = created['model_sha256']
    initial = call('cad_read', {'document_id': doc})

    def fk(summary, angle, length=48, thickness=8):
        check(summary['valid'] and summary['solid_count'] == 48, '48 valid exact leaf solids')
        parts = {p['id']: p for p in summary['assembly']['parts']}
        check(len(parts) == 48, 'Every leaf retained once')
        for side, x, theta in [('left', -34, math.radians(angle)), ('right', 34, -math.radians(angle))]:
            for name, rotation, tx, ty in [('proximal', theta, x, 12),
                ('distal', 2 * theta, x - length * math.sin(theta), 12 + length * math.cos(theta)),
                ('pad', 2 * theta, x - length * math.sin(theta), 12 + length * math.cos(theta))]:
                transform = parts[side + '/' + name]['transform']
                expected = [math.cos(rotation), -math.sin(rotation), 0, tx,
                            math.sin(rotation), math.cos(rotation), 0, ty,
                            0, 0, 1, thickness, 0, 0, 0, 1]
                for actual, value in zip(transform, expected):
                    near(actual, value, f'{side}/{name} independent planar FK')
        return parts

    pairs = [('left/pad', 'right/pad'), ('base', 'left/base_pin'), ('base', 'right/base_pin'),
             ('left/base_pin', 'left/base_bush'), ('left/base_bush', 'left/proximal'),
             ('left/proximal', 'left/distal'), ('left/distal', 'left/pad'),
             ('left/rear_post_left', 'left/mount_screw_1'), ('left/rear_post_right', 'left/mount_screw_2'),
             ('left/rear_post_left', 'left/proximal'), ('left/rear_post_right', 'left/proximal'),
             ('right/rear_post_left', 'right/proximal'), ('right/rear_post_right', 'right/proximal'),
             ('actuator', 'housing'), ('housing', 'base'), ('bottom', 'housing')]
    revision = 1
    for pose, angle in [('open', 0), ('approach', -5), ('grasp', -10)]:
        if pose != 'open':
            changed = job('cad_apply', {'document_id': doc, 'expected_revision': revision, 'operations': [
                {'op': 'apply_pose', 'assembly_id': name, 'pose_id': pose} for name in ('finger', 'right_finger')]}, pose)
            revision = changed['revision']
        queried = job('cad_query', {'document_id': doc, 'revision': revision, 'kind': 'topology'}, pose + '-topology')
        summary = queried['summary']
        parts = fk(summary, angle)
        measurement = job('cad_fabrication_review', {'document_id': doc, 'revision': revision, 'options': {
            'profile': {'process': 'fdm', 'orientation': {'build_direction': [0, 0, 1], 'x_direction': [1, 0, 0]}},
            'minimum_clearance_mm': 0, 'clearance_pairs': [{'a': a, 'b': b} for a, b in pairs]}}, pose + '-clearance')
        findings = next(c for c in measurement['report']['checks'] if c['id'] == 'assembly_interference')['evidence']['pairs']
        for finding in findings:
            check(not finding['interference'], f'{pose} clash: {finding}')
            near(math.dist(finding['point_a_mm'], finding['point_b_mm']), finding['distance_mm'], 'Independent clearance witness distance')
        by_pair = {(p['a'], p['b']): p for p in findings}
        near(by_pair['left/base_pin', 'left/base_bush']['distance_mm'], .1, 'Pin/bush nominal radial gap')
        near(by_pair['left/base_bush', 'left/proximal']['distance_mm'], .1, 'Bush/link nominal radial gap')
        near(by_pair['left/proximal', 'left/distal']['distance_mm'], 1, 'Link stack axial gap')
        pad_gap = by_pair['left/pad', 'right/pad']['distance_mm']
        evidence['poses'][pose] = {'revision': revision, 'angle_deg': angle, 'pad_gap_mm': pad_gap,
                                   'summary': summary, 'clearance_pairs': findings, 'review_sha256': measurement['sha256']}
        picture = job('cad_render', {'source': {'document_id': doc, 'revision': revision}, 'inline_image': False,
                                    'settings': {'width': 1024, 'height': 768}}, pose + '-render')
        retain(picture, pose + '.png')
    check(evidence['poses']['open']['pad_gap_mm'] > evidence['poses']['approach']['pad_gap_mm'] > evidence['poses']['grasp']['pad_gap_mm'] > 0,
          'Measured finger opening decreases through all three poses')

    all_pairs_request = {'document_id': doc, 'revision': revision, 'feature_id': 'gripper',
                         'evaluation_id': queried['evaluation_id'], 'query': {'action': 'clearance'}}
    rejected = call('cad_measure', all_pairs_request, 'all-pairs-limit', error='limit_exceeded')
    evidence['limitations']['all-pairs-limit'] = {'request': all_pairs_request, 'error': rejected,
                                               'reviewed_pairs': len(pairs), 'possible_pairs': 48 * 47 // 2}

    before = call('cad_read', {'document_id': doc})
    for label, operations in [
        ('joint-limit', [{'op': 'set_joint_value', 'assembly_id': 'finger', 'mate_id': 'base_hinge', 'coordinate': 'angle_deg', 'value': -13}]),
        ('closed-loop', [{'op': 'set_mate', 'assembly_id': 'finger', 'mate': {'id': 'closure', 'type': 'rigid', 'parent': 'distal', 'child': 'mount',
            'parent_frame': {'origin': [0, 0, 0], 'normal': [0, 0, 1], 'x_direction': [1, 0, 0]},
            'child_frame': {'origin': [0, 0, 0], 'normal': [0, 0, 1], 'x_direction': [1, 0, 0]}}}])]:
        rejected = call('cad_apply', {'document_id': doc, 'expected_revision': revision, 'operations': operations}, label, error='invalid_model')
        evidence['limitations'][label] = {'request': operations, 'error': rejected}
        check(call('cad_read', {'document_id': doc}) == before, 'Rejected edit preserves model and HEAD')
    changed = job('cad_apply', {'document_id': doc, 'expected_revision': revision, 'operations': [
                    {'op': 'set_parameter', 'name': 'proximal_length', 'value': 52}]}, 'longer-links')
    fk(changed['summary'], -10, length=52)
    check(changed['summary']['volume_mm3'] > evidence['poses']['grasp']['summary']['volume_mm3'], 'Longer shared links alter real geometry')
    edited_revision = changed['revision']
    old = call('cad_read', {'document_id': doc, 'revision': 1})
    check(old == initial, 'Historical editable source survives pose and parameter edits')
    # Keep exported reference geometry at the measured grasp revision; the later
    # parameter edit remains visible in history instead of rewriting that source.
    bom = job('cad_bom', {'document_id': doc, 'revision': revision}, 'bom')
    save('bom.json', bom)
    expected_counts = {'actuator_envelope': 1, 'actuator_housing': 1, 'base_plate': 1,
        'distal': 2, 'distal_bush': 4, 'housing_bottom': 1, 'joint_washer': 2,
        'knuckle_pin': 2, 'lower_washer': 2, 'mount': 2, 'mount_screw': 8, 'pad': 2,
        'pivot_cap': 2, 'pivot_pin': 2, 'pivot_seat': 2, 'proximal': 2,
        'proximal_bush': 4, 'spacer': 4, 'support_post': 4}
    ledger = bom.get('bom', bom)
    check(ledger['total_quantity'] == 48, 'BOM physical quantity')
    check({row['input'] for row in ledger['items']} == expected_counts.keys(), 'All 19 independently expected BOM sources')
    for row in ledger['items']:
        check(row['quantity'] == expected_counts[row['input']], 'BOM unique-source quantity')

    exported = job('cad_export', {'document_id': doc, 'revision': revision, 'format': 'step'}, 'step')
    step = retain(exported, 'grasp.step')
    imported = job('cad_import', {'document_id': 'gripper_step_readback', 'path': str(step), 'expected_sha256': sha(step)}, 'step-readback')
    check(imported['summary']['solid_count'] == 48 and imported['summary']['valid'], 'STEP readback retains every valid solid')
    near(imported['summary']['volume_mm3'], evidence['poses']['grasp']['summary']['volume_mm3'], 'STEP geometric volume', 1e-5)
    glb = retain(job('cad_export', {'document_id': doc, 'revision': revision, 'format': 'glb'}, 'glb'), 'grasp.glb')
    raw = glb.read_bytes()
    magic, version, total = struct.unpack_from('<III', raw)
    check((magic, version, total) == (0x46546C67, 2, len(raw)), 'Independent GLB container header')
    length, kind = struct.unpack_from('<II', raw, 12)
    check(kind == 0x4E4F534A, 'GLB JSON chunk')
    scene = json.loads(raw[20:20+length])
    meshes = [node for node in scene['nodes'] if 'mesh' in node]
    check(len(meshes) == 48, 'GLB retains all leaf nodes')
    for node in meshes:
        source = {p['id']: p for p in evidence['poses']['grasp']['summary']['assembly']['parts']}[node['name']]
        for actual, index in zip(node['translation'], (3, 7, 11)):
            near(actual * 1000, source['transform'][index], 'GLB meter translation matches exact source')

    drawing = job('cad_drawing', {'document_id': doc, 'revision': revision, 'drawing': {'title': 'Articulated gripper reference - grasp',
              'hidden_lines': False, 'views': [{'id': 'top', 'orientation': 'top'}],
              'dimensions': [{'view': 'top', 'kind': 'width'}, {'view': 'top', 'kind': 'height'}]}}, 'drawing')
    save('drawing.json', drawing)
    bounds = evidence['poses']['grasp']['summary']['bounds_mm']
    for dimension, axis in zip(drawing['dimensions'], (0, 1)):
        near(dimension['value_mm'], bounds['max'][axis] - bounds['min'][axis], 'Drawing dimension matches exact source extent')
    # Results name each actual output; copy all produced sheet files for inspection.
    for item in drawing['artifacts']:
        if item.get('format') in ('pdf', 'svg', 'dxf'):
            retain(item, 'grasp-' + Path(item['path']).name)

    frame_path = Path(__file__).resolve().parents[2] / 'skills/cad-manufacturing/assets/orientations.json'
    for frame in json.loads(frame_path.read_text()):
        reviewed = job('cad_fabrication_review', {'document_id': doc, 'revision': revision, 'feature_id': 'proximal', 'options': {'profile': {
            'process': 'fdm', 'orientation': {key: frame[key] for key in ('build_direction', 'x_direction')},
            'build_envelope_mm': [180, 180, 180], 'minimum_wall_mm': 1.5, 'overhang_angle_deg': 45}}}, 'orientation-' + frame['id'])
        findings = {item['id']: item for item in reviewed['report']['parts'][0]['checks']}
        evidence['orientations'].append({'frame': frame, 'source_feature': 'proximal', 'revision': revision, 'report_sha256': reviewed['sha256'],
            'size_mm': findings['build_envelope']['evidence']['size_mm'],
            'overhang_area_mm2': findings['fdm_overhang']['evidence']['area_exceeding_limit_mm2'],
            'sampled_wall_mm': findings['sampled_wall_thickness']['evidence']['minimum_sampled_chord_mm'], 'status': reviewed['report']['status']})
        check(findings['global_minimum_wall']['status'] == 'unknown', 'Orientation comparisons retain finite-sample limitation')

    request = json.loads(Path(__file__).with_name('gripper.manufacture.json').read_text())
    request['revision'] = revision
    package = job('cad_manufacture', request, 'manufacture')
    portable = out / 'portable-manufacture'
    shutil.copytree(package['directory'], portable)
    manifest = json.loads((portable / 'manifest.json').read_text())
    for item in manifest['artifacts']:
        path = (portable / item['path']).resolve()
        check(path.is_relative_to(portable) and path.is_file(), 'Portable artifact path stays contained')
        check(sha(path) == item['sha256'] and path.stat().st_size == item['bytes'], 'Every relocated artifact hash and byte count')
    saved = json.loads((portable / 'source.json').read_text())
    recreated = call('cad_create', {'document_id': 'portable_gripper', 'model': saved['model']}, 'portable-recreate', target_workspace=out / 'relocated-workspace')
    near(recreated['summary']['volume_mm3'], evidence['poses']['grasp']['summary']['volume_mm3'], 'Portable editable rebuild volume')
    fk(recreated['summary'], -10)
    check(len(manifest['parts']) == len(expected_counts) and len(manifest['occurrences']) == 48, 'Portable package has unique parts and all occurrences')
    check(call('cad_read', {'document_id': doc})['revision'] == edited_revision, 'Read/export/review work never rewrites later HEAD')
    evidence['package'] = {'path': str(portable.relative_to(out)), 'manifest_sha256': sha(portable / 'manifest.json'), 'unique_parts': len(manifest['parts'])}

    # Reproduce the discovered motion limit on an explicitly separate fixture:
    # native forward kinematics validates declared limits, not physical contact.
    unsafe = copy.deepcopy(create['model'])
    unsafe['parameters']['curl_angle'] = -12
    for feature in unsafe['features']:
        if feature['id'] in ('finger', 'right_finger'):
            for mate in feature['mates']:
                if mate['type'] == 'revolute':
                    mate['angle_limits_deg'] = [-12, 0]
    collision_source = job('cad_create', {'document_id': 'collision_limit_probe', 'model': unsafe}, 'collision-probe-create')
    collision = job('cad_fabrication_review', {'document_id': 'collision_limit_probe', 'revision': 1,
       'options': {'profile': {'process': 'fdm', 'orientation': {'build_direction': [0, 0, 1], 'x_direction': [1, 0, 0]}},
                   'clearance_pairs': [{'a': 'left/pad', 'b': 'right/pad'}]}}, 'collision-probe-review')
    witness = next(c for c in collision['report']['checks'] if c['id'] == 'assembly_interference')['evidence']['pairs'][0]
    check(witness['interference'] and witness['intersection_volume_mm3'] > 60, 'Native FK accepts a declared-limit pose with measured physical clash')
    evidence['limitations']['collision-unaware-fk'] = {'document_id': 'collision_limit_probe', 'model_sha256': collision_source['model_sha256'],
                      'angle_deg': -12, 'witness': witness, 'corrected_shipped_limit_deg': -10}
    evidence['status'] = 'passed'
    save('evidence.json', evidence)
    print(f"{evidence['checks']} gripper checks passed; evidence: {out / 'evidence.json'}", flush=True)


if __name__ == '__main__':
    main()
