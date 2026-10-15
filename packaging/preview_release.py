"""Prepare reproducible CI source and publish complete, immutable previews.

Developer/CI only. No branch is advanced, and no tag is pushed until all native
and universal runtime gates have passed. GitHub CLI supplies authentication.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

PREVIEW = re.compile(r'(0\.[0-9]+\.[0-9]+)-preview\.([1-9][0-9]*)')
SHA = re.compile(r'[0-9a-f]{40}')


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def git(*args, **kwargs):
    return run('git', *args, **kwargs)


def gh(*args):
    return run('gh', *args)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def prepare(event, ref, run_number, bundle):
    original = git('rev-parse', 'HEAD')
    require(SHA.fullmatch(original), 'Invalid source commit')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Tracked source must be clean')
    version = Path('VERSION').read_text().strip()
    match = PREVIEW.fullmatch(version)
    publish = bool(match and event == 'push' and (ref == 'refs/heads/main' or ref == 'refs/tags/v' + version))
    source = original
    if match and event == 'push' and ref == 'refs/heads/main':
        require(re.fullmatch(r'[1-9][0-9]*', str(run_number)), 'Invalid workflow run number')
        # One workflow counter, including branch/PR runs: gaps are intentional.
        # Retries retain run_number, so they reproduce this exact source commit.
        version = f'{match[1]}-preview.{run_number}'
        Path('VERSION').write_bytes((version + '\n').encode())
        git('add', '--', 'VERSION')
        if git('diff', '--cached', '--name-only'):
            require(git('diff', '--cached', '--name-only') == 'VERSION', 'Unexpected release source changes')
            timestamp = git('show', '-s', '--format=%ct', original) + ' +0000'
            environment = dict(os.environ, GIT_AUTHOR_NAME='github-actions[bot]',
                GIT_AUTHOR_EMAIL='41898282+github-actions[bot]@users.noreply.github.com',
                GIT_COMMITTER_NAME='github-actions[bot]',
                GIT_COMMITTER_EMAIL='41898282+github-actions[bot]@users.noreply.github.com',
                GIT_AUTHOR_DATE=timestamp, GIT_COMMITTER_DATE=timestamp)
            source = git('-c', 'commit.gpgsign=false', 'commit-tree', git('write-tree'),
                         '-p', original, '-m', f'Prepare v{version} from {original}', env=environment)
            git('checkout', '--detach', source)
            bundle.parent.mkdir(parents=True, exist_ok=True)
            git('bundle', 'create', str(bundle.resolve()), 'HEAD', '^' + original)
    return {'commit': source, 'parent': original, 'version': version, 'publish': str(publish).lower()}


def verify_assets(release, assets, complete):
    remote = {asset['name']: asset for asset in release['assets']}
    require(len(remote) == len(release['assets']), 'Duplicate uploaded assets')
    require(remote.keys() <= assets.keys(), 'Unexpected uploaded assets')
    if complete:
        require(remote.keys() == assets.keys(), 'Incomplete uploaded release')
    for name, asset in remote.items():
        path = assets[name]
        require(asset['state'] == 'uploaded' and asset['size'] == path.stat().st_size
                and asset['digest'] == 'sha256:' + digest(path), 'Uploaded bytes differ: ' + name)
    return [str(path) for name, path in sorted(assets.items()) if name not in remote]


def find_release(repository, tag):
    pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repository}/releases?per_page=100'))
    matches = [release for page in pages for release in page if release['tag_name'] == tag]
    require(len(matches) <= 1, 'Duplicate releases for tag')
    return matches[0] if matches else None


def publish(repository, commit, parent, run_id, directory, acceptance):
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository), 'Invalid repository')
    require(SHA.fullmatch(commit) and SHA.fullmatch(parent), 'Invalid release source commit')
    require(re.fullmatch(r'[1-9][0-9]*', run_id), 'Invalid workflow run ID')
    require(git('rev-parse', 'HEAD') == commit, 'Release source differs from tested commit')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Release source is dirty')
    version = Path('VERSION').read_text().strip()
    require(PREVIEW.fullmatch(version), 'Only v0 development previews may be published')
    tag = 'v' + version
    # The workflow gates supply the complete native matrix; recheck the exact
    # accepted universal archives here before creating any remote tag/release.
    report = json.loads(acceptance.read_text())
    universal = {f'agent-3d-cad-{version}-Darwin-universal.tar.gz',
                 f'agent-cad-plugin-{version}-Darwin-universal.zip',
                 f'agent-cad-claude-{version}-Darwin-universal.mcpb'}
    require(report['status'] == 'passed' and report['source_commit'] == commit,
            'Missing acceptance for tested source')
    require(set(report['archives']) == universal, 'Incomplete universal acceptance')
    for name, expected in report['archives'].items():
        require(digest(directory / name) == expected, 'Universal archive differs from accepted bytes')
    run('python3', 'packaging/release-manifest.py', str(directory))
    assets = {path.name: path for path in directory.iterdir()}
    require(len(assets) == 19, 'Expected 18 release assets plus SHA256SUMS')
    existing_tag = git('ls-remote', '--refs', 'origin', 'refs/tags/' + tag)
    if existing_tag:
        git('fetch', '--no-tags', 'origin', 'refs/tags/' + tag)
        require(git('rev-parse', 'FETCH_HEAD^{commit}') == commit, 'Existing tag points to different source')
    release = find_release(repository, tag)
    if release:
        require(release['target_commitish'] == commit and release['prerelease'], 'Existing release identity differs')
        missing = verify_assets(release, assets, complete=not release['draft'])
        if not release['draft']:
            require(bool(existing_tag), 'Published release has no verified tag')
            print('Already published and verified: ' + release['html_url'])
            return
    else:
        missing = [str(path) for path in assets.values()]
    if not existing_tag:
        # Push the tested commit directly as a new lightweight tag. Never force.
        git('push', 'origin', commit + ':refs/tags/' + tag)
    if not release:
        url = f'https://github.com/{repository}'
        notes = (f'# Agent CAD {version}\n\n'
            f'Automatic development preview from [main source {parent[:7]}]({url}/commit/{parent}), '
            f'with [versioned source {commit[:7]}]({url}/commit/{commit}).\n\n'
            f'[CI run {run_id}]({url}/actions/runs/{run_id}) passed all five native platform lanes '
            'and the same universal macOS packages on actual Apple Silicon and Intel CPUs.\n\n'
            'Downloads include native engines, local Codex / ChatGPT plugins, Claude Desktop '
            'extensions, macOS universal packages, both installers, and SHA256SUMS.\n\n'
            f'[Installation guide]({url}/blob/{commit}/docs/GETTING_STARTED.md). '
            'Use Darwin-universal on macOS. Keep saved projects outside the installation folder.\n\n'
            'Packages are not publisher-signed or notarized. Full host installation, update, '
            'restart/reopen and file retrieval acceptance remains unfinished. This is a '
            'development prerelease, not a 1.0 release.\n')
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'notes.md'
            path.write_text(notes)
            gh('release', 'create', tag, '--repo', repository, '--verify-tag', '--target', commit,
               '--draft', '--prerelease', '--title', 'Agent CAD ' + tag, '--notes-file', str(path))
    if missing:
        gh('release', 'upload', tag, *missing, '--repo', repository)
    release = find_release(repository, tag)
    require(release is not None, 'Uploaded draft is missing')
    verify_assets(release, assets, complete=True)
    gh('release', 'edit', tag, '--repo', repository, '--draft=false', '--prerelease', '--latest=false')
    release = find_release(repository, tag)
    require(release is not None and not release['draft'], 'Release did not publish')
    verify_assets(release, assets, complete=True)
    print('Published and verified: ' + release['html_url'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--bundle', type=Path, required=True)
    publication = commands.add_parser('publish')
    publication.add_argument('--commit', required=True)
    publication.add_argument('--parent', required=True)
    publication.add_argument('--directory', type=Path, required=True)
    publication.add_argument('--acceptance', type=Path, required=True)
    args = parser.parse_args()
    event, ref = os.environ['GITHUB_EVENT_NAME'], os.environ['GITHUB_REF']
    if args.command == 'prepare':
        result = prepare(event, ref, os.environ['GITHUB_RUN_NUMBER'], args.bundle)
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            for key, value in result.items():
                output.write(f'{key}={value}\n')
        print(json.dumps(result))
    else:
        require(event == 'push' and (ref == 'refs/heads/main' or ref == 'refs/tags/v' + Path('VERSION').read_text().strip()),
                'Publication requires a main or matching preview-tag push')
        publish(os.environ['GITHUB_REPOSITORY'], args.commit, args.parent, os.environ['GITHUB_RUN_ID'],
                args.directory, args.acceptance)


if __name__ == '__main__':
    main()
