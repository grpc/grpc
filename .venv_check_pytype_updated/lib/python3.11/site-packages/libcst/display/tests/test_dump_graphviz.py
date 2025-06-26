# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import annotations

from textwrap import dedent
from typing import TYPE_CHECKING

from libcst import parse_module
from libcst.display import dump_graphviz
from libcst.testing.utils import UnitTest

if TYPE_CHECKING:
    from libcst import Module


class CSTDumpGraphvizTest(UnitTest):
    """Check dump_graphviz contains CST nodes."""

    source_code: str = dedent(
        r"""
        def foo(a: str) -> None:
            pass ;
            pass
            return
        """[
            1:
        ]
    )
    cst: Module

    @classmethod
    def setUpClass(cls) -> None:
        cls.cst = parse_module(cls.source_code)

    def _assert_node(self, node_name: str, graphviz_str: str) -> None:
        self.assertIn(
            node_name, graphviz_str, f"No node {node_name} found in graphviz_dump"
        )

    def _check_essential_nodes_in_tree(self, graphviz_str: str) -> None:
        # Check CST nodes are present in graphviz string
        self._assert_node("Module", graphviz_str)
        self._assert_node("FunctionDef", graphviz_str)
        self._assert_node("Name", graphviz_str)
        self._assert_node("Parameters", graphviz_str)
        self._assert_node("Param", graphviz_str)
        self._assert_node("Annotation", graphviz_str)
        self._assert_node("IndentedBlock", graphviz_str)
        self._assert_node("SimpleStatementLine", graphviz_str)
        self._assert_node("Pass", graphviz_str)
        self._assert_node("Return", graphviz_str)

        # Check CST values are present in graphviz string
        self._assert_node("<foo>", graphviz_str)
        self._assert_node("<a>", graphviz_str)
        self._assert_node("<str>", graphviz_str)
        self._assert_node("<None>", graphviz_str)

    def test_essential_tree(self) -> None:
        """Check essential nodes are present in the CST graphviz dump."""
        graphviz_str = dump_graphviz(self.cst)
        self._check_essential_nodes_in_tree(graphviz_str)

    def test_full_tree(self) -> None:
        """Check all nodes are present in the CST graphviz dump."""
        graphviz_str = dump_graphviz(
            self.cst,
            show_whitespace=True,
            show_defaults=True,
            show_syntax=True,
        )
        self._check_essential_nodes_in_tree(graphviz_str)

        self._assert_node("Semicolon", graphviz_str)
        self._assert_node("SimpleWhitespace", graphviz_str)
        self._assert_node("Newline", graphviz_str)
        self._assert_node("TrailingWhitespace", graphviz_str)

        self._assert_node("<>", graphviz_str)
        self._assert_node("< >", graphviz_str)
