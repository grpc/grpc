# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from textwrap import dedent
from typing import Dict

import libcst as cst
from libcst.codemod import (
    Codemod,
    CodemodContext,
    CodemodTest,
    SkipFile,
    transform_module,
    TransformExit,
    TransformFailure,
    TransformSkip,
    TransformSuccess,
)


class TestRunner(CodemodTest):
    def test_runner_default(self) -> None:
        before = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        after = """
            # A comment
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        class SimpleCodemod(Codemod):
            def transform_module_impl(self, tree: cst.Module) -> cst.Module:
                self.warn("Testing")
                return tree.with_changes(
                    header=[cst.EmptyLine(comment=cst.Comment("# A comment"))]
                )

        transform = SimpleCodemod(CodemodContext())
        response = transform_module(transform, dedent(before))
        self.assertIsInstance(response, TransformSuccess)
        assert isinstance(response, TransformSuccess)
        self.assertCodeEqual(response.code, after)
        self.assertEqual(response.warning_messages, ["Testing"])

    def test_runner_interrupted(self) -> None:
        code = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        class SimpleCodemod(Codemod):
            def transform_module_impl(self, tree: cst.Module) -> cst.Module:
                raise KeyboardInterrupt("Testing")

        transform = SimpleCodemod(CodemodContext())
        response = transform_module(transform, dedent(code))
        self.assertIsInstance(response, TransformExit)

    def test_runner_skip(self) -> None:
        code = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        class SimpleCodemod(Codemod):
            def transform_module_impl(self, tree: cst.Module) -> cst.Module:
                self.warn("Testing")
                raise SkipFile()

        transform = SimpleCodemod(CodemodContext())
        response = transform_module(transform, dedent(code))
        self.assertIsInstance(response, TransformSkip)
        self.assertEqual(response.warning_messages, ["Testing"])

    def test_runner_failure(self) -> None:
        code = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """

        class SimpleCodemod(Codemod):
            def transform_module_impl(self, tree: cst.Module) -> cst.Module:
                self.warn("Testing")
                somedict: Dict[str, str] = {}
                somedict["invalid_key"]
                return tree

        transform = SimpleCodemod(CodemodContext())
        response = transform_module(transform, dedent(code))
        self.assertIsInstance(response, TransformFailure)
        assert isinstance(response, TransformFailure)
        self.assertEqual(response.warning_messages, ["Testing"])
        self.assertIsInstance(response.error, KeyError)
