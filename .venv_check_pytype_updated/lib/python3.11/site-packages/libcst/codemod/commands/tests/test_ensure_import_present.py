# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.ensure_import_present import EnsureImportPresentCommand


class EnsureImportPresentCommandTest(CodemodTest):
    TRANSFORM = EnsureImportPresentCommand

    def test_import_module(self) -> None:
        before = ""
        after = "import a"
        self.assertCodemod(before, after, module="a", entity=None, alias=None)

    def test_import_entity(self) -> None:
        before = ""
        after = "from a import b"
        self.assertCodemod(before, after, module="a", entity="b", alias=None)

    def test_import_wildcard(self) -> None:
        before = "from a import *"
        after = "from a import *"
        self.assertCodemod(before, after, module="a", entity="b", alias=None)

    def test_import_module_aliased(self) -> None:
        before = ""
        after = "import a as c"
        self.assertCodemod(before, after, module="a", entity=None, alias="c")

    def test_import_entity_aliased(self) -> None:
        before = ""
        after = "from a import b as c"
        self.assertCodemod(before, after, module="a", entity="b", alias="c")
