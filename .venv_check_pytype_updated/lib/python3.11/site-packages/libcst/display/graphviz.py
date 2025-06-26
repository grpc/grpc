# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import annotations

import textwrap
from collections.abc import Sequence

from libcst import CSTNode
from libcst.helpers import filter_node_fields


_syntax_style = ', color="#777777", fillcolor="#eeeeee"'
_value_style = ', color="#3e99ed", fillcolor="#b8d9f8"'

node_style: dict[str, str] = {
    "__default__": "",
    "EmptyLine": _syntax_style,
    "IndentedBlock": _syntax_style,
    "SimpleStatementLine": _syntax_style,
    "SimpleWhitespace": _syntax_style,
    "TrailingWhitespace": _syntax_style,
    "Newline": _syntax_style,
    "Comma": _syntax_style,
    "LeftParen": _syntax_style,
    "RightParen": _syntax_style,
    "LeftSquareBracket": _syntax_style,
    "RightSquareBracket": _syntax_style,
    "LeftCurlyBrace": _syntax_style,
    "RightCurlyBrace": _syntax_style,
    "BaseSmallStatement": _syntax_style,
    "BaseCompoundStatement": _syntax_style,
    "SimpleStatementSuite": _syntax_style,
    "Colon": _syntax_style,
    "Dot": _syntax_style,
    "Semicolon": _syntax_style,
    "ParenthesizedWhitespace": _syntax_style,
    "BaseParenthesizableWhitespace": _syntax_style,
    "Comment": _syntax_style,
    "Name": _value_style,
    "Integer": _value_style,
    "Float": _value_style,
    "Imaginary": _value_style,
    "SimpleString": _value_style,
    "FormattedStringText": _value_style,
}
"""Graphviz style for specific CST nodes"""


def _create_node_graphviz(node: CSTNode) -> str:
    """Creates the graphviz representation of a CST node."""
    node_name = node.__class__.__qualname__

    if node_name in node_style:
        style = node_style[node_name]
    else:
        style = node_style["__default__"]

    # pyre-ignore[16]: the existence of node.value is checked before usage
    if hasattr(node, "value") and isinstance(node.value, str):
        line_break = r"\n"
        quote = '"'
        escaped_quote = r"\""
        value = f"{line_break}<{node.value.replace(quote, escaped_quote)}>"
        style = style + ', shape="box"'
    else:
        value = ""

    return f'{id(node)} [label="{node_name}{value}"{style}]'


def _node_repr_recursive(
    node: object,
    *,
    show_defaults: bool,
    show_syntax: bool,
    show_whitespace: bool,
) -> list[str]:
    """Creates the graphviz representation of a CST node,
    and of its child nodes."""
    if not isinstance(node, CSTNode):
        return []

    fields = filter_node_fields(
        node,
        show_defaults=show_defaults,
        show_syntax=show_syntax,
        show_whitespace=show_whitespace,
    )

    graphviz_lines: list[str] = [_create_node_graphviz(node)]

    for field in fields:
        value = getattr(node, field.name)
        if isinstance(value, CSTNode):
            # Display a single node
            graphviz_lines.append(f'{id(node)} -> {id(value)} [label="{field.name}"]')
            graphviz_lines.extend(
                _node_repr_recursive(
                    value,
                    show_defaults=show_defaults,
                    show_syntax=show_syntax,
                    show_whitespace=show_whitespace,
                )
            )
            continue

        if isinstance(value, Sequence):
            # Display a sequence of nodes
            for index, child in enumerate(value):
                if isinstance(child, CSTNode):
                    graphviz_lines.append(
                        rf'{id(node)} -> {id(child)} [label="{field.name}[{index}]"]'
                    )
                    graphviz_lines.extend(
                        _node_repr_recursive(
                            child,
                            show_defaults=show_defaults,
                            show_syntax=show_syntax,
                            show_whitespace=show_whitespace,
                        )
                    )

    return graphviz_lines


def dump_graphviz(
    node: object,
    *,
    show_defaults: bool = False,
    show_syntax: bool = False,
    show_whitespace: bool = False,
) -> str:
    """
    Returns a string representation (in graphviz .dot style) of a CST node,
    and its child nodes.

    Setting ``show_defaults`` to ``True`` will add fields regardless if their
    value is different from the default value.

    Setting ``show_whitespace`` will add whitespace fields and setting
    ``show_syntax`` will add syntax fields while respecting the value of
    ``show_defaults``.
    """

    graphviz_settings = textwrap.dedent(
        r"""
        layout=dot;
        rankdir=TB;
        splines=line;
        ranksep=0.5;
        nodesep=1.0;
        dpi=300;
        bgcolor=transparent;
        node [
            style=filled,
            color="#fb8d3f",
            fontcolor="#4b4f54",
            fillcolor="#fdd2b3",
            fontname="Source Code Pro Semibold",
            penwidth="2",
            group=main,
        ];
        edge [
            color="#999999",
            fontcolor="#4b4f54",
            fontname="Source Code Pro Semibold",
            fontsize=12,
            penwidth=2,
        ];
        """[
            1:
        ]
    )

    return "\n".join(
        ["digraph {", graphviz_settings]
        + _node_repr_recursive(
            node,
            show_defaults=show_defaults,
            show_syntax=show_syntax,
            show_whitespace=show_whitespace,
        )
        + ["}"]
    )
