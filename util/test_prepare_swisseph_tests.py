# SPDX-License-Identifier: GPL-2.0-or-later
# Swiss Ephemeris integration, 2026-10-23.
"""Offline regression checks for fixture preparation; no upstream data required."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import prepare_swisseph_tests as fixtures

class PreparationTests(unittest.TestCase):
    def test_verified_copy_resume_and_no_implicit_network(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); source=root/'source'; source.mkdir()
            (source/'sample.se1').write_bytes(b'fixture')
            lock=root/'lock'
            lock.write_text('sample.se1|7|'+hashlib.sha256(b'fixture').hexdigest()+'|https://example.invalid/sample\n')
            destination=root/'test-data'
            with patch.object(fixtures,'LOCK',lock), patch.object(fixtures.urllib.request,'urlopen') as network:
                with self.assertRaises(ValueError): fixtures.prepare(destination)
                fixtures.prepare(destination,source)
                fixtures.prepare(destination)
                self.assertEqual((destination/'sample.se1').read_bytes(),b'fixture')
                network.assert_not_called()
                (destination/'unexpected.se1').write_bytes(b'extra')
                with self.assertRaises(ValueError): fixtures.prepare(destination,source)

    def test_bad_source_cannot_replace_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); source=root/'source'; source.mkdir()
            destination=root/'dest'; destination.mkdir()
            (source/'sample.se1').write_bytes(b'wrong')
            (destination/'sample.se1').write_bytes(b'keep')
            lock=root/'lock'
            lock.write_text('sample.se1|7|'+hashlib.sha256(b'fixture').hexdigest()+'|https://example.invalid/sample\n')
            with patch.object(fixtures,'LOCK',lock):
                with self.assertRaises(ValueError): fixtures.prepare(destination,source)
            self.assertEqual((destination/'sample.se1').read_bytes(),b'keep')
            self.assertEqual(sorted(p.name for p in destination.iterdir()),['sample.se1'])

if __name__=='__main__': unittest.main()
