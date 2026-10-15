"""Developer edit matrix over the real native Service/MCP. No product Python dependency.

Every case starts from an independent committed model. Geometric references are
parameterized once, before the action; the edit round reuses those saved rules.
The baseline ratchets individual cases and also detects lost corpus/coverage.
"""
import argparse
import copy
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import math
import queue
import threading
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent / 'action_matrix'
VERSION = 1
REFUSALS = {'kernel_failure', 'invalid_shape', 'invalid_model', 'selection_missing',
            'selection_ambiguous', 'selection_count_mismatch'}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def near(a, b):
    if not math.isclose(a, b, rel_tol=2e-7, abs_tol=1e-6):
        raise AssertionError(f'{a} != {b}')


def scaled(value, unit='mm', power=1):
    factor = {'parameter': 'scale'}
    if power == 2:
        factor = {'expression': {'op': 'multiply', 'args': [factor, factor], 'unit': 'dimensionless'}}
    return {'expression': {'op': 'multiply', 'args': [value, factor], 'unit': unit}}


def point(values):
    return [scaled(x) for x in values]


def model_for(fixture):
    return {'schema_version': 1, 'units': 'mm', 'parameters': {'scale': 1},
            'features': copy.deepcopy(fixture['features']) + [
                {'id': 'body', 'type': 'scale', 'input': 'seed', 'origin': [0, 0, 0],
                 'factors': [{'parameter': 'scale'}] * 3}], 'output': 'body'}


def selector(entity):
    rule = copy.deepcopy(entity.get('selector'))
    if rule is None:
        return None
    if 'center' in rule:
        rule['center']['point'] = point(rule['center']['point'])
    for measure, unit, power in [('length', 'mm', 1), ('area', 'mm2', 2)]:
        if measure in rule:
            rule[measure]['value'] = scaled(rule[measure]['value'], unit, power)
    return rule


def entity_key(entity):
    # Descriptors identify a base-model case, not persistent topology names.
    # Evaluation-local face/edge numbers are deliberately excluded.
    data = {key: entity[key] for key in ('surface_kind', 'curve_kind', 'center_mm',
            'area_mm2', 'length_mm', 'normal', 'direction') if key in entity}
    def rounded(value):
        if isinstance(value, float):
            return round(value, 5) + 0.0
        if isinstance(value, list):
            return [rounded(v) for v in value]
        if isinstance(value, dict):
            return {k: rounded(v) for k, v in value.items()}
        return value
    return digest(rounded(data))[:20]


def planar_interior_point(face, mesh):
    """Choose a fixed grid point inside the face, independent of triangulation.

    The grid comes from exact face bounds, rounded within the topology identity
    tolerance. Triangles only check occupancy, including holes/concave trims;
    their vertices and diagonals never define the executed hole position.
    """
    normal = [round(v, 7) + 0.0 for v in face['normal']]
    center = [round(v, 5) + 0.0 for v in face['center_mm']]
    drop = max(range(3), key=lambda i: abs(normal[i]))
    axes = [i for i in range(3) if i != drop]
    low = [round(face['bounds_mm']['min'][i], 5) for i in axes]
    high = [round(face['bounds_mm']['max'][i], 5) for i in axes]
    triangles = [[tuple(mesh['positions'][v][i] for i in axes) for v in tri]
                 for tri, owner in zip(mesh['triangles'], mesh['triangle_faces']) if owner == face['id']]

    def cross(a, b, p):
        return (b[0]-a[0])*(p[1]-a[1]) - (b[1]-a[1])*(p[0]-a[0])

    def inside(p):
        for a, b, c in triangles:
            if abs(cross(a, b, c)) <= 1e-12:
                continue
            signs = (cross(a, b, p), cross(b, c, p), cross(c, a, p))
            if min(signs) >= -1e-9 or max(signs) <= 1e-9:
                return True
        return False

    # Prefer the middle, then a fixed order of alternatives. Check clearance
    # beyond the .4mm drill radius, so tessellation noise at trims cannot choose
    # a different point. Exact-solid subtraction and scaling are still tested.
    grid = sorted(((i, j) for i in range(1, 10) for j in range(1, 10)),
                  key=lambda p: ((p[0]-5)**2 + (p[1]-5)**2, p))
    clearance = [(0, 0)] + [(.5*math.cos(i*math.pi/4), .5*math.sin(i*math.pi/4)) for i in range(8)]
    for cell in grid:
        p = [round(lo + (hi-lo)*n/10, 5) for lo, hi, n in zip(low, high, cell)]
        if all(inside((p[0]+dx, p[1]+dy)) for dx, dy in clearance):
            result = list(center)
            for axis, value in zip(axes, p):
                result[axis] = value
            result[drop] = round(center[drop] - sum(normal[i]*(result[i]-center[i]) for i in axes)/normal[drop], 5)
            return result
    raise AssertionError('No interior drill point with clearance for face ' + entity_key(face))


