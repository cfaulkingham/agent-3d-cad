"""Optional developer-only macOS publisher preparation; never invoked by CI/build.

inspect is offline. sign uses an explicitly selected Developer ID and Apple's
timestamp service. submit/collect contact Apple only with --allow-apple-network.
No command installs credentials, changes keychains, publishes or installs software.
"""
import argparse
import copy
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile

from bundle_validation import MANIFEST, native_slices, sha, validate_bundle, _portable_macho

spec = importlib.util.spec_from_file_location('universal_composer', Path(__file__).with_name('compose-macos-universal.py'))
composer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(composer)
VERSION = (Path(__file__).resolve().parents[1] / 'VERSION').read_text().strip()
MACHINE = {'arm64': 'arm64', 'x64': 'x86_64'}
PUBLISHER = 'publisher.json'


def command(*args, timeout=120):
    result = subprocess.run(args, capture_output=True, text=True, timeout=timeout, env={**os.environ, 'LC_ALL': 'C'})
    if result.returncode:
        raise ValueError(f'{Path(args[0]).name} failed: {result.stderr.strip()}')
    return result


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def snapshot(root):
    return {p.relative_to(root).as_posix(): {'sha256': sha(p), 'mode': p.stat().st_mode & 0o777}
            for p in sorted(root.rglob('*')) if p.is_file()}


def core(root, manifest_hash=None, source_hash=None):
    root = Path(root).resolve()
    provenance, system, architecture, binary = validate_bundle(root, VERSION)
    if system != 'Darwin' or 'plugin' in provenance or (root / 'manifest.json').exists():
        raise ValueError('Publisher input must be a native macOS core bundle')
    if manifest_hash is not None and sha(root / MANIFEST) != manifest_hash:
        raise ValueError('Input provenance hash differs from approved core')
    if architecture != 'universal':
        composer.input_bundle(root, architecture, VERSION)
    for key, value in composer.PINNED.items():
        if provenance.get(key) != value:
            raise ValueError('Unexpected pinned identity: ' + key)
    if source_hash is not None and provenance['source_identity']['sha256'] != source_hash:
        raise ValueError('Input production source hash differs from approved source')
    if len(provenance['files']) > 10000 or sum((root / e['path']).stat().st_size for e in provenance['files']) > 2 * 1024**3:
        raise ValueError('Core exceeds publisher input budget')
    native = {e['path']: native_slices(root / e['path']) for e in provenance['files']
              if e['path'] == binary or e['path'].endswith('.dylib')}
    if not native or len(native) > 512:
        raise ValueError('Native image count exceeds publisher budget')
    magics = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
              b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xce'}
    for entry in provenance['files']:
        path = entry['path']
        with (root / path).open('rb') as stream:
            if stream.read(4) in magics and path not in native:
                raise ValueError('Unlisted native code item: ' + path)
    for path, slices in native.items():
        command('/usr/bin/codesign', '--verify', '--strict', '--all-architectures', str(root / path))
        for arch, details in slices.items():
            _portable_macho(root, path, details, native[binary][arch])
    return provenance, native


def new_destination(destination, source):
    source = Path(source).resolve()
    requested = Path(destination).absolute()
    if requested.exists() or requested.is_symlink():
        raise ValueError('Output must be a new directory')
    target = requested.resolve()
    if target.is_relative_to(source) or source.is_relative_to(target):
        raise ValueError('Output cannot overlap input')
    target.parent.mkdir(parents=True, exist_ok=True)
    return target


def selected_identity(fingerprint, team):
    if not re.fullmatch('[0-9A-F]{40}', fingerprint) or not re.fullmatch('[A-Z0-9]{10}', team):
        raise ValueError('Supply an exact uppercase certificate SHA-1 and 10-character Team ID')
    listing = command('/usr/bin/security', 'find-identity', '-v', '-p', 'codesigning').stdout
    names = re.findall(r'^\s*\d+\) ' + fingerprint + r' "([^"]+)"\s*$', listing, re.MULTILINE)
    if len(names) != 1 or not names[0].startswith('Developer ID Application: ') or not names[0].endswith('(' + team + ')'):
        raise ValueError('Exact valid Developer ID Application identity/team is not present')
    return names[0]


def code_identifier(prefix, path):
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z][A-Za-z0-9-]*)+', prefix):
        raise ValueError('Supply the publisher-owned reverse-DNS identifier prefix')
    return prefix + '.' + ('native' if path == 'bin/agent-3d-cad' else 'library-' + hashlib.sha256(path.encode()).hexdigest()[:24])


