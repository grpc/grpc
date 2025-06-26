# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import List, Optional, Sequence

import libcst as cst
from libcst.codemod import VisitorBasedCodemodCommand
from libcst.codemod.visitors import AddImportsVisitor, RemoveImportsVisitor
from libcst.metadata import (
    ProviderT,
    QualifiedName,
    QualifiedNameProvider,
    QualifiedNameSource,
)


class ConvertNamedTupleToDataclassCommand(VisitorBasedCodemodCommand):
    """
    Convert NamedTuple class declarations to Python 3.7 dataclasses.

    This only performs a conversion at the class declaration level.
    It does not perform type annotation conversions, nor does it convert
    NamedTuple-specific attributes and methods.
    """

    DESCRIPTION: str = (
        "Convert NamedTuple class declarations to Python 3.7 dataclasses using the @dataclass decorator."
    )
    METADATA_DEPENDENCIES: Sequence[ProviderT] = (QualifiedNameProvider,)

    # The 'NamedTuple' we are interested in
    qualified_namedtuple: QualifiedName = QualifiedName(
        name="typing.NamedTuple", source=QualifiedNameSource.IMPORT
    )

    def leave_ClassDef(
        self, original_node: cst.ClassDef, updated_node: cst.ClassDef
    ) -> cst.ClassDef:
        new_bases: List[cst.Arg] = []
        namedtuple_base: Optional[cst.Arg] = None

        # Need to examine the original node's bases since they are directly tied to import metadata
        for base_class in original_node.bases:
            # Compare the base class's qualified name against the expected typing.NamedTuple
            if not QualifiedNameProvider.has_name(
                self, base_class.value, self.qualified_namedtuple
            ):
                # Keep all bases that are not of type typing.NamedTuple
                new_bases.append(base_class)
            else:
                namedtuple_base = base_class

        # We still want to return the updated node in case some of its children have been modified
        if namedtuple_base is None:
            return updated_node

        AddImportsVisitor.add_needed_import(self.context, "dataclasses", "dataclass")
        RemoveImportsVisitor.remove_unused_import_by_node(
            self.context, namedtuple_base.value
        )

        call = cst.ensure_type(
            cst.parse_expression(
                "dataclass(frozen=True)", config=self.module.config_for_parsing
            ),
            cst.Call,
        )
        return updated_node.with_changes(
            lpar=cst.MaybeSentinel.DEFAULT,
            rpar=cst.MaybeSentinel.DEFAULT,
            bases=new_bases,
            decorators=[*original_node.decorators, cst.Decorator(decorator=call)],
        )
