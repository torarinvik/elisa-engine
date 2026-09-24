#!/usr/bin/env python3
"""Unit tests for the Wicked static archive ABI-tag checker."""

from __future__ import annotations

import unittest

from check_wicked_archive_abi import abi_versions


class AbiTagTests(unittest.TestCase):
    def test_extracts_one_libcxx_abi_tag(self) -> None:
        self.assertEqual(abi_versions("std::__1::basic_string [abi:nqn230101]"), {"230101"})

    def test_extracts_mixed_libcxx_abi_tags(self) -> None:
        dump = "std::vector [abi:nqn220106]\nstd::string [abi:nqn230101]"
        self.assertEqual(abi_versions(dump), {"220106", "230101"})

    def test_ignores_unrelated_abi_annotations(self) -> None:
        self.assertEqual(abi_versions("[abi:v160006] unrelated"), set())


if __name__ == "__main__":
    unittest.main()
