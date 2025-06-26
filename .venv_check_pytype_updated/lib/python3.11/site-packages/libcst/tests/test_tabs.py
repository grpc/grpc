# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from libcst._tabs import expand_tabs
from libcst.testing.utils import data_provider, UnitTest


class ExpandTabsTest(UnitTest):
    @data_provider(
        [
            ("\t", " " * 8),
            ("\t\t", " " * 16),
            ("    \t", " " * 8),
            ("\t    ", " " * 12),
            ("abcd\t", "abcd    "),
            ("abcdefg\t", "abcdefg "),
            ("abcdefgh\t", "abcdefgh        "),
            ("\tsuffix", "        suffix"),
        ]
    )
    def test_expand_tabs(self, input: str, output: str) -> None:
        self.assertEqual(expand_tabs(input), output)
