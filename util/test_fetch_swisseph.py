#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Network-free regression checks for the source-only Swiss dependency fetcher."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

from fetch_swisseph import BASE, REVISION, download, main


class DownloadTests(unittest.TestCase):
    def test_fetches_source_and_notices_but_no_ephemerides(self):
        entries = [
            {'path': name, 'type': 'blob'}
            for name in ('sweph.c', 'swephexp.h', 'LICENSE', 'agpl-3.0.txt',
                         'readme.md', 'seas_18.se1', 'ephe/sepl_18.se1')]
        entries.append({'path': 'ephe', 'type': 'tree'})
        tree = json.dumps({'tree': entries}).encode()
        payload = b'test source or license text\n'
        urls = []

        def respond(url, **kwargs):
            urls.append(url)
            if url.startswith('https://api.github.com/'):
                return io.BytesIO(tree)
            self.assertFalse(url.endswith('.se1'))
            return io.BytesIO(payload)

        with tempfile.TemporaryDirectory() as directory:
            with patch('urllib.request.urlopen', side_effect=respond), contextlib.redirect_stdout(io.StringIO()):
                main(['--destination', directory])
            root = Path(directory)
            self.assertFalse((root / 'ephe').exists())
            self.assertEqual({p.name for p in (root / 'source').iterdir()},
                             {'sweph.c', 'swephexp.h', 'LICENSE', 'agpl-3.0.txt', 'readme.md'})
            manifest = json.loads((root / 'manifest.json').read_text())
            self.assertEqual(manifest['revision'], REVISION)
            self.assertEqual(len(manifest['files']), 5)
            self.assertTrue(all(row['sha256'] == hashlib.sha256(payload).hexdigest()
                                for row in manifest['files']))
            self.assertTrue(all(REVISION in url for url in urls))

    def test_preserves_legacy_data_and_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'ephe/seas_18.se1'
            data.parent.mkdir()
            data.write_bytes(b'existing ephemeris')
            receipt = {'upstream': 'ephe/seas_18.se1', 'sha256': 'old-data-checksum'}
            old = {'files': [receipt], 'asteroid_files': {'ast0/se00005.se1': {'bytes': 123}}}
            (root / 'manifest.json').write_text(json.dumps(old))
            tree = json.dumps({'tree': [{'type': 'blob', 'path': 'LICENSE'}]}).encode()
            with patch('urllib.request.urlopen', side_effect=[io.BytesIO(tree), io.BytesIO(b'license')]), contextlib.redirect_stdout(io.StringIO()):
                main(['--destination', directory])
            manifest = json.loads((root / 'manifest.json').read_text())
            self.assertIn(receipt, manifest['files'])
            self.assertEqual(manifest['asteroid_files'], old['asteroid_files'])
            self.assertEqual(data.read_bytes(), b'existing ephemeris')

    def test_failed_download_preserves_existing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sweph.c'
            path.write_bytes(b'existing source')
            with patch('urllib.request.urlopen', side_effect=HTTPError(BASE, 503, 'Unavailable', {}, None)):
                with self.assertRaises(HTTPError):
                    download('sweph.c', path)
            self.assertEqual(path.read_bytes(), b'existing source')

    def test_removed_data_options_fail_before_network_access(self):
        for args in (['--asteroids', '5'], ['--asteroids-only'], ['--length', 'short']):
            with self.subTest(args=args), patch('urllib.request.urlopen') as request, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as result:
                    main(args)
                self.assertEqual(result.exception.code, 2)
                request.assert_not_called()


if __name__ == '__main__':
    unittest.main()
