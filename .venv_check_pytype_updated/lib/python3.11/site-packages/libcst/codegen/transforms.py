# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# This holds a series of transforms that help prettify generated code.
# The design is such that any of them could be left out and the code
# in question will still be correct, but possibly uglier to look at.
# Great care should be taken to include only transforms that do not
# affect the behavior of generated code, only the style for readability.
# As a result, since these can be skipped without harm, it is okay to
# use features such as matchers which rely on previously generated
# code to function.

import ast

import libcst as cst
import libcst.matchers as m


class SimplifyUnionsTransformer(m.MatcherDecoratableTransformer):
    @m.leave(m.Subscript(m.Name("Union")))
    def _leave_union(
        self, original_node: cst.Subscript, updated_node: cst.Subscript
    ) -> cst.BaseExpression:
        if len(updated_node.slice) == 1:
            # This is a Union[SimpleType,] which is equivalent to just SimpleType
            return cst.ensure_type(updated_node.slice[0].slice, cst.Index).value
        return updated_node


class DoubleQuoteForwardRefsTransformer(m.MatcherDecoratableTransformer):
    @m.call_if_inside(m.Annotation())
    def leave_SimpleString(
        self, original_node: cst.SimpleString, updated_node: cst.SimpleString
    ) -> cst.SimpleString:
        # For prettiness, convert all single-quoted forward refs to double-quoted.
        if "'" in updated_node.quote:
            new_value = f'"{updated_node.value[1:-1]}"'
            try:
                if updated_node.evaluated_value == ast.literal_eval(new_value):
                    return updated_node.with_changes(value=new_value)
            except SyntaxError:
                pass
        return updated_node
