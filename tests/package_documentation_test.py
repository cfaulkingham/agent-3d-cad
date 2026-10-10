"""Check installed product Markdown links, including transitive skill guides.

Usage: python tests/package_documentation_test.py BUNDLE_OR_PLUGIN_DIRECTORY
Optional SOURCE_DIRECTORY additionally checks guides were copied unchanged.
"""
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit


def verify(root, source=None):
    root = Path(root).resolve()
    docs = root / 'share/agent-3d-cad'
    directories = [docs, docs / 'skills/native-cad']
    if (root / 'plugin.json').is_file():
        directories.append(root / 'skills/native-cad')
    required = {'COMMIT_RECEIPTS', 'PARAMETRIC_EXPRESSIONS', 'EXPORT_DOWNLOADS', 'MESH_RECONSTRUCTION',
                'PARITY_SHEET_METAL', 'PARITY_SOLIDS', 'PARITY_SURFACES', 'PARITY_CURVES',
                'PARITY_AUTHORING', 'RELEASE_1_0', 'LOCAL_PACKAGE_ACCEPTANCE'}
    links = 0
    for directory in directories:
        for name in required:
            assert (directory / (name + '.md')).is_file(), f'Missing product guide: {directory / name}'
        for path in directory.glob('*.md'):
            text = path.read_text(encoding='utf-8')
            if source and path.name not in ('SKILL.md', 'HANDOFF.md'):
                assert path.read_bytes() == (Path(source) / 'docs' / path.name).read_bytes(), f'Rewritten product guide: {path}'
            # Inline and reference-style Markdown destinations. Absolute URLs and
            # anchor-only links are outside this local-file existence check.
            destinations = re.findall(r'\]\(\s*<?([^\s)>]+)>?(?:\s+"[^"]*")?\s*\)', text)
            destinations += re.findall(r'^\s*\[[^\]]+\]:\s*<?([^\s>]+)>?', text, re.M)
            if path.name == 'SKILL.md':
                destinations += list(set(re.findall(r'\b[A-Z][A-Z_0-9]*\.md', text)) - {'SKILL.md'})
            for destination in destinations:
                parts = urlsplit(destination)
                if parts.scheme or parts.netloc or not parts.path:
                    continue
                target = (path.parent / unquote(parts.path)).resolve()
                assert target.is_relative_to(root) and target.is_file(), f'Broken packaged link: {path} -> {destination}'
                links += 1
        stub = (directory / 'HANDOFF.md').read_text(encoding='utf-8')
        assert 'implementation ledger is not included' in stub and 'latest repository state' in stub
    return links, len(directories)


if __name__ == '__main__':
    links, copies = verify(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    print(f'{links} packaged relative documentation links resolve across {copies} product/skill layouts')
