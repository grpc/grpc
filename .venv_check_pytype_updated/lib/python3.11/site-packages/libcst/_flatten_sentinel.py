# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import sys

# PEP 585
if sys.version_info < (3, 9):
    from typing import Iterable, Sequence
else:
    from collections.abc import Iterable, Sequence

from libcst._types import CSTNodeT_co


class FlattenSentinel(Sequence[CSTNodeT_co]):
    """
    A :class:`FlattenSentinel` may be returned by a :meth:`CSTTransformer.on_leave`
    method when one wants to replace a node with multiple nodes. The replaced
    node must be contained in a `Sequence` attribute such as
    :attr:`~libcst.Module.body`.  This is generally the case for
    :class:`~libcst.BaseStatement` and :class:`~libcst.BaseSmallStatement`.
    For example to insert a print before every return::

        def leave_Return(
            self, original_node: cst.Return, updated_node: cst.Return
        ) -> Union[cst.Return, cst.RemovalSentinel, cst.FlattenSentinel[cst.BaseSmallStatement]]:
            log_stmt = cst.Expr(cst.parse_expression("print('returning')"))
            return cst.FlattenSentinel([log_stmt, updated_node])

    Returning an empty :class:`FlattenSentinel` is equivalent to returning
    :attr:`cst.RemovalSentinel.REMOVE` and is subject to its requirements.
    """

    nodes: Sequence[CSTNodeT_co]

    def __init__(self, nodes: Iterable[CSTNodeT_co]) -> None:
        self.nodes = tuple(nodes)

    def __getitem__(self, idx: int) -> CSTNodeT_co:
        return self.nodes[idx]

    def __len__(self) -> int:
        return len(self.nodes)
