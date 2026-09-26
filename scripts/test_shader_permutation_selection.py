"""Validate authored shader selections before launching the compiler."""
import json
from pathlib import Path
import tempfile
import unittest

from shader_permutation_selection import PackageError, read_selection


class SelectionTests(unittest.TestCase):
    def test_rejects_malformed_and_escaping_selections(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "selection.json"
            for entries in ([], {}, [1], ["../escape.cso"], ["/absolute.cso"],
                    ["a.cso", "a.cso"], ["a//b.cso"], ["a.txt"], ["a\\b.cso"]):
                with self.subTest(entries=entries):
                    path.write_text(json.dumps(entries))
                    with self.assertRaises(PackageError):
                        read_selection(path)
