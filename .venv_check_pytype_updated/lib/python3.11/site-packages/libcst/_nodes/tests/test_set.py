# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Any, Callable

import libcst as cst
from libcst import parse_expression
from libcst._nodes.tests.base import CSTNodeTest, parse_expression_as
from libcst._parser.entrypoints import is_native
from libcst.testing.utils import data_provider


class ListTest(CSTNodeTest):
    # A lot of Element/StarredElement tests are provided by the tests for Tuple, so we
    # we don't need to duplicate them here.
    @data_provider(
        [
            # one-element list, sentinel comma value
            {
                "node": cst.Set([cst.Element(cst.Name("single_element"))]),
                "code": "{single_element}",
                "parser": parse_expression,
            },
            # custom whitespace between brackets
            {
                "node": cst.Set(
                    [cst.Element(cst.Name("single_element"))],
                    lbrace=cst.LeftCurlyBrace(
                        whitespace_after=cst.SimpleWhitespace("\t")
                    ),
                    rbrace=cst.RightCurlyBrace(
                        whitespace_before=cst.SimpleWhitespace("    ")
                    ),
                ),
                "code": "{\tsingle_element    }",
                "parser": parse_expression,
            },
            # two-element list, sentinel comma value
            {
                "node": cst.Set(
                    [cst.Element(cst.Name("one")), cst.Element(cst.Name("two"))]
                ),
                "code": "{one, two}",
                "parser": None,
            },
            # with parenthesis
            {
                "node": cst.Set(
                    [cst.Element(cst.Name("one"))],
                    lpar=[cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "code": "({one})",
                "parser": None,
            },
            # starred element
            {
                "node": cst.Set(
                    [
                        cst.StarredElement(cst.Name("one")),
                        cst.StarredElement(cst.Name("two")),
                    ]
                ),
                "code": "{*one, *two}",
                "parser": None,
            },
            # missing spaces around set, always okay
            {
                "node": cst.GeneratorExp(
                    cst.Name("elt"),
                    cst.CompFor(
                        target=cst.Name("elt"),
                        iter=cst.Set(
                            [
                                cst.Element(
                                    cst.Name("one"),
                                    cst.Comma(
                                        whitespace_after=cst.SimpleWhitespace(" ")
                                    ),
                                ),
                                cst.Element(cst.Name("two")),
                            ]
                        ),
                        ifs=[
                            cst.CompIf(
                                cst.Name("test"),
                                whitespace_before=cst.SimpleWhitespace(""),
                            )
                        ],
                        whitespace_after_in=cst.SimpleWhitespace(""),
                    ),
                ),
                "code": "(elt for elt in{one, two}if test)",
                "parser": parse_expression,
            },
        ]
    )
    def test_valid(self, **kwargs: Any) -> None:
        self.validate_node(**kwargs)

    @data_provider(
        (
            (
                lambda: cst.Set(
                    [cst.Element(cst.Name("mismatched"))],
                    lpar=[cst.LeftParen(), cst.LeftParen()],
                    rpar=[cst.RightParen()],
                ),
                "unbalanced parens",
            ),
            (lambda: cst.Set([]), "at least one element"),
        )
    )
    def test_invalid(
        self, get_node: Callable[[], cst.CSTNode], expected_re: str
    ) -> None:
        self.assert_invalid(get_node, expected_re)

    @data_provider(
        (
            {
                "code": "{*x, 2}",
                "parser": parse_expression_as(python_version="3.5"),
                "expect_success": True,
            },
            {
                "code": "{*x, 2}",
                "parser": parse_expression_as(python_version="3.3"),
                "expect_success": False,
            },
        )
    )
    def test_versions(self, **kwargs: Any) -> None:
        if is_native() and not kwargs.get("expect_success", True):
            self.skipTest("parse errors are disabled for native parser")
        self.assert_parses(**kwargs)
