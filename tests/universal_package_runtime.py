"""Execute the same archived universal packages on each real macOS CPU.

Only the developer harness uses Python. Child services receive an empty PATH and
no SDK/loader environment. --development-thin exercises this harness locally but
never produces universal acceptance evidence. No host registration is performed.
"""
import argparse
import base64
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import platform
import queue
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'packaging'))
from bundle_validation import native_identity

MANIFEST = 'share/agent-3d-cad/provenance.json'
CPUS = {'arm64': 0x0100000c, 'x64': 0x01000007}
checks = 0


def normalized_architecture(value):
    return {'arm64': 'arm64', 'aarch64': 'arm64', 'x64': 'x64',
            'x86_64': 'x64', 'amd64': 'x64', 'universal': 'universal'}.get(str(value).lower())


def check(condition, message):
    global checks
    if not condition:
        raise AssertionError(message)
    checks += 1


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def process_identity(pid):
    # macOS proc_info.h: PROC_PIDARCHINFO=19, two cpu_type_t integer fields.
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    lib.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    architecture = (ctypes.c_int * 2)()
    if lib.proc_pidinfo(pid, 19, 0, architecture, ctypes.sizeof(architecture)) != 8:
        raise ProcessLookupError(f'Cannot inspect process {pid}: {ctypes.get_errno()}')
    path = ctypes.create_string_buffer(4096)
    if lib.proc_pidpath(pid, path, len(path)) <= 0:
        raise ProcessLookupError(f'Cannot inspect executable for process {pid}')
    return {'pid': pid, 'cpu_type': architecture[0], 'cpu_subtype': architecture[1],
            'executable': path.value.decode()}


def translated_process():
    # Reject a translated Python harness so an arm64 host cannot stand in for
    # the real Intel runner. This key is absent on Intel and zero on native ARM.
    libc = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    libc.sysctlbyname.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p, ctypes.c_size_t]
    value = ctypes.c_int()
    size = ctypes.c_size_t(ctypes.sizeof(value))
    status = libc.sysctlbyname(b'sysctl.proc_translated', ctypes.byref(value), ctypes.byref(size), None, 0)
    if status != 0:
        check(ctypes.get_errno() == 2, 'Cannot establish native host execution')
        return False
    check(size.value == ctypes.sizeof(value), 'Unexpected translation status type')
    return value.value != 0


def extract(archive, destination):
    destination.mkdir()
    def safe(name):
        path = PurePosixPath(name)
        check(bool(name) and not path.is_absolute() and '..' not in path.parts, 'Archive path escaped')
        return path
    if archive.name.endswith('.tar.gz'):
        with tarfile.open(archive) as package:
            names = [m.name for m in package.getmembers()]
            check(len(names) == len(set(names)), 'Duplicate tar member')
            for name in names:
                safe(name)
            package.extractall(destination, filter='data')
    else:
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            check(len(names) == len(set(names)), 'Duplicate ZIP member')
            for member in package.infolist():
                target = destination / str(safe(member.filename))
                mode = member.external_attr >> 16
                check(not stat.S_ISLNK(mode), 'ZIP must contain materialized files')
                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(package.read(member))
                    target.chmod(stat.S_IMODE(mode) or 0o644)
    manifests = list(destination.rglob(MANIFEST))
    check(len(manifests) == 1, 'Expected one package inventory')
    root = manifests[0].parents[2].resolve()
    for path in root.rglob('*'):
        check(path.resolve().is_relative_to(root), 'Escaping package link')
    return root


