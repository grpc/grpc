# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from libcst._parser.grammar import _should_include
from libcst._parser.parso.utils import PythonVersionInfo
from libcst.testing.utils import data_provider, UnitTest


class VersionCompareTest(UnitTest):
    @data_provider(
        (
            # Simple equality
            ("==3.6", PythonVersionInfo(3, 6), True),
            ("!=3.6", PythonVersionInfo(3, 6), False),
            # Equal or GT/LT
            (">=3.6", PythonVersionInfo(3, 5), False),
            (">=3.6", PythonVersionInfo(3, 6), True),
            (">=3.6", PythonVersionInfo(3, 7), True),
            ("<=3.6", PythonVersionInfo(3, 5), True),
            ("<=3.6", PythonVersionInfo(3, 6), True),
            ("<=3.6", PythonVersionInfo(3, 7), False),
            # GT/LT
            (">3.6", PythonVersionInfo(3, 5), False),
            (">3.6", PythonVersionInfo(3, 6), False),
            (">3.6", PythonVersionInfo(3, 7), True),
            ("<3.6", PythonVersionInfo(3, 5), True),
            ("<3.6", PythonVersionInfo(3, 6), False),
            ("<3.6", PythonVersionInfo(3, 7), False),
            # Multiple checks
            (">3.6,<3.8", PythonVersionInfo(3, 6), False),
            (">3.6,<3.8", PythonVersionInfo(3, 7), True),
            (">3.6,<3.8", PythonVersionInfo(3, 8), False),
        )
    )
    def test_tokenize(
        self,
        requested_version: str,
        actual_version: PythonVersionInfo,
        expected_result: bool,
    ) -> None:
        self.assertEqual(
            _should_include(requested_version, actual_version), expected_result
        )
