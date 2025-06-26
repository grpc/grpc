# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from textwrap import dedent

import libcst as cst
from libcst import parse_module
from libcst.codemod import CodemodContext, ContextAwareTransformer, ContextAwareVisitor
from libcst.metadata import PositionProvider
from libcst.testing.utils import UnitTest


class TestingCollector(ContextAwareVisitor):
    METADATA_DEPENDENCIES = (PositionProvider,)

    def visit_Pass(self, node: cst.Pass) -> None:
        position = self.get_metadata(PositionProvider, node)
        self.context.scratch["pass"] = (position.start.line, position.start.column)


class TestingTransform(ContextAwareTransformer):
    METADATA_DEPENDENCIES = (PositionProvider,)

    def visit_FunctionDef(self, node: cst.FunctionDef) -> None:
        position = self.get_metadata(PositionProvider, node)
        self.context.scratch[node.name.value] = (
            position.start.line,
            position.start.column,
        )
        node.visit(TestingCollector(self.context))


class TestMetadata(UnitTest):
    def test_metadata_works(self) -> None:
        code = """
            def foo() -> None:
                pass

            def bar() -> int:
                return 5
        """
        module = parse_module(dedent(code))
        context = CodemodContext()
        transform = TestingTransform(context)
        transform.transform_module(module)
        self.assertEqual(
            context.scratch, {"foo": (2, 0), "pass": (3, 4), "bar": (5, 0)}
        )
