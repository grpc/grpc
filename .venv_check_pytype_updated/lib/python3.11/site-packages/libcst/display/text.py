# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import annotations

import dataclasses
from typing import List, Sequence

from libcst import CSTLogicError, CSTNode
from libcst.helpers import filter_node_fields

_DEFAULT_INDENT: str = "  "


def _node_repr_recursive(  # noqa: C901
    node: object,
    *,
    indent: str = _DEFAULT_INDENT,
    show_defaults: bool = False,
    show_syntax: bool = False,
    show_whitespace: bool = False,
) -> List[str]:
    if isinstance(node, CSTNode):
        # This is a CSTNode, we must pretty-print it.
        fields: Sequence[dataclasses.Field[CSTNode]] = filter_node_fields(
            node=node,
            show_defaults=show_defaults,
            show_syntax=show_syntax,
            show_whitespace=show_whitespace,
        )

        tokens: List[str] = [node.__class__.__name__]

        if len(fields) == 0:
            tokens.append("()")
        else:
            tokens.append("(\n")

            for field in fields:
                child_tokens: List[str] = [field.name, "="]
                value = getattr(node, field.name)

                if isinstance(value, (str, bytes)) or not isinstance(value, Sequence):
                    # Render out the node contents
                    child_tokens.extend(
                        _node_repr_recursive(
                            value,
                            indent=indent,
                            show_whitespace=show_whitespace,
                            show_defaults=show_defaults,
                            show_syntax=show_syntax,
                        )
                    )
                elif isinstance(value, Sequence):
                    # Render out a list of individual nodes
                    if len(value) > 0:
                        child_tokens.append("[\n")
                        list_tokens: List[str] = []

                        last_value = len(value) - 1
                        for j, v in enumerate(value):
                            list_tokens.extend(
                                _node_repr_recursive(
                                    v,
                                    indent=indent,
                                    show_whitespace=show_whitespace,
                                    show_defaults=show_defaults,
                                    show_syntax=show_syntax,
                                )
                            )
                            if j != last_value:
                                list_tokens.append(",\n")
                            else:
                                list_tokens.append(",")

                        split_by_line = "".join(list_tokens).split("\n")
                        child_tokens.append(
                            "\n".join(f"{indent}{t}" for t in split_by_line)
                        )

                        child_tokens.append("\n]")
                    else:
                        child_tokens.append("[]")
                else:
                    raise CSTLogicError("Logic error!")

                # Handle indentation and trailing comma.
                split_by_line = "".join(child_tokens).split("\n")
                tokens.append("\n".join(f"{indent}{t}" for t in split_by_line))
                tokens.append(",\n")

            tokens.append(")")

        return tokens
    else:
        # This is a python value, just return the repr
        return [repr(node)]


def dump(
    node: CSTNode,
    *,
    indent: str = _DEFAULT_INDENT,
    show_defaults: bool = False,
    show_syntax: bool = False,
    show_whitespace: bool = False,
) -> str:
    """
    Returns a string representation of the node that contains minimal differences
    from the default contruction of the node while also hiding whitespace and
    syntax fields.

    Setting ``show_defaults`` to ``True`` will add fields regardless if their
    value is different from the default value.

    Setting ``show_whitespace`` will add whitespace fields and setting
    ``show_syntax`` will add syntax fields while respecting the value of
    ``show_defaults``.

    When all keyword args are set to true, the output of this function is
    indentical to the __repr__ method of the node.
    """
    return "".join(
        _node_repr_recursive(
            node,
            indent=indent,
            show_defaults=show_defaults,
            show_syntax=show_syntax,
            show_whitespace=show_whitespace,
        )
    )
