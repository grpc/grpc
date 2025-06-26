# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import inspect
from collections import defaultdict
from collections.abc import Sequence as ABCSequence
from dataclasses import dataclass, fields, replace
from typing import Dict, Iterator, List, Mapping, Sequence, Set, Type, Union

import libcst as cst


def _get_bases() -> Iterator[Type[cst.CSTNode]]:
    """
    Get all base classes that are subclasses of CSTNode but not an actual
    node itself. This allows us to keep our types sane by refering to the
    base classes themselves.
    """

    for name in dir(cst):
        if not name.startswith("Base"):
            continue

        yield getattr(cst, name)


typeclasses: Sequence[Type[cst.CSTNode]] = sorted(
    _get_bases(), key=lambda base: base.__name__
)


def _get_nodes() -> Iterator[Type[cst.CSTNode]]:
    """
    Grab all CSTNodes that are not a superclass. Basically, anything that a
    person might use to generate a tree.
    """

    for name in dir(cst):
        if name.startswith("__") and name.endswith("__"):
            continue
        if name == "CSTNode":
            continue

        node = getattr(cst, name)
        try:
            if issubclass(node, cst.CSTNode):
                yield node
        except TypeError:
            # This isn't a class, so we don't care about it.
            pass


all_libcst_nodes: Sequence[Type[cst.CSTNode]] = sorted(
    _get_nodes(), key=lambda node: node.__name__
)
node_to_bases: Dict[Type[cst.CSTNode], List[Type[cst.CSTNode]]] = {}
for node in all_libcst_nodes:
    # Map the base classes for this node
    node_to_bases[node] = list(
        reversed([b for b in inspect.getmro(node) if issubclass(b, cst.CSTNode)])
    )


def _get_most_generic_base_for_node(node: Type[cst.CSTNode]) -> Type[cst.CSTNode]:
    # Ignore non-exported bases, a user couldn't specify these types
    # in type hints.
    exportable_bases = [b for b in node_to_bases[node] if b in node_to_bases]
    return exportable_bases[0]


nodebases: Dict[Type[cst.CSTNode], Type[cst.CSTNode]] = {}
for node in all_libcst_nodes:
    # Find the most generic version of this node that isn't CSTNode.
    nodebases[node] = _get_most_generic_base_for_node(node)


@dataclass(frozen=True)
class Usage:
    maybe: bool = False
    optional: bool = False
    sequence: bool = False


nodeuses: Dict[Type[cst.CSTNode], Usage] = {node: Usage() for node in all_libcst_nodes}


def _is_maybe(typeobj: object) -> bool:
    try:
        # pyre-ignore We wrap this in a TypeError check so this is safe
        return issubclass(typeobj, cst.MaybeSentinel)
    except TypeError:
        return False


def _get_origin(typeobj: object) -> object:
    try:
        # pyre-ignore We wrap this in a AttributeError check so this is safe
        return typeobj.__origin__
    except AttributeError:
        # Don't care, not a union or sequence
        return None


def _get_args(typeobj: object) -> List[object]:
    try:
        # pyre-ignore We wrap this in a AttributeError check so this is safe
        return typeobj.__args__
    except AttributeError:
        # Don't care, not a union or sequence
        return []


def _is_sequence(typeobj: object) -> bool:
    origin = _get_origin(typeobj)
    return origin is Sequence or origin is ABCSequence


def _is_union(typeobj: object) -> bool:
    return _get_origin(typeobj) is Union


def _calc_node_usage(typeobj: object) -> None:
    if _is_union(typeobj):
        has_maybe = any(_is_maybe(n) for n in _get_args(typeobj))
        has_none = any(isinstance(n, type(None)) for n in _get_args(typeobj))

        for node in _get_args(typeobj):
            if node in all_libcst_nodes:
                nodeuses[node] = replace(
                    nodeuses[node],
                    maybe=nodeuses[node].maybe or has_maybe,
                    optional=nodeuses[node].optional or has_none,
                )
            else:
                _calc_node_usage(node)

    if _is_sequence(typeobj):
        for node in _get_args(typeobj):
            if node in all_libcst_nodes:
                nodeuses[node] = replace(nodeuses[node], sequence=True)
            else:
                _calc_node_usage(node)


for node in all_libcst_nodes:
    for field in fields(node) or []:
        if field.name == "_metadata":
            continue

        _calc_node_usage(field.type)


imports: Mapping[str, Set[str]] = defaultdict(set)
for node, base in nodebases.items():
    if node.__name__.startswith("Base"):
        continue
    for x in (node, base):
        imports[x.__module__].add(x.__name__)
