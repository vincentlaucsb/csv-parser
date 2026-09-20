#!/usr/bin/env python3
"""Copy classify_scalar verbatim from a pinned upstream commit and verify provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'vincentlaucsb/classify_scalar'
SOURCE = 'include/classify_scalar.hpp'
HEADER = ROOT / 'include/external/classify_scalar.hpp'
LOCK = ROOT / 'include/external/classify_scalar.json'


def fetch(url):
    request = Request(url, headers={'User-Agent': 'csv-parser-vendor-sync'})
    with urlopen(request, timeout=30) as response:
        return response.read()


def version(data):
    text = data.decode('utf-8')
    parts = []
    for part in ('MAJOR', 'MINOR', 'PATCH'):
        match = re.search(r'^#define CLASSIFY_SCALAR_VERSION_' + part + r' (\d+)$', text, re.M)
        if not match:
            raise ValueError('Missing version macro: ' + part)
        parts.append(int(match.group(1)))
    dotted = '.'.join(map(str, parts))
    numeric = parts[0] * 10000 + parts[1] * 100 + parts[2]
    for pattern in (rf'^#define CLASSIFY_SCALAR_VERSION {numeric}$',
                    rf'^#if CLASSIFY_SCALAR_VERSION >= {numeric}$',
                    rf'^classify_scalar, version {re.escape(dotted)}$'):
        if not re.search(pattern, text, re.M):
            raise ValueError('Inconsistent upstream version metadata')
    return dotted


def upstream(revision, local=None):
    if local:
        sha = subprocess.check_output(
            ['git', '-C', str(local), 'rev-parse', '--verify', revision + '^{commit}'], text=True).strip()
        data = subprocess.check_output(['git', '-C', str(local), 'show', sha + ':' + SOURCE])
    else:
        info = json.loads(fetch(f'https://api.github.com/repos/{REPOSITORY}/commits/{quote(revision, safe="")}'))
        sha = info['sha']
        if not re.fullmatch(r'[0-9a-f]{40}', sha):
            raise ValueError('Invalid upstream commit ID')
        data = fetch(f'https://raw.githubusercontent.com/{REPOSITORY}/{sha}/{SOURCE}')
    return sha, data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--ref', help='Upstream tag or commit to import')
    action.add_argument('--check', action='store_true', help='Verify local bytes and pinned upstream bytes')
    parser.add_argument('--source-repo', type=Path, help='Read committed data from a local upstream checkout')
    parser.add_argument('--offline', action='store_true', help='With --check, verify local metadata/checksum only')
    args = parser.parse_args()
    if args.offline and not args.check:
        parser.error('--offline requires --check')
    try:
        if args.check:
            lock = json.loads(LOCK.read_text(encoding='utf-8'))
            if lock['repository'] != REPOSITORY or lock['path'] != SOURCE:
                raise ValueError('Unexpected upstream source in provenance file')
            if not re.fullmatch(r'[0-9a-f]{40}', lock['commit']):
                raise ValueError('Provenance must pin a full commit ID')
            data = HEADER.read_bytes()
            if version(data) != lock['version'] or hashlib.sha256(data).hexdigest() != lock['sha256']:
                raise ValueError('Vendored header differs from its provenance; fix upstream and run the sync command')
            if not args.offline:
                sha, original = upstream(lock['commit'], args.source_repo)
                if sha != lock['commit'] or original != data:
                    raise ValueError('Vendored header does not match the pinned upstream commit')
            print(f"Verified classify_scalar {lock['version']} at {lock['commit']}")
        else:
            sha, data = upstream(args.ref, args.source_repo)
            lock = {'repository': REPOSITORY, 'path': SOURCE, 'commit': sha,
                    'version': version(data), 'sha256': hashlib.sha256(data).hexdigest()}
            HEADER.write_bytes(data)
            LOCK.write_text(json.dumps(lock, indent=2) + '\n', encoding='utf-8', newline='\n')
            print(f"Synced classify_scalar {lock['version']} at {sha}")
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