def cases_for(topology, mesh):
    cases = []
    def add(action, entity, features, invariant):
        key = action + '/' + (entity_key(entity) if entity else 'body')
        cases.append({'key': key, 'action': action, 'entity': entity,
                      'features': features, 'invariant': invariant})
    for kind in ('transform', 'mirror', 'pattern'):
        feature = {'id': 'action', 'type': kind, 'input': 'body'}
        if kind == 'transform':
            feature['translation'] = point([60, 0, 0])
        elif kind == 'mirror':
            feature['plane'] = {'origin': [0, 0, 0], 'normal': [1, 0, 0], 'x_direction': [0, 1, 0]}
        else:
            feature.update(count=2, step=point([60, 0, 0]))
        add('body.' + kind, None, [feature], kind)
    for edge in topology['edges']:
        if edge.get('degenerate'):
            continue
        rule = selector(edge)
        for kind, field in [('fillet', 'radius'), ('chamfer', 'distance')]:
            add('edge.' + kind, edge, [{'id': 'action', 'type': kind, 'input': 'body',
                'edges': rule, field: scaled(.2)}], 'changed')
    for face in topology['faces']:
        if face['surface_kind'] != 'plane':
            continue
        rule = selector(face)
        add('face.shell', face, [{'id': 'action', 'type': 'shell', 'input': 'body',
            'thickness': scaled(-.3), 'faces': rule, 'join': 'intersection'}], 'removed')
        normal = face['normal']
        center = face['center_mm']
        inside = planar_interior_point(face, mesh)
        origin = [p + n * .1 for p, n in zip(inside, normal)]
        add('face.hole', face, [{'id': 'action', 'type': 'hole', 'input': 'body',
            'origin': point(origin), 'axis': [-n for n in normal],
            'radius': scaled(.4), 'depth': scaled(80)}], 'removed')
        axis = min(([1, 0, 0], [0, 1, 0], [0, 0, 1]), key=lambda a: abs(sum(x*y for x, y in zip(a, normal))))
        add('face.projection', face, [
            {'id': 'projected', 'type': 'sketch_projection', 'input': 'body', 'faces': rule,
             'workplane': {'origin': point(center), 'normal': normal, 'x_direction': axis}},
            {'id': 'action', 'type': 'extrude', 'input': 'projected', 'distance': scaled(1)}], 'projection')
    keys = [c['key'] for c in cases]
    if len(keys) != len(set(keys)):
        raise AssertionError('Ambiguous case descriptors; extend the corpus identity, never number topology')
    return sorted(cases, key=lambda c: c['key'])


