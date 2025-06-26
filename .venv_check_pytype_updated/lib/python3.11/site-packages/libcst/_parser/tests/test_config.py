# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-strict
from libcst._parser.parso.utils import PythonVersionInfo
from libcst._parser.types.config import _pick_compatible_python_version
from libcst.testing.utils import UnitTest


class ConfigTest(UnitTest):
    def test_pick_compatible(self) -> None:
        self.assertEqual(
            PythonVersionInfo(3, 1), _pick_compatible_python_version("3.2")
        )
        self.assertEqual(
            PythonVersionInfo(3, 1), _pick_compatible_python_version("3.1")
        )
        self.assertEqual(
            PythonVersionInfo(3, 8), _pick_compatible_python_version("3.9")
        )
        self.assertEqual(
            PythonVersionInfo(3, 8), _pick_compatible_python_version("3.10")
        )
        self.assertEqual(
            PythonVersionInfo(3, 8), _pick_compatible_python_version("4.0")
        )
        with self.assertRaisesRegex(
            ValueError,
            (
                r"No version found older than 1\.0 \(PythonVersionInfo\("
                + r"major=1, minor=0\)\) while running on"
            ),
        ):
            _pick_compatible_python_version("1.0")
