# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from typing import Tuple

import libcst as cst
from libcst import parse_module
from libcst._batched_visitor import BatchableCSTVisitor
from libcst._visitors import CSTVisitor
from libcst.metadata import (
    CodeRange,
    MetadataWrapper,
    PositionProvider,
    WhitespaceInclusivePositionProvider,
)
from libcst.metadata.position_provider import (
    PositionProvidingCodegenState,
    WhitespaceInclusivePositionProvidingCodegenState,
)
from libcst.testing.utils import UnitTest


def position(
    state: WhitespaceInclusivePositionProvidingCodegenState,
) -> Tuple[int, int]:
    return state.line, state.column


class PositionProviderTest(UnitTest):
    def test_visitor_provider(self) -> None:
        """
        Sets 2 metadata entries for every node:
            SimpleProvider -> 1
            DependentProvider - > 2
        """
        test = self

        class DependentVisitor(CSTVisitor):
            METADATA_DEPENDENCIES = (PositionProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                test.assertEqual(
                    self.get_metadata(PositionProvider, node), CodeRange((1, 0), (1, 4))
                )

        wrapper = MetadataWrapper(parse_module("pass"))
        wrapper.visit(DependentVisitor())

    def test_equal_range(self) -> None:
        test = self
        expected_range = CodeRange((1, 4), (1, 6))

        class EqualPositionVisitor(CSTVisitor):
            METADATA_DEPENDENCIES = (PositionProvider,)

            def visit_Equal(self, node: cst.Equal) -> None:
                test.assertEqual(
                    self.get_metadata(PositionProvider, node), expected_range
                )

            def visit_NotEqual(self, node: cst.NotEqual) -> None:
                test.assertEqual(
                    self.get_metadata(PositionProvider, node), expected_range
                )

        MetadataWrapper(parse_module("var == 1")).visit(EqualPositionVisitor())
        MetadataWrapper(parse_module("var != 1")).visit(EqualPositionVisitor())

    def test_batchable_provider(self) -> None:
        test = self

        class ABatchable(BatchableCSTVisitor):
            METADATA_DEPENDENCIES = (PositionProvider,)

            def visit_Pass(self, node: cst.Pass) -> None:
                test.assertEqual(
                    self.get_metadata(PositionProvider, node), CodeRange((1, 0), (1, 4))
                )

        wrapper = MetadataWrapper(parse_module("pass"))
        wrapper.visit_batched([ABatchable()])


class PositionProvidingCodegenStateTest(UnitTest):
    def test_codegen_initial_position(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        self.assertEqual(position(state), (1, 0))

    def test_codegen_add_token(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.add_token("1234")
        self.assertEqual(position(state), (1, 4))

    def test_codegen_add_tokens(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.add_token("1234\n1234")
        self.assertEqual(position(state), (2, 4))

    def test_codegen_add_newline(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.add_token("\n")
        self.assertEqual(position(state), (2, 0))

    def test_codegen_add_indent_tokens(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.increase_indent(state.default_indent)
        state.add_indent_tokens()
        self.assertEqual(position(state), (1, 4))

    def test_codegen_decrease_indent(self) -> None:
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.increase_indent(state.default_indent)
        state.increase_indent(state.default_indent)
        state.increase_indent(state.default_indent)
        state.decrease_indent()
        state.add_indent_tokens()
        self.assertEqual(position(state), (1, 8))

    def test_whitespace_inclusive_position(self) -> None:
        # create a dummy node
        node = cst.Pass()

        # simulate codegen behavior for the dummy node
        # generates the code " pass "
        state = WhitespaceInclusivePositionProvidingCodegenState(
            " " * 4, "\n", WhitespaceInclusivePositionProvider()
        )
        state.before_codegen(node)
        state.add_token(" ")
        with state.record_syntactic_position(node):
            state.add_token("pass")
        state.add_token(" ")
        state.after_codegen(node)

        # check whitespace is correctly recorded
        self.assertEqual(state.provider._computed[node], CodeRange((1, 0), (1, 6)))

    def test_position(self) -> None:
        # create a dummy node
        node = cst.Pass()

        # simulate codegen behavior for the dummy node
        # generates the code " pass "
        state = PositionProvidingCodegenState(" " * 4, "\n", PositionProvider())
        state.before_codegen(node)
        state.add_token(" ")
        with state.record_syntactic_position(node):
            state.add_token("pass")
        state.add_token(" ")
        state.after_codegen(node)

        # check syntactic position ignores whitespace
        self.assertEqual(state.provider._computed[node], CodeRange((1, 1), (1, 5)))
