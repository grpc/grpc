# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict

import libcst as cst
from libcst.codemod import VisitorBasedCodemodCommand
from libcst.codemod.visitors import RemoveImportsVisitor
from libcst.metadata import QualifiedName, QualifiedNameProvider, QualifiedNameSource


class ConvertUnionToOrCommand(VisitorBasedCodemodCommand):
    DESCRIPTION: str = "Convert `Union[A, B]` to `A | B` in Python 3.10+"

    METADATA_DEPENDENCIES = (QualifiedNameProvider,)

    def leave_Subscript(
        self, original_node: cst.Subscript, updated_node: cst.Subscript
    ) -> cst.BaseExpression:
        """
        Given a subscript, check if it's a Union - if so, either flatten the members
        into a nested BitOr (if multiple members) or unwrap the type (if only one member).
        """
        if not QualifiedNameProvider.has_name(
            self,
            original_node,
            QualifiedName(name="typing.Union", source=QualifiedNameSource.IMPORT),
        ):
            return updated_node
        types = [
            cst.ensure_type(
                cst.ensure_type(s, cst.SubscriptElement).slice, cst.Index
            ).value
            for s in updated_node.slice
        ]
        if len(types) == 1:
            return types[0]
        else:
            replacement = cst.BinaryOperation(
                left=types[0], right=types[1], operator=cst.BitOr()
            )
            for type_ in types[2:]:
                replacement = cst.BinaryOperation(
                    left=replacement, right=type_, operator=cst.BitOr()
                )
            return replacement

    def leave_Module(
        self, original_node: cst.Module, updated_node: cst.Module
    ) -> cst.Module:
        RemoveImportsVisitor.remove_unused_import(
            self.context, module="typing", obj="Union"
        )
        return updated_node