def verify_inventory(root, kind, thin, architecture, source_manifest):
    manifest = json.loads((root / MANIFEST).read_text())
    check(manifest['system'] == 'Darwin', 'Non-macOS package')
    check(normalized_architecture(manifest['architecture']) == (architecture if thin else 'universal'), 'Wrong package architecture')
    entries = {item['path']: item['sha256'] for item in manifest['files']}
    check(len(entries) == len(manifest['files']), 'Duplicate inventory entry')
    excluded = {MANIFEST} | ({'manifest.json'} if kind == 'claude' else set())
    actual = {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*')
              if p.is_file() and p.relative_to(root).as_posix() not in excluded}
    check(entries == actual, 'Inventory must cover every file and exact hash')
    candidates = [root/'bin/agent-3d-cad', *sorted((root/'lib').glob('*.dylib'))]
    check(len(candidates) > 1, 'Native library closure is missing')
    for candidate in candidates:
        check(native_identity(candidate) == ('Darwin', architecture if thin else 'universal'),
              f'Incorrect native header/slices: {candidate}')
    if not thin:
        identity = manifest['source_identity']
        check(identity['format_version'] == 1 and identity['algorithm'] == 'sha256-file-list-v1', 'Source identity format')
        check(identity['manifest'] == 'share/agent-3d-cad/source-inputs.sha256', 'Source manifest path')
        check(digest(root/identity['manifest']) == identity['sha256'] == digest(source_manifest),
              'Embedded source differs from independently captured composer checkout')
    return manifest


class Session:
    def __init__(self, root, kind, workspace, architecture, evidence):
        self.identifier = 0
        self.queue = queue.Queue()
        executable = root / 'bin/agent-3d-cad'
        if kind == 'plugin':
            settings = json.loads((root / 'mcp.json').read_text())['mcpServers']['agent-3d-cad']
            check(settings['command'] == './bin/agent-3d-cad' and settings['type'] == 'stdio', 'Plugin command')
            command = [str(executable), *settings['args'], '--workspace', str(workspace)]
        elif kind == 'claude':
            settings = json.loads((root / 'manifest.json').read_text())['server']['mcp_config']
            command = [settings['command'].replace('${__dirname}', str(root)),
                       *[arg.replace('${user_config.workspace}', str(workspace)) for arg in settings['args']]]
            check(command[0] == str(executable), 'Claude command escaped the package')
        else:
            command = [str(executable), 'serve', '--workspace', str(workspace)]
        environment = {k: v for k, v in os.environ.items() if k in ('HOME', 'TMPDIR', 'LANG', 'LC_ALL')}
        environment['PATH'] = ''
        self.stderr = tempfile.TemporaryFile(mode='w+t')
        self.process = subprocess.Popen(command, cwd=root, env=environment, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.stderr, text=True, encoding='utf-8')
        def read():
            try:
                for line in self.process.stdout:
                    self.queue.put(json.loads(line))
            except Exception as error:
                self.queue.put(error)
            finally:
                self.queue.put(EOFError('Native server closed stdout'))
        self.thread = threading.Thread(target=read, daemon=True)
        self.thread.start()
        try:
            initialized = self.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                        'clientInfo': {'name': 'universal-runtime-test', 'version': '1'}})
            check(initialized['protocolVersion'] == '2025-11-25', 'Protocol negotiation')
            self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
            identity = process_identity(self.process.pid)
            check(identity['cpu_type'] == CPUS[architecture], 'Service ran the wrong CPU slice')
            check(Path(identity['executable']).resolve() == executable.resolve(), 'Service ran a different executable')
            evidence.append(identity)
        except BaseException:
            self.close()
            raise

    def send(self, value):
        self.process.stdin.write(json.dumps(value) + '\n')
        self.process.stdin.flush()

    def request(self, method, params):
        self.identifier += 1
        self.send({'jsonrpc': '2.0', 'id': self.identifier, 'method': method, 'params': params})
        deadline = time.monotonic() + 15
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f'{method} exceeded its 15s response deadline')
            item = self.queue.get(timeout=remaining)
            if isinstance(item, BaseException):
                raise item
            if 'id' not in item:
                continue
            check(item['id'] == self.identifier and 'error' not in item, f'RPC failed: {item}')
            return item['result']

    def call(self, name, arguments, error=None):
        deadline = time.monotonic() + 15
        while True:
            result = self.request('tools/call', {'name': name, 'arguments': arguments})
            value = result['structuredContent']
            if value.get('error', {}).get('code') != 'workspace_busy':
                break
            check(time.monotonic() < deadline, 'Workspace remained busy')
            time.sleep(.01)
        check(json.loads(result['content'][0]['text']) == value, 'Structured and text result disagree')
        if error:
            check(result.get('isError') and value['error']['code'] == error, f'Expected {error}: {value}')
        else:
            check(not result.get('isError'), f'{name}: {value}')
        return value

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                raise AssertionError('Native session did not shut down')
        self.thread.join(timeout=2)
        self.stderr.seek(0)
        diagnostics = self.stderr.read()
        self.stderr.close()
        check(self.process.returncode == 0, f'Native process failed: {diagnostics}')

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def cache_geometry(workspace, volume, keys=None):
    found = {}
    for path in (workspace / '.cache').glob('*.json'):
        if keys is not None and path.stem not in keys:
            continue
        envelope = json.loads(path.read_text())
        payload = envelope['payload']
        check(hashlib.sha256(payload.encode()).hexdigest() == envelope['sha256'], 'Cache checksum')
        value = json.loads(payload)
        if 'snapshot' in value and math.isclose(value.get('summary', {}).get('volume_mm3', -1), volume, abs_tol=1e-7):
            check(path.stem == envelope['key'], 'Cache key/path mismatch')
            found[path.stem] = {'sha256': digest(path), 'mtime_ns': path.stat().st_mtime_ns}
    check(bool(found), 'No exact geometry cache entry for the representative model')
    return found


