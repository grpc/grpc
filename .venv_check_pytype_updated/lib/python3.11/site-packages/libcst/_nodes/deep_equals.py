# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Provides the implementation of `CSTNode.deep_equals`.
"""

from dataclasses import fields
from typing import Sequence

from libcst._nodes.base import CSTNode


def deep_equals(a: object, b: object) -> bool:
    if isinstance(a, CSTNode) and isinstance(b, CSTNode):
        return _deep_equals_cst_node(a, b)
    elif (
        isinstance(a, Sequence)
        and not isinstance(a, (str, bytes))
        and isinstance(b, Sequence)
        and not isinstance(b, (str, bytes))
    ):
        return _deep_equals_sequence(a, b)
    else:
        return a == b


def _deep_equals_sequence(a: Sequence[object], b: Sequence[object]) -> bool:
    """
    A helper function for `CSTNode.deep_equals`.

    Normalizes and compares sequences. Because we only ever expose `Sequence[]`
    types, and not `List[]`, `Tuple[]`, or `Iterable[]` values, all sequences should
    be treated as equal if they have the same values.
    """
    if a is b:  # short-circuit
        return True
    if len(a) != len(b):
        return False
    return all(deep_equals(a_el, b_el) for (a_el, b_el) in zip(a, b))


def _deep_equals_cst_node(a: "CSTNode", b: "CSTNode") -> bool:
    if type(a) is not type(b):
        return False
    if a is b:  # short-circuit
        return True
    # Ignore metadata and other hidden fields
    for field in (f for f in fields(a) if f.compare is True):
        a_value = getattr(a, field.name)
        b_value = getattr(b, field.name)
        if not deep_equals(a_value, b_value):
            return False
    return True
