# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import annotations

import dataclasses
from typing import TYPE_CHECKING

from libcst import IndentedBlock, Module
from libcst._nodes.deep_equals import deep_equals

if TYPE_CHECKING:
    from typing import Sequence

    from libcst import CSTNode


def get_node_fields(node: CSTNode) -> Sequence[dataclasses.Field[CSTNode]]:
    """
    Returns the sequence of a given CST-node's fields.
    """
    return dataclasses.fields(node)


def is_whitespace_node_field(node: CSTNode, field: dataclasses.Field[CSTNode]) -> bool:
    """
    Returns True if a given CST-node's field is a whitespace-related field
    (whitespace, indent, header, footer, etc.).
    """
    if "whitespace" in field.name:
        return True
    if "leading_lines" in field.name:
        return True
    if "lines_after_decorators" in field.name:
        return True
    if isinstance(node, (IndentedBlock, Module)) and field.name in [
        "header",
        "footer",
    ]:
        return True
    if isinstance(node, IndentedBlock) and field.name == "indent":
        return True
    return False


def is_syntax_node_field(node: CSTNode, field: dataclasses.Field[CSTNode]) -> bool:
    """
    Returns True if a given CST-node's field is a syntax-related field
    (colon, semicolon, dot, encoding, etc.).
    """
    if isinstance(node, Module) and field.name in [
        "encoding",
        "default_indent",
        "default_newline",
        "has_trailing_newline",
    ]:
        return True
    type_str = repr(field.type)
    if (
        "Sentinel" in type_str
        and field.name not in ["star_arg", "star", "posonly_ind"]
        and "whitespace" not in field.name
    ):
        # This is a value that can optionally be specified, so its
        # definitely syntax.
        return True

    for name in ["Semicolon", "Colon", "Comma", "Dot", "AssignEqual"]:
        # These are all nodes that exist for separation syntax
        if name in type_str:
            return True

    return False


def get_field_default_value(field: dataclasses.Field[CSTNode]) -> object:
    """
    Returns the default value of a CST-node's field.
    """
    if field.default_factory is not dataclasses.MISSING:
        # pyre-fixme[29]: `Union[dataclasses._MISSING_TYPE,
        #  dataclasses._DefaultFactory[object]]` is not a function.
        return field.default_factory()
    return field.default


def is_default_node_field(node: CSTNode, field: dataclasses.Field[CSTNode]) -> bool:
    """
    Returns True if a given CST-node's field has its default value.
    """
    return deep_equals(getattr(node, field.name), get_field_default_value(field))


def filter_node_fields(
    node: CSTNode,
    *,
    show_defaults: bool,
    show_syntax: bool,
    show_whitespace: bool,
) -> Sequence[dataclasses.Field[CSTNode]]:
    """
    Returns a filtered sequence of a CST-node's fields.

    Setting ``show_whitespace`` to ``False`` will filter whitespace fields.

    Setting ``show_defaults`` to ``False`` will filter fields if their value is equal to
    the default value ;  while respecting  the value of ``show_whitespace``.

    Setting ``show_syntax``  to ``False`` will filter syntax fields ; while respecting
    the value of ``show_whitespace`` & ``show_defaults``.
    """

    fields: Sequence[dataclasses.Field[CSTNode]] = dataclasses.fields(node)
    # Hide all fields prefixed with "_"
    fields = [f for f in fields if f.name[0] != "_"]
    # Filter whitespace nodes if needed
    if not show_whitespace:
        fields = [f for f in fields if not is_whitespace_node_field(node, f)]
    # Filter values which aren't changed from their defaults
    if not show_defaults:
        fields = [f for f in fields if not is_default_node_field(node, f)]
    # Filter out values which aren't interesting if needed
    if not show_syntax:
        fields = [f for f in fields if not is_syntax_node_field(node, f)]

    return fields
