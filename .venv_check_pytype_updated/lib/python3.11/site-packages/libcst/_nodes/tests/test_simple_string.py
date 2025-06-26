# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import unittest

import libcst as cst


class TestSimpleString(unittest.TestCase):
    def test_quote(self) -> None:
        test_cases = [
            ('"a"', '"'),
            ("'b'", "'"),
            ('""', '"'),
            ("''", "'"),
            ('"""c"""', '"""'),
            ("'''d'''", "'''"),
            ('""""e"""', '"""'),
            ("''''f'''", "'''"),
            ('"""""g"""', '"""'),
            ("'''''h'''", "'''"),
            ('""""""', '"""'),
            ("''''''", "'''"),
        ]

        for s, expected_quote in test_cases:
            simple_string = cst.SimpleString(s)
            actual = simple_string.quote
            self.assertEqual(expected_quote, actual)
