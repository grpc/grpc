# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst.codemod import CodemodTest
from libcst.codemod.commands.noop import NOOPCommand


class TestNOOPCodemod(CodemodTest):
    TRANSFORM = NOOPCommand

    def test_noop(self) -> None:
        before = """
            foo: str = ""

            class Class:
                pass

            def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                bar = ""
                return t

            bar = Class()
            foo(bar, baz="bla")
        """
        after = """
            foo: str = ""

            class Class:
                pass

            def foo(a: Class, **kwargs: str) -> Class:
                t: Class = Class()  # This is a comment
                bar = ""
                return t

            bar = Class()
            foo(bar, baz="bla")
        """

        self.assertCodemod(before, after)
