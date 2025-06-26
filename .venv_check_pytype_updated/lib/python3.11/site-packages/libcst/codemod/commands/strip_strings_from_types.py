# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from typing import Union

import libcst
import libcst.matchers as m
from libcst import parse_expression
from libcst.codemod import VisitorBasedCodemodCommand
from libcst.codemod.visitors import AddImportsVisitor
from libcst.metadata import QualifiedNameProvider


class StripStringsCommand(VisitorBasedCodemodCommand):
    DESCRIPTION: str = (
        "Converts string type annotations to 3.7-compatible forward references."
    )

    METADATA_DEPENDENCIES = (QualifiedNameProvider,)

    # We want to gate the SimpleString visitor below to only SimpleStrings inside
    # an Annotation.
    @m.call_if_inside(m.Annotation())
    # We also want to gate the SimpleString visitor below to ensure that we don't
    # erroneously strip strings from a Literal.
    @m.call_if_not_inside(
        m.Subscript(
            # We could match on value=m.Name("Literal") here, but then we might miss
            # instances where people are importing typing_extensions directly, or
            # importing Literal as an alias.
            value=m.MatchMetadataIfTrue(
                QualifiedNameProvider,
                lambda qualnames: any(
                    qualname.name == "typing_extensions.Literal"
                    for qualname in qualnames
                ),
            )
        )
    )
    def leave_SimpleString(
        self, original_node: libcst.SimpleString, updated_node: libcst.SimpleString
    ) -> Union[libcst.SimpleString, libcst.BaseExpression]:
        AddImportsVisitor.add_needed_import(self.context, "__future__", "annotations")
        evaluated_value = updated_node.evaluated_value
        # Just use LibCST to evaluate the expression itself, and insert that as the
        # annotation.
        if isinstance(evaluated_value, str):
            return parse_expression(
                evaluated_value, config=self.module.config_for_parsing
            )
        else:
            return updated_node
