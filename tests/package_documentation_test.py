"""Check installed product Markdown links, including transitive skill guides.

Usage: python tests/package_documentation_test.py BUNDLE_OR_PLUGIN_DIRECTORY
Optional SOURCE_DIRECTORY additionally checks guides were copied unchanged.
"""
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

PRODUCT_SKILLS = {'native-cad', 'cad-manufacturing', 'cad-sendcutsend',
                  'cad-source-parts', 'cad-print-handoff'}


def local_links(root, path):
    text = path.read_text(encoding='utf-8')
    destinations = re.findall(r'\]\(\s*<?([^\s)>]+)>?(?:\s+"[^"]*")?\s*\)', text)
    destinations += re.findall(r'^\s*\[[^\]]+\]:\s*<?([^\s>]+)>?', text, re.M)
    # The main native skill also names its colocated product manuals in prose.
    if path.name == 'SKILL.md' and path.parent.name == 'native-cad':
        destinations += list(set(re.findall(r'\b[A-Z][A-Z_0-9]*\.md', text)) - {'SKILL.md'})
    count = 0
    for destination in destinations:
        parts = urlsplit(destination)
        if parts.scheme or parts.netloc or not parts.path:
            continue
        target = (path.parent / unquote(parts.path)).resolve()
        assert target.is_relative_to(root) and target.is_file(), f'Broken packaged link: {path} -> {destination}'
        count += 1
    return count


def verify_skills(root, skill_root, source=None):
    entrypoints = {path.parent.name: path for path in skill_root.glob('*/SKILL.md')}
    assert PRODUCT_SKILLS <= entrypoints.keys(), f'Missing packaged skills: {PRODUCT_SKILLS - entrypoints.keys()}'
    links = 0
    for name, entrypoint in entrypoints.items():
        text = entrypoint.read_text(encoding='utf-8')
        header = re.match(r'^---\r?\n(.*?)\r?\n---(?:\r?\n|$)', text, re.S)
        assert header, f'Missing skill frontmatter: {entrypoint}'
        fields = dict(re.findall(r'^(name|description):\s*(.+)$', header[1], re.M))
        assert fields.get('name') == name and fields.get('description', '').strip(), f'Invalid discovery fields: {entrypoint}'
        assert re.fullmatch(r'[a-z0-9-]{1,64}', name), f'Invalid skill name: {name}'
        for path in entrypoint.parent.rglob('*.md'):
            # native-cad manuals are checked by the product-layout pass below.
            if name != 'native-cad':
                links += local_links(root, path)
        if source and name in PRODUCT_SKILLS:
            original = Path(source) / 'skills' / name
            for path in original.rglob('*'):
                if path.is_file():
                    copy = entrypoint.parent / path.relative_to(original)
                    assert copy.is_file() and copy.read_bytes() == path.read_bytes(), f'Missing/rewritten skill resource: {copy}'
    return links


def verify(root, source=None):
    root = Path(root).resolve()
    docs = root / 'share/agent-3d-cad'
    directories = [docs, docs / 'skills/native-cad']
    if (root / 'plugin.json').is_file():
        directories.append(root / 'skills/native-cad')
    required = {'COMMIT_RECEIPTS', 'PARAMETRIC_EXPRESSIONS', 'EXPORT_DOWNLOADS', 'MESH_RECONSTRUCTION',
                'PARITY_SHEET_METAL', 'PARITY_SOLIDS', 'PARITY_SURFACES', 'PARITY_CURVES',
                'PARITY_AUTHORING', 'RELEASE_1_0', 'LOCAL_PACKAGE_ACCEPTANCE',
                'MARKUP', 'PROGRESSIVE_VIEWER'}
    skill_roots = [docs / 'skills']
    if (root / 'plugin.json').is_file():
        skill_roots.append(root / 'skills')
    links = sum(verify_skills(root, skill_root, source) for skill_root in skill_roots)
    reference = docs / 'examples/articulated-gripper'
    for name in ('README.md', 'generate.py', 'verify.py', 'gripper.create.json',
                 'gripper.close.edit.json', 'gripper.review.json', 'gripper.manufacture.json'):
        path = reference / name
        assert path.is_file(), f'Missing editable reference resource: {path}'
        if source:
            original = Path(source) / 'examples/articulated-gripper' / name
            assert path.read_bytes() == original.read_bytes(), f'Rewritten reference resource: {path}'
    links += local_links(root, reference / 'README.md')
    assert not list(reference.rglob('*.pyc')), 'Developer bytecode cache leaked into the reference package'
    for directory in directories:
        for name in required:
            assert (directory / (name + '.md')).is_file(), f'Missing product guide: {directory / name}'
        for path in directory.glob('*.md'):
            if source and path.name not in ('SKILL.md', 'HANDOFF.md'):
                assert path.read_bytes() == (Path(source) / 'docs' / path.name).read_bytes(), f'Rewritten product guide: {path}'
            links += local_links(root, path)
        stub = (directory / 'HANDOFF.md').read_text(encoding='utf-8')
        assert 'implementation ledger is not included' in stub and 'latest repository state' in stub
    return links, len(directories)


if __name__ == '__main__':
    links, copies = verify(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    print(f'{links} packaged relative documentation links resolve across {copies} product/skill layouts')