def verify_developer_id(file, team):
    if not re.fullmatch('[A-Z0-9]{10}', team):
        raise ValueError('Invalid Team ID')
    requirement = ('=anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists '
                   'and certificate leaf[field.1.2.840.113635.100.6.1.13] exists '
                   'and certificate leaf[subject.OU] = "' + team + '"')
    command('/usr/bin/codesign', '--verify', '--strict', '--all-architectures', '--test-requirement', requirement, str(file))


def audit_signature(file, slices, fingerprint, team, identifier):
    command('/usr/bin/codesign', '--verify', '--strict', '--all-architectures', str(file))
    result = {}
    with tempfile.TemporaryDirectory(prefix='cad-signer-audit-') as temporary:
        for arch in slices:
            prefix = str(Path(temporary) / (arch + '-certificate-'))
            details = command('/usr/bin/codesign', '--display', '--verbose=4', '--arch', MACHINE[arch],
                              '--extract-certificates=' + prefix, str(file)).stderr
            certificates = sorted(Path(temporary).glob(arch + '-certificate-*'))
            flags = re.search(r'flags=0x([0-9a-fA-F]+)', details)
            fields = dict(re.findall(r'^([A-Za-z]+)=(.*)$', details, re.MULTILINE))
            authorities = re.findall(r'^Authority=(.*)$', details, re.MULTILINE)
            if not certificates or hashlib.sha1(Path(prefix + '0').read_bytes()).hexdigest().upper() != fingerprint:
                raise ValueError('Native signature does not use the exact selected certificate')
            if fields.get('TeamIdentifier') != team or not authorities or not authorities[0].startswith('Developer ID Application: '):
                raise ValueError('Native signature team/Developer ID differs')
            verify_developer_id(file, team)
            if fields.get('Identifier') != identifier or not fields.get('Timestamp'):
                raise ValueError('Native signature identifier/secure timestamp is missing')
            if not flags or not (int(flags[1], 16) & 0x10000) or int(flags[1], 16) & 2:
                raise ValueError('Native signature lacks hardened runtime or is ad-hoc')
            entitlements = command('/usr/bin/codesign', '--display', '--arch', MACHINE[arch], '--entitlements', '-', '--xml', str(file)).stdout.strip()
            if entitlements and plistlib.loads(entitlements.encode()) != {}:
                raise ValueError('Publisher preparation permits no runtime-relaxing or other entitlements')
            if not re.fullmatch('[0-9a-f]{40,64}', fields.get('CDHash', '')):
                raise ValueError('Native signature CDHash is missing')
            result[arch] = {'certificate_sha1': fingerprint, 'team_id': team, 'identifier': identifier,
                            'timestamp': fields['Timestamp'], 'cdhash': fields['CDHash'], 'hardened_runtime': True,
                            'entitlements': {}, 'authorities': authorities}
    return result


def preserve_native(before, after, path):
    if before.keys() != after.keys():
        raise ValueError('Signing changed the CPU slice set: ' + path)
    for arch in before:
        for key in (*composer.METADATA, 'section_payload_sha256', 'signing_payload_sha256'):
            if before[arch][key] != after[arch][key]:
                raise ValueError('Signing changed unsigned native payload/loader metadata: ' + path + ': ' + key)


def refresh_inventory(stage, provenance, native):
    # Build identities and original thin provenance stay byte-exact. Only final
    # post-signature slice hashes and final file inventory change.
    if provenance['architecture'] == 'universal':
        for record in provenance['composition']['native_files']:
            for item in record['slices']:
                item['sha256'] = native[record['path']][item['architecture']]['sha256']
    provenance['files'] = [{'path': p.relative_to(stage).as_posix(), 'sha256': sha(p)}
                           for p in sorted(stage.rglob('*')) if p.is_file() and p.relative_to(stage) != MANIFEST]
    write_json(stage / MANIFEST, provenance)


def publish_directory(stage, destination):
    # Darwin renamex_np(RENAME_EXCL) is atomic and cannot replace even an empty
    # destination created by another process after the preflight check.
    library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    library.renamex_np.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    if library.renamex_np(os.fsencode(stage), os.fsencode(destination), 4):
        raise OSError(ctypes.get_errno(), 'Publisher output could not be published exclusively')


