# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from libcst import parse_expression, parse_statement
from libcst.helpers.matchers import node_to_matcher
from libcst.matchers import matches
from libcst.testing.utils import data_provider, UnitTest


class MatchersTest(UnitTest):
    @data_provider(
        (
            ('"some string"',),
            ("call(some, **kwargs)",),
            ("a[b.c]",),
            ("[1 for _ in range(99) if False]",),
        )
    )
    def test_reflexive_expressions(self, code: str) -> None:
        node = parse_expression(code)
        matcher = node_to_matcher(node)
        self.assertTrue(matches(node, matcher))

    @data_provider(
        (
            ("def foo(a) -> None: pass",),
            ("class F: ...",),
            ("foo: bar",),
        )
    )
    def test_reflexive_statements(self, code: str) -> None:
        node = parse_statement(code)
        matcher = node_to_matcher(node)
        self.assertTrue(matches(node, matcher))

    def test_whitespace(self) -> None:
        code_ws = parse_expression("(foo  ,   bar  )")
        code = parse_expression("(foo,bar)")
        self.assertTrue(
            matches(
                code,
                node_to_matcher(code_ws),
            )
        )
        self.assertFalse(
            matches(
                code,
                node_to_matcher(code_ws, match_syntactic_trivia=True),
            )
        )