class Native:
    def __init__(self, exe, workspace):
        self.exe, self.workspace = exe, str(workspace)
        self.sequence = 0
        self.active_job = None
        self.stderr = tempfile.TemporaryFile(mode='w+t', encoding='utf-8')
        try:
            self.process = subprocess.Popen([exe, 'serve', '--workspace', self.workspace],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr, text=True, encoding='utf-8')
        except OSError:
            self.stderr.close()
            raise
        self.lines = queue.Queue()
        def read():
            for line in self.process.stdout:
                self.lines.put(line)
            self.lines.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        try:
            self.rpc('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                'clientInfo': {'name': 'action-matrix', 'version': '1'}})
            self.process.stdin.write(canonical({'jsonrpc': '2.0', 'method': 'notifications/initialized'}) + '\n')
            self.process.stdin.flush()
        except Exception:
            self.__exit__()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self.active_job:
            try:
                self.raw('cad_job', {'action': 'cancel', 'job_id': self.active_job})
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    state, error = self.raw('cad_job', {'action': 'get', 'job_id': self.active_job})
                    if state and state['state'] in ('succeeded', 'failed', 'cancelled'):
                        break
                    time.sleep(.05)
            except (RuntimeError, OSError):
                pass
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        self.process.stdout.close()
        self.stderr.close()

    def rpc(self, method, params):
        self.sequence += 1
        self.process.stdin.write(canonical({'jsonrpc': '2.0', 'id': self.sequence,
            'method': method, 'params': params}) + '\n')
        self.process.stdin.flush()
        try:
            line = self.lines.get(timeout=75)
        except queue.Empty:
            raise RuntimeError('Native MCP response deadline expired') from None
        if line is None:
            self.stderr.seek(0)
            raise RuntimeError('Native MCP exited: ' + self.stderr.read()[-2000:])
        response = json.loads(line)
        if response.get('id') != self.sequence or 'error' in response:
            raise RuntimeError(f'Unexpected MCP response: {response}')
        return response['result']

    def call(self, tool, args):
        if tool != 'cad_apply':
            return self.raw(tool, args)
        job_id = f'matrix_{self.sequence + 1}'
        self.active_job = job_id
        value, error = self.raw('cad_job', {'action': 'submit', 'request_id': job_id,
            'tool': tool, 'arguments': args, 'budget': {'timeout_ms': 10000, 'memory_mb': 2048}})
        if error:
            self.active_job = None
            return None, error
        deadline = time.monotonic() + 20
        while value['state'] not in ('succeeded', 'failed', 'cancelled'):
            if time.monotonic() > deadline:
                raise RuntimeError('Native job failed to reach a terminal state')
            time.sleep(.02)
            value, error = self.raw('cad_job', {'action': 'get', 'job_id': job_id})
            if error:
                if error['code'] == 'workspace_busy':
                    value = {'state': 'running'}
                    continue
                raise RuntimeError(f'Job polling failed: {error}')
        self.active_job = None
        return (value['result'], None) if value['state'] == 'succeeded' else (None, value['error'])

    def raw(self, tool, args):
        result = self.rpc('tools/call', {'name': tool, 'arguments': args})
        content = result['structuredContent']
        return (None, content['error']) if result.get('isError') else (content, None)

    def ok(self, tool, args):
        value, error = self.call(tool, args)
        if error:
            raise AssertionError(f'{tool}: {error}')
        return value


def scaled_summary(actual, original, scale):
    near(actual['volume_mm3'], original['volume_mm3'] * scale ** 3)
    near(actual['area_mm2'], original['area_mm2'] * scale ** 2)
    for a, b in zip(actual['center_of_mass_mm'], original['center_of_mass_mm']):
        near(a, b * scale)
    for side in ('min', 'max'):
        for a, b in zip(actual['bounds_mm'][side], original['bounds_mm'][side]):
            near(a, b * scale)
    if actual['solid_count'] != original['solid_count']:
        raise AssertionError('Scale edit changed the solid count')


def run_case(exe, model, case, evidence=None):
    record = {'key': case['key'], 'action': case['action'], 'state': 'passed', 'edit_state': 'not_run'}
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix='cad-action-') as temp, Native(exe, temp) as native:
            base = native.ok('cad_create', {'document_id': 'part', 'model': model})
            before = native.ok('cad_read', {'document_id': 'part'})
            # Record unavailable selection separately; its disappearance must not
            # silently remove a formerly passing case from coverage.
            if case['entity'] and selector(case['entity']) is None and case['action'] != 'face.hole':
                record.update(state='unselectable', edit_state='not_run')
                return record
            operations = [{'op': 'add_feature', 'feature': f} for f in case['features']]
            operations.append({'op': 'set_output', 'feature_id': 'action'})
            args = {'document_id': 'part', 'expected_revision': 1, 'operations': operations}
            record['request'] = {'tool': 'cad_apply', 'arguments': args}
            result, error = native.call('cad_apply', args)
            if error:
                if native.ok('cad_read', {'document_id': 'part'}) != before:
                    raise AssertionError('Rejected action changed HEAD')
                record.update(state='refused', error=error)
                if error['code'] == 'job_timeout':
                    record['state'] = 'timeout'
                elif error['code'] not in REFUSALS:
                    raise RuntimeError(f'Unexpected failure: {error}')
                if evidence and 'failure_package' in error['details']:
                    target = evidence / digest(case['key'])[:20]
                    shutil.copytree(Path(error['details']['failure_package']['path']).parent, target, dirs_exist_ok=True)
                    record['failure_package'] = str(target / 'report.json')
                return record
            original = result['summary']
            record['summary'] = original
            record['edit_summaries'] = []
            if original['valid'] is not True or original['solid_count'] < 1:
                raise AssertionError('Native result is not a valid solid')
            volume, baseline = original['volume_mm3'], base['summary']['volume_mm3']
            if not math.isfinite(volume) or volume <= 0:
                raise AssertionError('Nonpositive or nonfinite volume')
            invariant = case['invariant']
            if invariant in ('transform', 'mirror', 'pattern'):
                near(volume, baseline * (2 if invariant == 'pattern' else 1))
            if invariant == 'transform':
                for actual, old, delta in zip(original['center_of_mass_mm'], base['summary']['center_of_mass_mm'], [60, 0, 0]):
                    near(actual, old + delta)
            if invariant == 'mirror':
                for actual, old, sign in zip(original['center_of_mass_mm'], base['summary']['center_of_mass_mm'], [-1, 1, 1]):
                    near(actual, old * sign)
            if invariant == 'projection':
                near(volume, case['entity']['area_mm2'])
            if invariant == 'removed' and not volume < baseline - 1e-6:
                raise AssertionError('Subtractive edit removed no material')
            if invariant == 'changed' and math.isclose(volume, baseline, rel_tol=1e-9, abs_tol=1e-7):
                raise AssertionError('Edge edit changed no material')
            # Every successful action gets a dimension walk and return, using the
            # same saved feature and selector; every read checks persisted source.
            revision = 2
            for scale in (1.1, .9, 1.0):
                snapshot = native.ok('cad_read', {'document_id': 'part'})
                edit_args = {'document_id': 'part', 'expected_revision': revision,
                    'operations': [{'op': 'set_parameter', 'name': 'scale', 'value': scale}]}
                changed, error = native.call('cad_apply', edit_args)
                if error:
                    if native.ok('cad_read', {'document_id': 'part'}) != snapshot:
                        raise AssertionError('Rejected dimension edit changed HEAD')
                    record.update(edit_state='refused', edit_error=error, edit_request={'tool': 'cad_apply', 'arguments': edit_args})
                    if evidence and 'failure_package' in error['details']:
                        target = evidence / digest(case['key'])[:20]
                        shutil.copytree(Path(error['details']['failure_package']['path']).parent, target, dirs_exist_ok=True)
                        record['failure_package'] = str(target / 'report.json')
                    if error['code'] == 'job_timeout':
                        record['edit_state'] = 'timeout'
                    elif error['code'] not in REFUSALS:
                        raise RuntimeError(f'Unexpected edit failure: {error}')
                    break
                record['edit_summaries'].append({'scale': scale, 'summary': changed['summary']})
                scaled_summary(changed['summary'], original, scale)
                revision = changed['revision']
            else:
                record['edit_state'] = 'passed'
            if native.ok('cad_read', {'document_id': 'part', 'revision': 1}) != before:
                raise AssertionError('Action or dimension walk modified history')
            # Exercise rollback even when all geometry operations succeeded.
            snapshot = native.ok('cad_read', {'document_id': 'part'})
            _, rejected = native.call('cad_apply', {'document_id': 'part', 'expected_revision': revision,
                'operations': [{'op': 'set_parameter', 'name': 'scale', 'value': -1}]})
            if rejected is None or native.ok('cad_read', {'document_id': 'part'}) != snapshot:
                raise AssertionError('Invalid scale failed to preserve HEAD')
    except (AssertionError, KeyError) as error:
        record.update(state='invariant_failure', message=str(error))
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        record.update(state='infrastructure_failure', message=str(error))
    finally:
        record['elapsed_ms'] = round((time.monotonic() - started) * 1000)
    return record