def sign(root, output, manifest_hash, source_hash, fingerprint, team, prefix):
    root = Path(root).resolve()
    destination = new_destination(output, root)
    provenance, native = core(root, manifest_hash, source_hash)
    original = snapshot(root)
    selected_identity(fingerprint, team)
    code_identifier(prefix, 'bin/agent-3d-cad')
    with tempfile.TemporaryDirectory(prefix='.publisher-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'result'
        stage.mkdir()
        copied = stage / 'core'
        shutil.copytree(root, copied, symlinks=False)
        if snapshot(copied) != original:
            raise ValueError('Input changed while capturing publisher copy')
        signatures, signed = {}, {}
        for path in sorted(native, key=lambda p: (p == 'bin/agent-3d-cad', p)):
            identifier = code_identifier(prefix, path)
            command('/usr/bin/codesign', '--force', '--sign', fingerprint, '--timestamp', '--options', 'runtime',
                    '--identifier', identifier, str(copied / path))
            signed[path] = native_slices(copied / path)
            preserve_native(native[path], signed[path], path)
            signatures[path] = audit_signature(copied / path, signed[path], fingerprint, team, identifier)
        refresh_inventory(copied, copy.deepcopy(provenance), signed)
        core(copied, source_hash=source_hash)
        final = snapshot(copied)
        if set(original) != set(final) or any(original[p] != final[p] for p in original if p not in native and p != MANIFEST.as_posix()):
            raise ValueError('Signing changed a non-native resource')
        if any(original[p]['mode'] != final[p]['mode'] for p in original):
            raise ValueError('Signing changed file permissions')
        shutil.copyfile(root / MANIFEST, stage / 'input-provenance.json')
        with zipfile.ZipFile(stage / 'notary-submission.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(copied.rglob('*')):
                if file.is_file():
                    archive.write(file, 'core/' + file.relative_to(copied).as_posix())
        report = {'format_version': 1, 'status': 'signed-not-submitted', 'project_version': VERSION,
                  'input_provenance_sha256': manifest_hash, 'source_identity': provenance['source_identity'],
                  'certificate_sha1': fingerprint, 'team_id': team, 'identifier_prefix': prefix,
                  'signatures': signatures, 'signed_provenance_sha256': sha(copied / MANIFEST),
                  'submission_sha256': sha(stage / 'notary-submission.zip'), 'core_files': final,
                  'notarization': 'not-submitted', 'stapled': False,
                  'boundary': 'Standalone native binaries and ZIPs cannot be stapled; host/Gatekeeper acceptance is separate.'}
        write_json(stage / PUBLISHER, report)
        if snapshot(root) != original:
            raise ValueError('Original input changed during signing')
        publish_directory(stage, destination)
    return destination


def prepared(root, expected_hash):
    root = Path(root).resolve()
    report = json.loads((root / PUBLISHER).read_text())
    if sha(root / 'notary-submission.zip') != expected_hash or report['submission_sha256'] != expected_hash:
        raise ValueError('Submission archive differs from explicitly approved hash')
    if snapshot(root / 'core') != report['core_files'] or sha(root / 'input-provenance.json') != report['input_provenance_sha256']:
        raise ValueError('Prepared source/signing inventory changed')
    _, native = core(root / 'core', report['signed_provenance_sha256'], report['source_identity']['sha256'])
    for path, slices in native.items():
        if audit_signature(root / 'core' / path, slices, report['certificate_sha1'], report['team_id'],
                           code_identifier(report['identifier_prefix'], path)) != report['signatures'][path]:
            raise ValueError('Prepared signer evidence changed')
    with zipfile.ZipFile(root / 'notary-submission.zip') as archive:
        expected = {'core/' + p: e['sha256'] for p, e in report['core_files'].items()}
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != set(expected):
            raise ValueError('Notary ZIP inventory differs from signed core')
        for name, digest in expected.items():
            if archive.getinfo(name).file_size != (root / name).stat().st_size:
                raise ValueError('Notary ZIP size differs from signed core')
            with archive.open(name) as contents:
                if hashlib.file_digest(contents, 'sha256').hexdigest() != digest:
                    raise ValueError('Notary ZIP bytes differ from signed core')
    return report


def submit(root, output, archive_hash, profile):
    prepared(root, archive_hash)
    destination = new_destination(output, Path(root).resolve())
    destination.mkdir()
    # Preserve an attempt record before the irreversible upload. Unknown outcome
    # never authorizes automatic resubmission or an Accepted claim.
    write_json(destination / 'attempt.json', {'archive_sha256': archive_hash, 'status': 'upload-requested'})
    try:
        result = subprocess.run(['/usr/bin/xcrun', 'notarytool', 'submit', str(Path(root).resolve() / 'notary-submission.zip'),
                             '--keychain-profile', profile, '--no-wait', '--output-format', 'json'],
                                capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired as error:
        for name, value in [('submit.stdout.json', error.stdout), ('submit.stderr.txt', error.stderr)]:
            (destination / name).write_bytes(value if isinstance(value, bytes) else (value or '').encode())
        raise ValueError('Apple upload outcome is unknown after timeout; review retained attempt before retry') from error
    (destination / 'submit.stdout.json').write_text(result.stdout)
    (destination / 'submit.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise ValueError('Apple submission did not complete; retained attempt must be reviewed before retry')
    response = json.loads(result.stdout)
    submission_id = str(uuid.UUID(response['id']))
    write_json(destination / 'submission.json', {'archive_sha256': archive_hash, 'submission_id': submission_id,
                                              'status': 'submitted-not-accepted', 'response': response})
    return destination


def accepted_log(info, log, submission_id, archive_hash):
    if str(uuid.UUID(info['id'])) != submission_id or str(uuid.UUID(log['jobId'])) != submission_id:
        raise ValueError('Apple result submission identity differs')
    if info.get('status') != 'Accepted' or log.get('status') != 'Accepted' or log.get('sha256') != archive_hash:
        raise ValueError('Apple has not reported Accepted for this exact archive')
    if any(issue.get('severity') == 'error' for issue in (log.get('issues') or [])):
        raise ValueError('Apple log contains errors')


def collect(root, submission, output, archive_hash, profile):
    prepared(root, archive_hash)
    record = json.loads(Path(submission).read_text())
    if record['archive_sha256'] != archive_hash:
        raise ValueError('Submission is for another archive')
    submission_id = str(uuid.UUID(record['submission_id']))
    destination = new_destination(output, Path(root).resolve())
    destination.mkdir()
    info = command('/usr/bin/xcrun', 'notarytool', 'info', submission_id, '--keychain-profile', profile, '--output-format', 'json').stdout
    (destination / 'info.json').write_text(info)
    log = command('/usr/bin/xcrun', 'notarytool', 'log', submission_id, '--keychain-profile', profile).stdout
    (destination / 'log.json').write_text(log)
    accepted_log(json.loads(info), json.loads(log), submission_id, archive_hash)
    prepared(root, archive_hash)
    write_json(destination / 'accepted.json', {'submission_id': submission_id, 'archive_sha256': archive_hash,
               'status': 'Apple-Accepted', 'info_sha256': sha(destination / 'info.json'), 'log_sha256': sha(destination / 'log.json'),
               'issues': json.loads(log).get('issues'), 'stapled': False, 'host_acceptance': 'not-established'})
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    inspect = commands.add_parser('inspect'); inspect.add_argument('core', type=Path)
    signing = commands.add_parser('sign'); signing.add_argument('core', type=Path); signing.add_argument('output', type=Path)
    signing.add_argument('--input-manifest-sha256', required=True); signing.add_argument('--source-sha256', required=True)
    signing.add_argument('--identity-sha1', required=True); signing.add_argument('--team-id', required=True)
    signing.add_argument('--identifier-prefix', required=True); signing.add_argument('--allow-apple-network', action='store_true')
    for action in ('submit', 'collect'):
        item = commands.add_parser(action); item.add_argument('prepared', type=Path); item.add_argument('output', type=Path)
        item.add_argument('--archive-sha256', required=True); item.add_argument('--keychain-profile', required=True)
        item.add_argument('--allow-apple-network', action='store_true')
        if action == 'collect': item.add_argument('--submission', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin': parser.error('Publisher workflow requires macOS developer tools')
    if args.action != 'inspect' and not args.allow_apple_network:
        parser.error('Explicit --allow-apple-network is required for timestamping or notary service contact')
    if args.action == 'inspect':
        provenance, native = core(args.core)
        print(json.dumps({'status': 'inspected-not-signed', 'input_provenance_sha256': sha(args.core / MANIFEST),
              'source_identity': provenance['source_identity'], 'architecture': provenance['architecture'],
              'native_files': sorted(native), 'notarization': 'not-submitted'}, indent=2))
    elif args.action == 'sign':
        print(sign(args.core, args.output, args.input_manifest_sha256, args.source_sha256, args.identity_sha1, args.team_id, args.identifier_prefix))
    elif args.action == 'submit': print(submit(args.prepared, args.output, args.archive_sha256, args.keychain_profile))
    else: print(collect(args.prepared, args.submission, args.output, args.archive_sha256, args.keychain_profile))


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
