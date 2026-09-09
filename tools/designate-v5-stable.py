#!/usr/bin/env python3
"""Internal backend of milestone-release stable; never treats latest as stable."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO = 'CXITRON/MILESTONE-Core'

def run(*args):
    return subprocess.run(args, check=True, capture_output=True).stdout

def main():
    version = sys.argv[1]
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Invalid version')
    remote = run('git', 'remote', 'get-url', 'origin').decode().strip()
    if remote not in (f'https://github.com/{REPO}.git', f'https://github.com/{REPO}',
                       f'git@github.com:{REPO}.git'):
        raise ValueError('Unexpected repository')
    private = Path(os.environ['MILESTONE_V5_PRIVATE_KEY'])
    public = Path(os.environ['MILESTONE_V5_PUBLIC_KEY'])
    with tempfile.TemporaryDirectory(prefix='milestone-stable-') as tmp:
        root = Path(tmp)
        run('gh', 'release', 'download', f'v{version}', '--repo', REPO, '--dir', tmp)
        # Use the same complete catalog verifier as the regular release.
        run('python3', str(Path(__file__).with_name('v5-release-assets.py')),
            'verify', str(root), '--version', version, '--public-key', str(public))
        original = (root / 'v5-bundle.txt').read_bytes()
        prefix = f'MILESTONE-V5 BUNDLE {version} '.encode()
        if not original.startswith(prefix):
            raise ValueError('Version mismatch')
        text = root / 'v5-stable.txt'
        sig = root / 'v5-stable.sig'
        text.write_bytes(original.replace(b'MILESTONE-V5 BUNDLE ', b'MILESTONE-V5 STABLE ', 1))
        run('openssl', 'dgst', '-sha256', '-sign', str(private), '-out', str(sig), str(text))
        run('openssl', 'dgst', '-sha256', '-verify', str(public), '-signature', str(sig), str(text))
        exists = subprocess.run(['gh', 'release', 'view', 'stable', '--repo', REPO],
                                capture_output=True).returncode == 0
        if exists:
            run('gh', 'release', 'upload', 'stable', str(text), str(sig), '--repo', REPO, '--clobber')
        else:
            run('gh', 'release', 'create', 'stable', str(text), str(sig), '--repo', REPO,
                '--target', f'v{version}', '--title', 'MILESTONE curated stable channel',
                '--notes', 'Administrator-designated signed SD recovery channel; not latest firmware.',
                '--prerelease', '--latest=false')
        verify = root / 'published'
        verify.mkdir()
        run('gh', 'release', 'download', 'stable', '--repo', REPO, '--dir', str(verify))
        for item in (text, sig):
            if hashlib.sha256(item.read_bytes()).digest() != hashlib.sha256((verify / item.name).read_bytes()).digest():
                raise ValueError('Published stable asset mismatch')
        print(f'Stable designation published and verified: {version}')

if __name__ == '__main__':
    main()