def baseline_errors(baseline, corpus_hash, inventory, results, case_hashes=None):
    errors = []
    if baseline.get('version') != VERSION or baseline.get('corpus_sha256') != corpus_hash:
        errors.append('Corpus/version changed: run the complete matrix and explicitly record a new baseline')
    if case_hashes is not None and baseline.get('case_hashes') != case_hashes:
        errors.append('Action definitions changed: full baseline review required')
        changed = sorted(k for k in set(baseline.get('case_hashes', {})) | set(case_hashes)
                         if baseline.get('case_hashes', {}).get(k) != case_hashes.get(k))
        errors.append('Changed definitions: ' + ', '.join(changed))
    old = baseline.get('cases', {})
    if set(old) != set(inventory):
        errors.append('Case inventory changed (including missing selectors/entities); full baseline review required')
    for result in results:
        prior = old.get(result['key'])
        if result['state'] in ('infrastructure_failure', 'invariant_failure'):
            errors.append(f"{result['key']}: {result['state']}: {result.get('message')}")
        if prior and prior['state'] != 'timeout' and result['state'] == 'timeout':
            errors.append(f"{result['key']}: new timeout")
        if prior and prior['state'] == 'passed' and result['state'] != 'passed':
            errors.append(f"{result['key']}: previously passing action now {result['state']}")
        if prior and prior['edit_state'] == 'passed' and result['edit_state'] != 'passed':
            errors.append(f"{result['key']}: previously passing dimension walk now {result['edit_state']}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=lambda p: str(Path(p).resolve()))
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--jobs', type=int, choices=range(1, 5), default=2)
    parser.add_argument('--results', type=Path)
    parser.add_argument('--filter', help='Run matching case keys (cannot bless)')
    parser.add_argument('--bless', metavar='REASON', help='Record a reviewed full run; refuses partial or broken runs')
    args = parser.parse_args()
    if args.bless and (not args.full or not args.bless.strip() or args.filter):
        parser.error('--bless requires --full and a nonempty reason')
    corpus = json.loads((ROOT / 'corpus.json').read_text())
    inventory, results, case_hashes = [], [], {}
    for name, fixture in sorted(corpus.items()):
        model = model_for(fixture)
        with tempfile.TemporaryDirectory(prefix='cad-matrix-source-') as temp, Native(args.executable, temp) as native:
            created = native.ok('cad_create', {'document_id': 'source', 'model': model})
            near(created['summary']['volume_mm3'], fixture['volume'])
            evaluation = native.ok('cad_query', {'document_id': 'source', 'revision': 1, 'kind': 'mesh'})
        cases = cases_for(evaluation['topology'], evaluation['mesh'])
        groups, selected_cases = set(), []
        for case in cases:
            case['key'] = name + '/' + case['key']
            inventory.append(case['key'])
            # Quantize only the comparison identity, never the executed request.
            def stable(value):
                if isinstance(value, float):
                    return round(value, 7) + 0.0
                if isinstance(value, list):
                    return [stable(v) for v in value]
                if isinstance(value, dict):
                    return {k: stable(v) for k, v in value.items()}
                return value
            case_hashes[case['key']] = digest(stable({'features': case['features'], 'invariant': case['invariant']}))
            # Always cover each action family on each fixture; add a stable 1/8 sample.
            selected = args.full or case['action'] not in groups or int(digest(case['key'])[:8], 16) % 8 == 0
            groups.add(case['action'])
            if selected and (not args.filter or args.filter in case['key']):
                selected_cases.append(case)
        evidence = None
        if args.results:
            evidence = args.results.resolve().parent / (args.results.stem + '-failures')
            evidence.mkdir(parents=True, exist_ok=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run_case, args.executable, model, case, evidence) for case in selected_cases]
            for future in as_completed(futures):
                result = future.result()
                results.append(result)
                print(f"  {result['key']}: {result['state']}/{result['edit_state']} ({result['elapsed_ms']} ms)", flush=True)
        print(f'{name}: {len(cases)} cases discovered', flush=True)
    results.sort(key=lambda r: r['key'])
    path = ROOT / 'baseline.json'
    baseline = json.loads(path.read_text()) if path.exists() else {}
    errors = baseline_errors(baseline, digest(corpus), inventory, results, case_hashes)
    if not results:
        errors.append('No cases selected')
    if args.bless:
        broken = [r for r in results if r['state'] in ('infrastructure_failure', 'invariant_failure')]
        if not results:
            errors = ['Cannot bless an empty matrix']
        elif broken:
            errors = [f"Cannot bless {r['key']}: {r.get('message')}" for r in broken]
        else:
            baseline = {'version': VERSION, 'corpus_sha256': digest(corpus),
                        'notes': baseline.get('notes', []) + [args.bless], 'case_hashes': case_hashes,
                        'cases': {r['key']: {k: r[k] for k in ('state', 'edit_state')} for r in results}}
            path.write_text(json.dumps(baseline, indent=2, sort_keys=True) + '\n')
            errors = []
    report = {'version': VERSION, 'full': args.full, 'corpus_sha256': digest(corpus),
              'executable_sha256': hashlib.sha256(Path(args.executable).read_bytes()).hexdigest(),
              'inventory': inventory, 'case_hashes': case_hashes, 'results': results, 'errors': errors}
    if args.results:
        args.results.parent.mkdir(parents=True, exist_ok=True)
        args.results.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    passed = sum(r['state'] == 'passed' for r in results)
    edited = sum(r['edit_state'] == 'passed' for r in results)
    print(f'Action matrix: {passed}/{len(results)} actions passed; {edited} dimension walks; {len(inventory)} total cases')
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
