# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from libcst.codemod import CodemodTest
from libcst.codemod.commands.add_trailing_commas import AddTrailingCommas


class AddTrailingCommasTest(CodemodTest):
    TRANSFORM = AddTrailingCommas

    def test_transform_defines(self) -> None:
        before = """
        def f(x, y):
            pass

        """
        after = """
        def f(x, y,):
            pass
        """
        self.assertCodemod(before, after)

    def test_skip_transforming_defines(self) -> None:
        before = """
        # skip defines with no params.
        def f0():
            pass

        # skip defines with a single param named `self`.
        class Foo:
            def __init__(self):
                pass
        """
        after = before
        self.assertCodemod(before, after)

    def test_transform_calls(self) -> None:
        before = """
        f(a, b, c)

        g(x=a, y=b, z=c)
        """
        after = """
        f(a, b, c,)

        g(x=a, y=b, z=c,)
        """
        self.assertCodemod(before, after)

    def test_skip_transforming_calls(self) -> None:
        before = """
        # skip empty calls
        f()

        # skip calls with one argument
        g(a)
        g(x=a)
        """
        after = before
        self.assertCodemod(before, after)

    def test_using_yapf_presets(self) -> None:
        before = """
        def f(x):  # skip single parameters for yapf
            pass

        def g(x, y):
            pass
        """
        after = """
        def f(x):  # skip single parameters for yapf
            pass

        def g(x, y,):
            pass
        """
        self.assertCodemod(before, after, formatter="yapf")

    def test_using_custom_presets(self) -> None:
        before = """
        def f(x, y, z):
            pass

        f(5, 6, 7)
        """
        after = before
        self.assertCodemod(before, after, parameter_count=4, argument_count=4)
