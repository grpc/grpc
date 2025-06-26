# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

import libcst as cst
import libcst.matchers as m
from libcst.codemod import VisitorBasedCodemodCommand
from libcst.metadata import QualifiedName, QualifiedNameProvider, QualifiedNameSource


class FixVariadicCallableCommmand(VisitorBasedCodemodCommand):
    DESCRIPTION: str = (
        "Fix incorrect variadic callable type annotations from `Callable[[...], T]` to `Callable[..., T]``"
    )

    METADATA_DEPENDENCIES = (QualifiedNameProvider,)

    def leave_Subscript(
        self, original_node: cst.Subscript, updated_node: cst.Subscript
    ) -> cst.BaseExpression:
        if QualifiedNameProvider.has_name(
            self,
            original_node,
            QualifiedName(name="typing.Callable", source=QualifiedNameSource.IMPORT),
        ):
            node_matches = len(updated_node.slice) == 2 and m.matches(
                updated_node.slice[0],
                m.SubscriptElement(
                    slice=m.Index(value=m.List(elements=[m.Element(m.Ellipsis())]))
                ),
            )

            if node_matches:
                slices = list(updated_node.slice)
                slices[0] = cst.SubscriptElement(cst.Index(cst.Ellipsis()))
                return updated_node.with_changes(slice=slices)
        return updated_node
