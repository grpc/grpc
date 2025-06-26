# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import itertools
import re
from typing import Callable, cast, List, Sequence

import libcst as cst
import libcst.matchers as m
from libcst.codemod import VisitorBasedCodemodCommand

USE_FSTRING_SIMPLE_EXPRESSION_MAX_LENGTH = 30


def _match_simple_string(node: cst.CSTNode) -> bool:
    if isinstance(node, cst.SimpleString) and not node.prefix.lower().startswith("b"):
        # SimpleString can be a bytes and fstring don't support bytes
        return re.fullmatch("[^%]*(%s[^%]*)+", node.raw_value) is not None
    return False


def _gen_match_simple_expression(module: cst.Module) -> Callable[[cst.CSTNode], bool]:
    def _match_simple_expression(node: cst.CSTNode) -> bool:
        # either each element in Tuple is simple expression or the entire expression is simple.
        if (
            isinstance(node, cst.Tuple)
            and all(
                len(module.code_for_node(elm.value))
                < USE_FSTRING_SIMPLE_EXPRESSION_MAX_LENGTH
                for elm in node.elements
            )
        ) or len(module.code_for_node(node)) < USE_FSTRING_SIMPLE_EXPRESSION_MAX_LENGTH:
            return True
        return False

    return _match_simple_expression


class EscapeStringQuote(cst.CSTTransformer):
    def __init__(self, quote: str) -> None:
        self.quote = quote
        super().__init__()

    def leave_SimpleString(
        self, original_node: cst.SimpleString, updated_node: cst.SimpleString
    ) -> cst.SimpleString:
        if self.quote == original_node.quote:
            for quo in ["'", '"', "'''", '"""']:
                if quo != original_node.quote and quo not in original_node.raw_value:
                    escaped_string = cst.SimpleString(
                        original_node.prefix + quo + original_node.raw_value + quo
                    )
                    if escaped_string.evaluated_value != original_node.evaluated_value:
                        raise ValueError(
                            f"Failed to escape string:\n  original:{original_node.value}\n  escaped:{escaped_string.value}"
                        )
                    else:
                        return escaped_string
            raise ValueError(
                f"Cannot find a good quote for escaping the SimpleString: {original_node.value}"
            )
        return original_node


class ConvertPercentFormatStringCommand(VisitorBasedCodemodCommand):
    DESCRIPTION: str = "Converts simple % style string format to f-string."

    def leave_BinaryOperation(
        self, original_node: cst.BinaryOperation, updated_node: cst.BinaryOperation
    ) -> cst.BaseExpression:
        expr_key = "expr"
        extracts = m.extract(
            original_node,
            m.BinaryOperation(
                # pyre-fixme[6]: Expected `Union[m._matcher_base.AllOf[typing.Union[m...
                left=m.MatchIfTrue(_match_simple_string),
                operator=m.Modulo(),
                # pyre-fixme[6]: Expected `Union[m._matcher_base.AllOf[typing.Union[m...
                right=m.SaveMatchedNode(
                    m.MatchIfTrue(_gen_match_simple_expression(self.module)),
                    expr_key,
                ),
            ),
        )

        if extracts:
            exprs = extracts[expr_key]
            exprs = (exprs,) if not isinstance(exprs, Sequence) else exprs
            parts = []
            simple_string = cst.ensure_type(original_node.left, cst.SimpleString)
            innards = simple_string.raw_value.replace("{", "{{").replace("}", "}}")
            tokens = innards.split("%s")
            token = tokens[0]
            if len(token) > 0:
                parts.append(cst.FormattedStringText(value=token))
            expressions: List[cst.CSTNode] = list(
                *itertools.chain(
                    (
                        [elm.value for elm in expr.elements]
                        if isinstance(expr, cst.Tuple)
                        else [expr]
                    )
                    for expr in exprs
                )
            )
            escape_transformer = EscapeStringQuote(simple_string.quote)
            i = 1
            while i < len(tokens):
                if i - 1 >= len(expressions):
                    # the %-string doesn't come with same number of elements in tuple
                    return original_node
                try:
                    parts.append(
                        cst.FormattedStringExpression(
                            expression=cast(
                                cst.BaseExpression,
                                expressions[i - 1].visit(escape_transformer),
                            )
                        )
                    )
                except Exception:
                    return original_node
                token = tokens[i]
                if len(token) > 0:
                    parts.append(cst.FormattedStringText(value=token))
                i += 1
            start = f"f{simple_string.prefix}{simple_string.quote}"
            return cst.FormattedString(
                parts=parts, start=start, end=simple_string.quote
            )

        return original_node
