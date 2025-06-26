# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any

import libcst as cst
from libcst import parse_statement
from libcst._nodes.tests.base import CSTNodeTest
from libcst.metadata import CodeRange
from libcst.testing.utils import data_provider


class AssignTest(CSTNodeTest):
    @data_provider(
        (
            # Simple assignment creation case.
            {
                "node": cst.Assign(
                    (cst.AssignTarget(cst.Name("foo")),), cst.Integer("5")
                ),
                "code": "foo = 5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 7)),
            },
            # Multiple targets creation
            {
                "node": cst.Assign(
                    (
                        cst.AssignTarget(cst.Name("foo")),
                        cst.AssignTarget(cst.Name("bar")),
                    ),
                    cst.Integer("5"),
                ),
                "code": "foo = bar = 5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 13)),
            },
            # Whitespace test for creating nodes
            {
                "node": cst.Assign(
                    (
                        cst.AssignTarget(
                            cst.Name("foo"),
                            whitespace_before_equal=cst.SimpleWhitespace(""),
                            whitespace_after_equal=cst.SimpleWhitespace(""),
                        ),
                    ),
                    cst.Integer("5"),
                ),
                "code": "foo=5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 5)),
            },
            # Simple assignment parser case.
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Assign(
                            (cst.AssignTarget(cst.Name("foo")),), cst.Integer("5")
                        ),
                    )
                ),
                "code": "foo = 5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Multiple targets parser
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Assign(
                            (
                                cst.AssignTarget(cst.Name("foo")),
                                cst.AssignTarget(cst.Name("bar")),
                            ),
                            cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo = bar = 5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Whitespace test parser
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.Assign(
                            (
                                cst.AssignTarget(
                                    cst.Name("foo"),
                                    whitespace_before_equal=cst.SimpleWhitespace(""),
                                    whitespace_after_equal=cst.SimpleWhitespace(""),
                                ),
                            ),
                            cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo=5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": (lambda: cst.Assign(targets=(), value=cst.Integer("5"))),
                "expected_re": "at least one AssignTarget",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.Assign(
                        # pyre-ignore: Incompatible parameter type [6]
                        targets=[
                            cst.BinaryOperation(
                                left=cst.Name("x"),
                                operator=cst.Add(),
                                right=cst.Integer("1"),
                            ),
                        ],
                        value=cst.Name("y"),
                    )
                ),
                "expected_re": "Expected an instance of .*statement.AssignTarget.*",
            },
        )
    )
    def test_invalid_types(self, **kwargs: Any) -> None:
        self.assert_invalid_types(**kwargs)


class AnnAssignTest(CSTNodeTest):
    @data_provider(
        (
            # Simple assignment creation case.
            {
                "node": cst.AnnAssign(
                    cst.Name("foo"), cst.Annotation(cst.Name("str")), cst.Integer("5")
                ),
                "code": "foo: str = 5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 12)),
            },
            # Annotation creation without assignment
            {
                "node": cst.AnnAssign(cst.Name("foo"), cst.Annotation(cst.Name("str"))),
                "code": "foo: str",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 8)),
            },
            # Complex annotation creation
            {
                "node": cst.AnnAssign(
                    cst.Name("foo"),
                    cst.Annotation(
                        cst.Subscript(
                            cst.Name("Optional"),
                            (cst.SubscriptElement(cst.Index(cst.Name("str"))),),
                        )
                    ),
                    cst.Integer("5"),
                ),
                "code": "foo: Optional[str] = 5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 22)),
            },
            # Simple assignment parser case.
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AnnAssign(
                            target=cst.Name("foo"),
                            annotation=cst.Annotation(
                                annotation=cst.Name("str"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo: str = 5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Annotation without assignment
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AnnAssign(
                            target=cst.Name("foo"),
                            annotation=cst.Annotation(
                                annotation=cst.Name("str"),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            value=None,
                        ),
                    )
                ),
                "code": "foo: str\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Complex annotation
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AnnAssign(
                            target=cst.Name("foo"),
                            annotation=cst.Annotation(
                                annotation=cst.Subscript(
                                    cst.Name("Optional"),
                                    (cst.SubscriptElement(cst.Index(cst.Name("str"))),),
                                ),
                                whitespace_before_indicator=cst.SimpleWhitespace(""),
                            ),
                            equal=cst.AssignEqual(),
                            value=cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo: Optional[str] = 5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Whitespace test
            {
                "node": cst.AnnAssign(
                    target=cst.Name("foo"),
                    annotation=cst.Annotation(
                        annotation=cst.Subscript(
                            cst.Name("Optional"),
                            (cst.SubscriptElement(cst.Index(cst.Name("str"))),),
                        ),
                        whitespace_before_indicator=cst.SimpleWhitespace(" "),
                        whitespace_after_indicator=cst.SimpleWhitespace("  "),
                    ),
                    equal=cst.AssignEqual(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("  "),
                    ),
                    value=cst.Integer("5"),
                ),
                "code": "foo :  Optional[str]  =  5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 26)),
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AnnAssign(
                            target=cst.Name("foo"),
                            annotation=cst.Annotation(
                                annotation=cst.Subscript(
                                    cst.Name("Optional"),
                                    (cst.SubscriptElement(cst.Index(cst.Name("str"))),),
                                ),
                                whitespace_before_indicator=cst.SimpleWhitespace(" "),
                                whitespace_after_indicator=cst.SimpleWhitespace("  "),
                            ),
                            equal=cst.AssignEqual(
                                whitespace_before=cst.SimpleWhitespace("  "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                            value=cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo :  Optional[str]  =  5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.AnnAssign(
                        target=cst.Name("foo"),
                        annotation=cst.Annotation(cst.Name("str")),
                        equal=cst.AssignEqual(),
                        value=None,
                    )
                ),
                "expected_re": "Must have a value when specifying an AssignEqual.",
            },
        )
    )
    def test_invalid(self, **kwargs: Any) -> None:
        self.assert_invalid(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.AnnAssign(
                        # pyre-ignore: Incompatible parameter type [6]
                        target=cst.BinaryOperation(
                            left=cst.Name("x"),
                            operator=cst.Add(),
                            right=cst.Integer("1"),
                        ),
                        annotation=cst.Annotation(cst.Name("int")),
                        equal=cst.AssignEqual(),
                        value=cst.Name("y"),
                    )
                ),
                "expected_re": (
                    "Expected an instance of .*BaseAssignTargetExpression.*"
                ),
            },
        )
    )
    def test_invalid_types(self, **kwargs: Any) -> None:
        self.assert_invalid_types(**kwargs)


