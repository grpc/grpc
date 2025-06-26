# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from textwrap import dedent
from typing import Callable
from unittest.mock import patch

import libcst as cst
from libcst._nodes.base import CSTValidationError
from libcst._parser.entrypoints import is_native
from libcst.testing.utils import data_provider, UnitTest


class ParseErrorsTest(UnitTest):
    @data_provider(
        {
            # _wrapped_tokenize raises these exceptions
            "wrapped_tokenize__invalid_token": (
                lambda: cst.parse_module("'"),
                dedent(
                    """
                    Syntax Error @ 1:1.
                    "'" is not a valid token.

                    '
                    ^
                    """
                ).strip(),
            ),
            "wrapped_tokenize__expected_dedent": (
                lambda: cst.parse_module("if False:\n    pass\n  pass"),
                dedent(
                    """
                    Syntax Error @ 3:1.
                    Inconsistent indentation. Expected a dedent.

                      pass
                    ^
                    """
                ).strip(),
            ),
            "wrapped_tokenize__mismatched_braces": (
                lambda: cst.parse_module("abcd)"),
                dedent(
                    """
                    Syntax Error @ 1:5.
                    Encountered a closing brace without a matching opening brace.

                    abcd)
                        ^
                    """
                ).strip(),
            ),
            # _base_parser raises these exceptions
            "base_parser__unexpected_indent": (
                lambda: cst.parse_module("    abcd"),
                dedent(
                    """
                    Syntax Error @ 1:5.
                    Incomplete input. Unexpectedly encountered an indent.

                        abcd
                        ^
                    """
                ).strip(),
            ),
            "base_parser__unexpected_dedent": (
                lambda: cst.parse_module("if False:\n    (el for el\n"),
                dedent(
                    """
                    Syntax Error @ 3:1.
                    Incomplete input. Encountered a dedent, but expected 'in'.

                        (el for el
                                  ^
                    """
                ).strip(),
            ),
            "base_parser__multiple_possibilities": (
                lambda: cst.parse_module("try: pass"),
                dedent(
                    """
                    Syntax Error @ 2:1.
                    Incomplete input. Encountered end of file (EOF), but expected 'except', or 'finally'.

                    try: pass
                             ^
                    """
                ).strip(),
            ),
            # conversion functions raise these exceptions.
            # `_base_parser` is responsible for attaching location information.
            "convert_nonterminal__dict_unpacking": (
                lambda: cst.parse_expression("{**el for el in []}"),
                dedent(
                    """
                    Syntax Error @ 1:19.
                    dict unpacking cannot be used in dict comprehension

                    {**el for el in []}
                                      ^
                    """
                ).strip(),
            ),
            "convert_nonterminal__arglist_non_default_after_default": (
                lambda: cst.parse_statement("def fn(first=None, second): ..."),
                dedent(
                    """
                    Syntax Error @ 1:26.
                    Cannot have a non-default argument following a default argument.

                    def fn(first=None, second): ...
                                             ^
                    """
                ).strip(),
            ),
            "convert_nonterminal__arglist_trailing_param_star_without_comma": (
                lambda: cst.parse_statement("def fn(abc, *): ..."),
                dedent(
                    """
                    Syntax Error @ 1:14.
                    Named (keyword) arguments must follow a bare *.

                    def fn(abc, *): ...
                                 ^
                    """
                ).strip(),
            ),
            "convert_nonterminal__arglist_trailing_param_star_with_comma": (
                lambda: cst.parse_statement("def fn(abc, *,): ..."),
                dedent(
                    """
                    Syntax Error @ 1:15.
                    Named (keyword) arguments must follow a bare *.

                    def fn(abc, *,): ...
                                  ^
                    """
                ).strip(),
            ),
            "convert_nonterminal__class_arg_positional_after_keyword": (
                lambda: cst.parse_statement("class Cls(first=None, second): ..."),
                dedent(
                    """
                    Syntax Error @ 2:1.
                    Positional argument follows keyword argument.

                    class Cls(first=None, second): ...
                                                      ^
                    """
                ).strip(),
            ),
            "convert_nonterminal__class_arg_positional_expansion_after_keyword": (
                lambda: cst.parse_statement("class Cls(first=None, *second): ..."),
                dedent(
                    """
                    Syntax Error @ 2:1.
                    Positional argument follows keyword argument.

                    class Cls(first=None, *second): ...
                                                       ^
                    """
                ).strip(),
            ),
        }
    )
    def test_parser_syntax_error_str(
        self, parse_fn: Callable[[], object], expected: str
    ) -> None:
        with self.assertRaises(cst.ParserSyntaxError) as cm:
            parse_fn()
        # make sure str() doesn't blow up
        self.assertIn("Syntax Error", str(cm.exception))
        if not is_native():
            self.assertEqual(str(cm.exception), expected)

    def test_native_fallible_into_py(self) -> None:
        with patch("libcst._nodes.expression.Name._validate") as await_validate:
            await_validate.side_effect = CSTValidationError("validate is broken")
            with self.assertRaises((SyntaxError, cst.ParserSyntaxError)):
                cst.parse_module("foo")
