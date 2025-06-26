# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from textwrap import dedent
from typing import Union

import libcst as cst
from libcst._removal_sentinel import RemovalSentinel
from libcst._types import CSTNodeT
from libcst._visitors import CSTTransformer
from libcst.testing.utils import data_provider, none_throws, UnitTest

_EMPTY_SIMPLE_WHITESPACE = cst.SimpleWhitespace("")


class _TestVisitor(CSTTransformer):
    def __init__(self, test: UnitTest) -> None:
        self.counter = 0
        self.test = test

    def assert_counter(self, expected: int) -> None:
        self.test.assertEqual(self.counter, expected)
        self.counter += 1

    def on_visit(self, node: cst.CSTNode) -> bool:
        if isinstance(node, cst.Module):
            self.assert_counter(0)
        elif isinstance(node, cst.SimpleStatementLine):
            self.assert_counter(1)
        elif isinstance(node, cst.Pass):
            self.assert_counter(2)
        elif isinstance(node, cst.Newline):
            self.assert_counter(4)
        return True

    def on_leave(
        self, original_node: CSTNodeT, updated_node: CSTNodeT
    ) -> Union[CSTNodeT, RemovalSentinel]:
        self.test.assertTrue(original_node.deep_equals(updated_node))
        # Don't allow type checkers to accidentally refine our return type.
        return_node = updated_node
        if isinstance(updated_node, cst.Pass):
            self.assert_counter(3)
        elif isinstance(updated_node, cst.Newline):
            self.assert_counter(5)
        elif isinstance(updated_node, cst.SimpleStatementLine):
            self.assert_counter(6)
        elif isinstance(updated_node, cst.Module):
            self.assert_counter(7)
        return return_node


class CSTNodeTest(UnitTest):
    def test_with_changes(self) -> None:
        initial = cst.TrailingWhitespace(
            whitespace=cst.SimpleWhitespace("  \\\n  "),
            comment=cst.Comment("# initial"),
            newline=cst.Newline("\r\n"),
        )
        changed = initial.with_changes(comment=cst.Comment("# new comment"))

        # see that we have the updated fields
        self.assertEqual(none_throws(changed.comment).value, "# new comment")
        # and that the old fields are still there
        self.assertEqual(changed.whitespace.value, "  \\\n  ")
        self.assertEqual(changed.newline.value, "\r\n")

        # ensure no mutation actually happened
        self.assertEqual(none_throws(initial.comment).value, "# initial")

    def test_default_eq(self) -> None:
        sw1 = cst.SimpleWhitespace("")
        sw2 = cst.SimpleWhitespace("")
        self.assertNotEqual(sw1, sw2)
        self.assertEqual(sw1, sw1)
        self.assertEqual(sw2, sw2)
        self.assertTrue(sw1.deep_equals(sw2))
        self.assertTrue(sw2.deep_equals(sw1))

    def test_hash(self) -> None:
        sw1 = cst.SimpleWhitespace("")
        sw2 = cst.SimpleWhitespace("")
        self.assertNotEqual(hash(sw1), hash(sw2))
        self.assertEqual(hash(sw1), hash(sw1))
        self.assertEqual(hash(sw2), hash(sw2))

    @data_provider(
        {
            "simple": (cst.SimpleWhitespace(""), cst.SimpleWhitespace("")),
            "identity": (_EMPTY_SIMPLE_WHITESPACE, _EMPTY_SIMPLE_WHITESPACE),
            "nested": (
                cst.EmptyLine(whitespace=cst.SimpleWhitespace("")),
                cst.EmptyLine(whitespace=cst.SimpleWhitespace("")),
            ),
            "tuple_versus_list": (
                cst.SimpleStatementLine(body=[cst.Pass()]),
                cst.SimpleStatementLine(body=(cst.Pass(),)),
            ),
        }
    )
    def test_deep_equals_success(self, a: cst.CSTNode, b: cst.CSTNode) -> None:
        self.assertTrue(a.deep_equals(b))

    @data_provider(
        {
            "simple": (cst.SimpleWhitespace(" "), cst.SimpleWhitespace("     ")),
            "nested": (
                cst.EmptyLine(whitespace=cst.SimpleWhitespace(" ")),
                cst.EmptyLine(whitespace=cst.SimpleWhitespace("       ")),
            ),
            "list": (
                cst.SimpleStatementLine(body=[cst.Pass(semicolon=cst.Semicolon())]),
                cst.SimpleStatementLine(body=[cst.Pass(semicolon=cst.Semicolon())] * 2),
            ),
        }
    )
    def test_deep_equals_fails(self, a: cst.CSTNode, b: cst.CSTNode) -> None:
        self.assertFalse(a.deep_equals(b))

    def test_repr(self) -> None:
        self.assertEqual(
            repr(
                cst.SimpleStatementLine(
                    body=[cst.Pass()],
                    # tuple with multiple items
                    leading_lines=(
                        cst.EmptyLine(
                            indent=True,
                            whitespace=cst.SimpleWhitespace(""),
                            comment=None,
                            newline=cst.Newline(),
                        ),
                        cst.EmptyLine(
                            indent=True,
                            whitespace=cst.SimpleWhitespace(""),
                            comment=None,
                            newline=cst.Newline(),
                        ),
                    ),
                    trailing_whitespace=cst.TrailingWhitespace(
                        whitespace=cst.SimpleWhitespace(" "),
                        comment=cst.Comment("# comment"),
                        newline=cst.Newline(),
                    ),
                )
            ),
            dedent(
                """
                SimpleStatementLine(
                    body=[
                        Pass(
                            semicolon=MaybeSentinel.DEFAULT,
                        ),
                    ],
                    leading_lines=[
                        EmptyLine(
                            indent=True,
                            whitespace=SimpleWhitespace(
                                value='',
                            ),
                            comment=None,
                            newline=Newline(
                                value=None,
                            ),
                        ),
                        EmptyLine(
                            indent=True,
                            whitespace=SimpleWhitespace(
                                value='',
                            ),
                            comment=None,
                            newline=Newline(
                                value=None,
                            ),
                        ),
                    ],
                    trailing_whitespace=TrailingWhitespace(
                        whitespace=SimpleWhitespace(
                            value=' ',
                        ),
                        comment=Comment(
                            value='# comment',
                        ),
                        newline=Newline(
                            value=None,
                        ),
                    ),
                )
                """
            ).strip(),
        )

    def test_visit(self) -> None:
        tree = cst.Module((cst.SimpleStatementLine((cst.Pass(),)),))
        tree.visit(_TestVisitor(self))
