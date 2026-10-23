#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Swiss Ephemeris integration, 2026-10-23.
"""Prepare checksum-pinned Swiss fixtures. No automatic downloads or data redistribution."""
import argparse
import hashlib
from pathlib import Path
import tempfile
import urllib.request

LOCK = Path(__file__).resolve().parents[1] / 'src/tests/swiss-fixtures.lock'


def prepare(destination, source=None, download=False):
    rows = [line.split('|') for line in LOCK.read_text().splitlines() if line and not line.startswith('#')]
    expected = {row[0] for row in rows}
    extras = {p.relative_to(destination).as_posix() for p in destination.rglob('*.se1')} - expected
    if extras:
        raise ValueError(f'Use a dedicated fixture directory; unexpected files: {sorted(extras)}')
    for name, size, digest, url in rows:
        target = destination / name
        def valid(data):
            return len(data) == int(size) and hashlib.sha256(data).hexdigest() == digest
        if target.is_file() and valid(target.read_bytes()):
            continue
        if source is not None:
            data = (source / name).read_bytes()
        elif download:
            with urllib.request.urlopen(url, timeout=90) as response:
                data = response.read(int(size) + 1)
        else:
            raise ValueError(f'Missing/invalid {target}; specify --from-directory or --download')
        if not valid(data):
            raise ValueError(f'{name}: size/SHA-256 mismatch; fixture lock was not changed')
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as out:
                temporary = Path(out.name)
                out.write(data)
            temporary.replace(target)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    print(f'Verified {len(rows)} Swiss fixtures in {destination}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=Path('build/test-data/swiss'))
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--from-directory', type=Path, help='Copy verified files from an existing local download')
    mode.add_argument('--download', action='store_true', help='Explicitly fetch external data; review upstream terms first')
    args = parser.parse_args()
    try:
        prepare(args.destination, args.from_directory, args.download)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