class AugAssignTest(CSTNodeTest):
    @data_provider(
        (
            # Simple assignment constructor case.
            {
                "node": cst.AugAssign(
                    cst.Name("foo"), cst.AddAssign(), cst.Integer("5")
                ),
                "code": "foo += 5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 8)),
            },
            {
                "node": cst.AugAssign(
                    cst.Name("bar"), cst.MultiplyAssign(), cst.Name("foo")
                ),
                "code": "bar *= foo",
                "parser": None,
                "expected_position": None,
            },
            # Whitespace constructor test
            {
                "node": cst.AugAssign(
                    target=cst.Name("foo"),
                    operator=cst.LeftShiftAssign(
                        whitespace_before=cst.SimpleWhitespace("  "),
                        whitespace_after=cst.SimpleWhitespace("  "),
                    ),
                    value=cst.Integer("5"),
                ),
                "code": "foo  <<=  5",
                "parser": None,
                "expected_position": CodeRange((1, 0), (1, 11)),
            },
            # Simple assignment parser case.
            {
                "node": cst.SimpleStatementLine(
                    (cst.AugAssign(cst.Name("foo"), cst.AddAssign(), cst.Integer("5")),)
                ),
                "code": "foo += 5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AugAssign(
                            cst.Name("bar"), cst.MultiplyAssign(), cst.Name("foo")
                        ),
                    )
                ),
                "code": "bar *= foo\n",
                "parser": parse_statement,
                "expected_position": None,
            },
            # Whitespace parser test
            {
                "node": cst.SimpleStatementLine(
                    (
                        cst.AugAssign(
                            target=cst.Name("foo"),
                            operator=cst.LeftShiftAssign(
                                whitespace_before=cst.SimpleWhitespace("  "),
                                whitespace_after=cst.SimpleWhitespace("  "),
                            ),
                            value=cst.Integer("5"),
                        ),
                    )
                ),
                "code": "foo  <<=  5\n",
                "parser": parse_statement,
                "expected_position": None,
            },
        )
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            {
                "get_node": (
                    lambda: cst.AugAssign(
                        # pyre-ignore: Incompatible parameter type [6]
                        target=cst.BinaryOperation(
                            left=cst.Name("x"),
                            operator=cst.Add(),
                            right=cst.Integer("1"),
                        ),
                        operator=cst.AddAssign(),
                        value=cst.Name("y"),
                    )
                ),
                "expected_re": (
                    "Expected an instance of .*BaseAssignTargetExpression.*"
                ),
            },
        )
    )
    def test_invalid_types(self, **kwargs: Any) -> None:
        self.assert_invalid_types(**kwargs)
