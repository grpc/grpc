# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from pathlib import Path
from tempfile import TemporaryDirectory

from libcst.helpers.paths import chdir
from libcst.testing.utils import UnitTest


class PathsTest(UnitTest):
    def test_chdir(self) -> None:
        with TemporaryDirectory() as td:
            tdp = Path(td).resolve()
            inner = tdp / "foo" / "bar"
            inner.mkdir(parents=True)

            with self.subTest("string paths"):
                cwd1 = Path.cwd()

                with chdir(tdp.as_posix()) as path2:
                    cwd2 = Path.cwd()
                    self.assertEqual(tdp, cwd2)
                    self.assertEqual(tdp, path2)

                    with chdir(inner.as_posix()) as path3:
                        cwd3 = Path.cwd()
                        self.assertEqual(inner, cwd3)
                        self.assertEqual(inner, path3)

                    cwd4 = Path.cwd()
                    self.assertEqual(tdp, cwd4)
                    self.assertEqual(cwd2, cwd4)

                cwd5 = Path.cwd()
                self.assertEqual(cwd1, cwd5)

            with self.subTest("pathlib objects"):
                cwd1 = Path.cwd()

                with chdir(tdp) as path2:
                    cwd2 = Path.cwd()
                    self.assertEqual(tdp, cwd2)
                    self.assertEqual(tdp, path2)

                    with chdir(inner) as path3:
                        cwd3 = Path.cwd()
                        self.assertEqual(inner, cwd3)
                        self.assertEqual(inner, path3)

                    cwd4 = Path.cwd()
                    self.assertEqual(tdp, cwd4)
                    self.assertEqual(cwd2, cwd4)

                cwd5 = Path.cwd()
                self.assertEqual(cwd1, cwd5)
