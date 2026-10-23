#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch pinned Swiss Ephemeris source code and license notices only.

Default destination: build/swisseph-deps (ignored by Git). No installation,
Git operations or downloads at CMake time. Download ephemeris data through
Stellarium's GUI after building. See doc/swisseph-prototype.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

REVISION = 'aacf962d19d79f8bc921dbcdaacf306d85be1917'
BASE = f'https://raw.githubusercontent.com/aloistr/swisseph/{REVISION}/'


def download(relative, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    data = urllib.request.urlopen(BASE + relative, timeout=90).read()
    temporary = destination.with_suffix(destination.suffix + '.part')
    temporary.write_bytes(data)
    temporary.replace(destination)
    print(relative, len(data), flush=True)
    return {'upstream': relative, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=Path('build/swisseph-deps'),
                        help='Dependency directory (source is written to its source/ subdirectory)')
    args = parser.parse_args(argv)
    manifest_path = args.destination / 'manifest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    records = []
    with urllib.request.urlopen(
            f'https://api.github.com/repos/aloistr/swisseph/git/trees/{REVISION}', timeout=90) as response:
        tree = json.load(response)
    for entry in tree['tree']:
        name = entry['path']
        if entry['type'] == 'blob' and (name.endswith(('.c', '.h', '.txt', '.TXT'))
                                      or name in ('LICENSE', 'Makefile', 'readme.md')):
            records.append(download(name, args.destination / 'source' / name))
    # Preserve receipts for data fetched by older versions without downloading,
    # moving or deleting any of those files.
    old_data = [record for record in manifest.get('files', [])
                if record.get('upstream', '').startswith('ephe/')]
    manifest.update({'revision': REVISION, 'files': records + old_data})
    args.destination.mkdir(parents=True, exist_ok=True)
    temporary = manifest_path.with_suffix('.json.part')
    temporary.write_text(json.dumps(manifest, indent=2) + '\n')
    temporary.replace(manifest_path)
    print('Source ready. Build Stellarium, then download ephemerides in the Swiss Ephemeris GUI tab.')


if __name__ == '__main__':
    main()