def expected_cache_key(build_identity):
    # Independent literal-box oracle for src/cache.cpp's canonical JSON identity.
    def key(intent):
        envelope = {'format': 1, 'build': build_identity, 'kernel': '8.0.1', 'intent': intent}
        return hashlib.sha256(json.dumps(envelope, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    feature = {'id': 'base', 'type': 'box', 'size': [20, 30, {'parameter': 'height'}]}
    feature_key = key({'kind': 'feature', 'feature': feature, 'parameters': {'height': 8}, 'dependencies': {}})
    return key({'kind': 'geometry', 'output': 'base', 'features': {'base': feature_key}})


def poll(session, job_id):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        result = session.call('cad_job', {'action': 'get', 'job_id': job_id})
        if result['state'] in {'succeeded', 'failed', 'cancelled', 'interrupted'}:
            return result
        time.sleep(.01)
    raise AssertionError('Job did not terminate within 30s')


def check_box_geometry(summary, height):
    check(summary['valid'] and summary['solid_count'] == 1 and summary['units'] == 'mm', 'Exact box topology/units')
    check(math.isclose(summary['volume_mm3'], 600*height, abs_tol=1e-6), 'Analytic box volume')
    check(math.isclose(summary['area_mm2'], 2*(600+50*height), abs_tol=1e-6), 'Analytic box area')
    for actual, expected in zip(summary['center_of_mass_mm'], (10,15,height/2)):
        check(math.isclose(actual, expected, abs_tol=1e-7), 'Analytic box centroid')
    for end, expected in [('min',(0,0,0)), ('max',(20,30,height))]:
        for actual, value in zip(summary['bounds_mm'][end], expected):
            check(math.isclose(actual, value, abs_tol=1e-7), 'Analytic box bounds')


def exercise(root, kind, base, architecture, provenance, archive):
    evidence = {'services': [], 'workers': [], 'checks': 0}
    executable_hash = digest(root/'bin/agent-3d-cad')
    before = checks
    workspace = base / (kind + ' saved projects')
    model = {'schema_version': 1, 'units': 'mm', 'parameters': {'height': 6},
             'features': [{'id': 'base', 'type': 'box', 'size': [20, 30, {'parameter': 'height'}]}], 'output': 'base'}
    with Session(root, kind, workspace, architecture, evidence['services']) as session:
        catalog = session.request('tools/list', {})
        check(len(json.dumps(catalog, separators=(',', ':')).encode()) <= 476160, 'Discovery budget')
        resource = session.request('resources/read', {'uri': 'ui://agent-3d-cad/viewer.html'})['contents'][0]
        check(hashlib.sha256(resource['text'].encode()).hexdigest() == provenance['viewer_app_sha256'], 'Viewer bytes differ from provenance')
        created = session.call('cad_create', {'document_id': 'part', 'model': model})
        check(created['revision'] == 1 and math.isclose(created['summary']['volume_mm3'], 3600, abs_tol=1e-7), 'Created volume')
        check_box_geometry(created['summary'], 6)
        first = session.call('cad_read', {'document_id': 'part', 'revision': 1})
        check(first['model'] == model, 'Captured editable source changed')
        edited = session.call('cad_apply', {'document_id': 'part', 'expected_revision': 1,
                              'operations': [{'op': 'set_parameter', 'name': 'height', 'value': 8}]})
        check(edited['revision'] == 2 and math.isclose(edited['summary']['volume_mm3'], 4800, abs_tol=1e-7), 'Edited volume')
        check_box_geometry(edited['summary'], 8)
        current = session.call('cad_read', {'document_id': 'part', 'revision': 2})
        cache = cache_geometry(workspace, 4800)
        if provenance['architecture'] == 'universal':
            thin = json.loads((root/f'share/agent-3d-cad/universal-inputs/{architecture}/provenance.json').read_text())
            check(normalized_architecture(thin['architecture']) == architecture, 'Wrong slice provenance')
            build_identity = thin['build_identity']['cache_identity']
        else:
            # Development harness check only: old thin packages predate the
            # build_identity manifest. This cannot produce universal acceptance.
            candidates = set(re.findall(rb'[0-9a-f]{64}-Release', (root/'bin/agent-3d-cad').read_bytes()))
            check(len(candidates) == 1, 'Cannot identify development binary cache identity')
            build_identity = candidates.pop().decode()
        check(sorted(cache) == [expected_cache_key(build_identity)], 'Actual worker cache belongs to wrong binary/slice')
        evidence['compiled_cache_identity'] = build_identity
        session.call('cad_apply', {'document_id': 'part', 'expected_revision': 2,
                     'operations': [{'op': 'set_parameter', 'name': 'height', 'value': 0}]}, error='invalid_model')
        check(session.call('cad_read', {'document_id': 'part'}) == current, 'Failed edit changed HEAD')
        exported_job = session.call('cad_job', {'action': 'submit', 'request_id': 'universal_export', 'tool': 'cad_export',
                                   'arguments': {'document_id': 'part', 'revision': 2, 'format': 'step'}})
        exported = poll(session, exported_job['job_id'])
        check(exported['state'] == 'succeeded', f'Export worker: {exported}')
        exported = exported['result']
        step = Path(exported['path']); step_hash = digest(step)
        for link in exported['downloads']:
            resource = session.request('resources/read', {'uri': link['uri']})['contents'][0]
            check(base64.b64decode(resource['blob'], validate=True) == step.read_bytes(), 'Download bytes differ')
        inspected = session.call('cad_inspect_step', {'path': str(step)})
        check(inspected['valid'] and inspected['meshable'] and inspected['solid_count'] == 1, 'STEP topology')
        imported = session.call('cad_import', {'document_id': 'readback', 'path': str(step), 'expected_sha256': step_hash})
        check_box_geometry(imported['summary'], 8)
        # Keep a real geometry worker alive long enough to inspect its actual CPU,
        # then prove cancellation retains the earlier committed source.
        features = [{'id': 'plate', 'type': 'box', 'size': [1300, 1300, 5], 'origin': [-10, -10, 0]},
                    {'id': 'pin', 'type': 'cylinder', 'radius': 4, 'height': 20, 'origin': [0, 0, -5]},
                    {'id': 'row', 'type': 'pattern', 'input': 'pin', 'count': 64, 'step': [20, 0, 0]},
                    {'id': 'grid', 'type': 'pattern', 'input': 'row', 'count': 64, 'step': [0, 20, 0]},
                    {'id': 'perforated', 'type': 'cut', 'left': 'plate', 'right': 'grid'}]
        job = session.call('cad_job', {'action': 'submit', 'request_id': 'architecture_worker', 'tool': 'cad_apply',
                           'arguments': {'document_id': 'part', 'expected_revision': 2,
                                         'operations': [{'op': 'add_feature', 'feature': f} for f in features]
                                         + [{'op': 'set_output', 'feature_id': 'perforated'}]},
                           'budget': {'timeout_ms': 10000, 'memory_mb': 2048}})
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not evidence['workers']:
                for marker in (workspace / '.workers').glob('*/building.json'):
                    try:
                        pid = json.loads((marker.parent / 'process.json').read_text())['pid']
                        identity = process_identity(pid)
                    except (OSError, ValueError, ProcessLookupError):
                        continue
                    check(identity['cpu_type'] == CPUS[architecture], 'Geometry worker ran wrong CPU slice')
                    check(Path(identity['executable']).resolve() == (root / 'bin/agent-3d-cad').resolve(), 'Worker executable escaped package')
                    evidence['workers'].append(identity)
                time.sleep(.002)
            check(bool(evidence['workers']), 'Did not observe an actual building geometry worker')
            session.request('ping', {})
            check(session.call('cad_read', {'document_id': 'part'}) == current, 'Live worker changed committed HEAD')
        finally:
            session.call('cad_job', {'action': 'cancel', 'job_id': job['job_id']})
        check(poll(session, job['job_id'])['state'] == 'cancelled', 'Worker cancellation failed')
        check(session.call('cad_read', {'document_id': 'part'}) == current, 'Cancelled worker changed HEAD')
    with Session(root, kind, workspace, architecture, evidence['services']) as session:
        check(session.call('cad_read', {'document_id': 'part'}) == current, 'Restart changed source')
        check(session.call('cad_read', {'document_id': 'part', 'revision': 1}) == first, 'Restart changed history')
        query = session.call('cad_query', {'document_id': 'part', 'revision': 2})
        check_box_geometry(query['summary'], 8)
        check(cache_geometry(workspace, 4800, cache) == cache, 'Restart did not reuse unchanged native cache')
        check(session.call('cad_job', {'action': 'get', 'job_id': 'universal_export'})['state'] == 'succeeded', 'Job persistence')
    # Isolated same-build replacement/removal/reinstallation never touches a host.
    replacement = base / (kind + ' replacement'); shutil.copytree(root, replacement, symlinks=True)
    head = workspace / 'documents/part/HEAD.json'; head_hash = digest(head)
    with Session(replacement, kind, workspace, architecture, evidence['services']) as session:
        check(session.call('cad_read', {'document_id': 'part'}) == current, 'Replacement source readback')
    shutil.rmtree(root)
    shutil.rmtree(replacement)
    check(digest(head) == head_hash and digest(step) == step_hash, 'Removing package changed persistent files')
    reinstalled = extract(archive, base/(kind + ' reinstalled'))
    with Session(reinstalled, kind, workspace, architecture, evidence['services']) as session:
        check(session.call('cad_read', {'document_id': 'part'}) == current, 'Reinstall source readback')
    shutil.rmtree(reinstalled)
    evidence.update(checks=checks-before, geometry_cache_keys=sorted(cache), step_sha256=step_hash,
                    source_revision=2, imported_volume_mm3=imported['summary']['volume_mm3'],
                    imported_bounds_mm=imported['summary']['bounds_mm'],
                    imported_centroid_mm=imported['summary']['center_of_mass_mm'],
                    executable_sha256=executable_hash)
    return evidence


def run(args):
    check(sys.platform == 'darwin', 'Actual runtime test requires macOS')
    check(process_identity(os.getpid())['cpu_type'] == CPUS[args.architecture], 'Test runner itself uses wrong CPU')
    check(not translated_process(), 'Rosetta cannot substitute for the actual Intel runtime lane')
    archives = sorted(p for p in args.packages.iterdir() if p.name.endswith(('.tar.gz', '.zip', '.mcpb')))
    check(len(archives) == 3, 'Expected exact native/plugin/Claude artifact set')
    result = {'source_commit': args.source, 'architecture': args.architecture, 'system': platform.platform(),
              'universal_acceptance': not args.development_thin, 'translated_process': False, 'archives': {p.name: digest(p) for p in archives}, 'packages': {}}
    with tempfile.TemporaryDirectory(prefix='cad universal relocated ') as temporary:
        base = Path(temporary).resolve()
        for archive in archives:
            kind = 'native' if archive.name.endswith('.tar.gz') else 'claude' if archive.suffix == '.mcpb' else 'plugin'
            root = extract(archive, base/(kind+' unpacked'))
            provenance = verify_inventory(root, kind, args.development_thin, args.architecture, args.packages/'source-inputs.sha256')
            result['packages'][kind] = exercise(root, kind, base, args.architecture, provenance, archive)
            result['packages'][kind]['source_identity'] = provenance.get('source_identity')
        keys = [p['geometry_cache_keys'] for p in result['packages'].values()]
        check(all(k == keys[0] for k in keys), 'Wrapper changed cache identity')
    check(result['archives'] == {p.name:digest(p) for p in archives}, 'Input archive changed')
    result['checks'] = checks
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(f'{checks} relocated {args.architecture} runtime checks passed; universal acceptance={result["universal_acceptance"]}')


def compare(args):
    reports = [json.loads(p.read_text()) for p in args.reports]
    check(len(reports) == 2 and {r['architecture'] for r in reports} == {'arm64', 'x64'}, 'Need both actual CPU reports')
    check(all(r['universal_acceptance'] and r['source_commit'] == args.source for r in reports), 'Thin/stale fixture is not acceptance')
    check(all(r['translated_process'] is False for r in reports), 'Translated process cannot substitute for native CPU evidence')
    check(reports[0]['archives'] == reports[1]['archives'], 'Runners executed different archives')
    for kind in ('native','plugin','claude'):
        a,b = [r['packages'][kind] for r in reports]
        check(a['executable_sha256'] == b['executable_sha256'], 'Runners executed different universal binaries')
        check(a['geometry_cache_keys'] != b['geometry_cache_keys'], 'CPU-specific cache identities were not isolated')
        check(a['source_identity'] is not None and a['source_identity'] == b['source_identity'], 'Source bytes differ between CPU reports')
        for report in reports:
            evidence = report['packages'][kind]
            check(len(evidence['services']) >= 4 and bool(evidence['workers']), 'Missing process observations')
            check(all(p['cpu_type'] == CPUS[report['architecture']] for p in evidence['services']+evidence['workers']), 'Wrong CPU process evidence')
    args.report.write_text(json.dumps({'source_commit':args.source,'status':'passed','archives':reports[0]['archives'],
                           'runtime_reports':[{k:r[k] for k in ('architecture','checks','system')} for r in reports]},indent=2)+'\n')
    print(f'{checks} same-artifact dual-CPU acceptance checks passed')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='command',required=True)
    runtime=commands.add_parser('run');runtime.add_argument('--packages',type=Path,required=True);runtime.add_argument('--architecture',choices=CPUS,required=True)
    runtime.add_argument('--development-thin',action='store_true',help='Local harness check only; cannot satisfy compare')
    merge=commands.add_parser('compare');merge.add_argument('reports',type=Path,nargs=2)
    for command in (runtime,merge):
        command.add_argument('--source',required=True);command.add_argument('--report',type=Path,required=True)
    options=parser.parse_args();(run if options.command=='run' else compare)(options)
