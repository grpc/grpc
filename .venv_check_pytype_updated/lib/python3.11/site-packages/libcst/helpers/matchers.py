# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

from dataclasses import fields, is_dataclass, MISSING

from libcst import matchers
from libcst._nodes.base import CSTNode


def node_to_matcher(
    node: CSTNode, *, match_syntactic_trivia: bool = False
) -> matchers.BaseMatcherNode:
    """Convert a concrete node to a matcher."""
    if not is_dataclass(node):
        raise ValueError(f"{node} is not a CSTNode")

    attrs = {}
    for field in fields(node):
        name = field.name
        child = getattr(node, name)
        if not match_syntactic_trivia and field.name.startswith("whitespace"):
            # Not all nodes have whitespace fields, some have multiple, but they all
            # start with whitespace*
            child = matchers.DoNotCare()
        elif field.default is not MISSING and child == field.default:
            child = matchers.DoNotCare()
        # pyre-ignore[29]: Union[MISSING_TYPE, ...] is not a function.
        elif field.default_factory is not MISSING and child == field.default_factory():
            child = matchers.DoNotCare()
        elif isinstance(child, (list, tuple)):
            child = type(child)(
                node_to_matcher(item, match_syntactic_trivia=match_syntactic_trivia)
                for item in child
            )
        elif hasattr(matchers, type(child).__name__):
            child = node_to_matcher(
                child, match_syntactic_trivia=match_syntactic_trivia
            )
        attrs[name] = child

    matcher = getattr(matchers, type(node).__name__)
    return matcher(**attrs)
