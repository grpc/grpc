# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import argparse
import ast
from typing import Generator, List, Optional, Sequence, Set, Tuple

import libcst as cst
import libcst.matchers as m
from libcst import CSTLogicError
from libcst._exceptions import ParserSyntaxError
from libcst.codemod import (
    CodemodContext,
    ContextAwareTransformer,
    ContextAwareVisitor,
    VisitorBasedCodemodCommand,
)


def _get_lhs(field: cst.BaseExpression) -> cst.BaseExpression:
    if isinstance(field, (cst.Name, cst.Integer)):
        return field
    elif isinstance(field, (cst.Attribute, cst.Subscript)):
        return _get_lhs(field.value)
    else:
        raise TypeError("Unsupported node type!")


def _find_expr_from_field_name(
    fieldname: str, args: Sequence[cst.Arg]
) -> Optional[cst.BaseExpression]:
    # Things like "0.name" are invalid expressions in python since
    # we can't tell if name is supposed to be the fraction or a name.
    # So we do a trick to parse here where we wrap the LHS in parens
    # and assume LibCST will handle it.
    if "." in fieldname:
        ind, exp = fieldname.split(".", 1)
        fieldname = f"({ind}).{exp}"
    field_expr = cst.parse_expression(fieldname)
    lhs = _get_lhs(field_expr)

    # Verify we don't have any *args or **kwargs attributes.
    if any(arg.star != "" for arg in args):
        return None

    # Get the index into the arg
    index: Optional[int] = None
    if isinstance(lhs, cst.Integer):
        index = int(lhs.value)
        if index < 0 or index >= len(args):
            raise CSTLogicError(f"Logic error, arg sequence {index} out of bounds!")
    elif isinstance(lhs, cst.Name):
        for i, arg in enumerate(args):
            kw = arg.keyword
            if kw is None:
                continue
            if kw.value == lhs.value:
                index = i
                break
        if index is None:
            raise CSTLogicError(f"Logic error, arg name {lhs.value} out of bounds!")

    if index is None:
        raise CSTLogicError(
            f"Logic error, unsupported fieldname expression {fieldname}!"
        )

    # Format it!
    return field_expr.deep_replace(lhs, args[index].value)


def _get_field(formatstr: str) -> Tuple[str, Optional[str], Optional[str]]:
    in_index: int = 0
    format_spec: Optional[str] = None
    conversion: Optional[str] = None

    # Grab any format spec as long as its not an array slice
    for pos, char in enumerate(formatstr):
        if char == "[":
            in_index += 1
        elif char == "]":
            in_index -= 1
        elif char == ":":
            if in_index == 0:
                formatstr, format_spec = (formatstr[:pos], formatstr[pos + 1 :])
                break

    # Grab any conversion
    if "!" in formatstr:
        formatstr, conversion = formatstr.split("!", 1)

    # Return it
    return formatstr, format_spec, conversion


def _get_tokens(  # noqa: C901
    string: str,
) -> Generator[Tuple[str, Optional[str], Optional[str], Optional[str]], None, None]:
    length = len(string)
    prefix: str = ""
    format_accum: str = ""
    in_brackets: int = 0
    seen_escape: bool = False

    for pos, char in enumerate(string):
        if seen_escape:
            # The last character was an escape character, so consume
            # this one as well, and then pop out of the escape.
            if in_brackets == 0:
                prefix += char
            else:
                format_accum += char
            seen_escape = False
            continue

        # We can't escape inside a f-string/format specifier.
        if in_brackets == 0:
            # Grab the next character to see if we are an escape sequence.
            next_char: Optional[str] = None
            if pos < length - 1:
                next_char = string[pos + 1]

            # If this current character is an escape, we want to
            # not react to it, append it to the current accumulator and
            # then do the same for the next character.
            if char == "{" and next_char == "{":
                seen_escape = True
            if char == "}" and next_char == "}":
                seen_escape = True

        # Only if we are not an escape sequence do we consider these
        # brackets.
        if not seen_escape:
            if char == "{":
                in_brackets += 1

                # We want to add brackets to the format accumulator as
                # long as they aren't the outermost, because format
                # specs allow {} expansion.
                if in_brackets == 1:
                    continue
            if char == "}":
                in_brackets -= 1

                if in_brackets < 0:
                    raise ValueError("Stray } in format string!")

                if in_brackets == 0:
                    field_name, format_spec, conversion = _get_field(format_accum)
                    yield (prefix, field_name, format_spec, conversion)

                    prefix = ""
                    format_accum = ""
                    continue

        # Place in the correct accumulator
        if in_brackets == 0:
            prefix += char
        else:
            format_accum += char

    if in_brackets > 0:
        raise ParserSyntaxError(
            "Stray { in format string!", lines=[string], raw_line=0, raw_column=0
        )
    if format_accum:
        raise CSTLogicError("Logic error!")

    # Yield the last bit of information
    yield (prefix, None, None, None)


class StringQuoteGatherer(ContextAwareVisitor):
    def __init__(self, context: CodemodContext) -> None:
        super().__init__(context)
        self.stringends: Set[str] = set()

    def visit_SimpleString(self, node: cst.SimpleString) -> None:
        self.stringends.add(node.value[-1])


class StripNewlinesTransformer(ContextAwareTransformer):
    def leave_ParenthesizedWhitespace(
        self,
        original_node: cst.ParenthesizedWhitespace,
        updated_node: cst.ParenthesizedWhitespace,
    ) -> cst.SimpleWhitespace:
        return cst.SimpleWhitespace(" ")


class SwitchStringQuotesTransformer(ContextAwareTransformer):
    def __init__(self, context: CodemodContext, avoid_quote: str) -> None:
        super().__init__(context)
        if avoid_quote not in {'"', "'"}:
            raise ValueError("Must specify either ' or \" single quote to avoid.")
        self.avoid_quote: str = avoid_quote
        self.replace_quote: str = '"' if avoid_quote == "'" else "'"

    def leave_SimpleString(
        self, original_node: cst.SimpleString, updated_node: cst.SimpleString
    ) -> cst.SimpleString:
        if self.avoid_quote in updated_node.quote:
            # Attempt to swap the value out, verify that the string is still identical
            # before and after transformation.
            new_quote = updated_node.quote.replace(self.avoid_quote, self.replace_quote)
            new_value = (
                f"{updated_node.prefix}{new_quote}{updated_node.raw_value}{new_quote}"
            )

            try:
                new_str = ast.literal_eval(new_value)
                if updated_node.evaluated_value != new_str:
                    # This isn't the same!
                    return updated_node

                return updated_node.with_changes(value=new_value)
            except Exception:
                # Failed to parse string, changing the quoting screwed us up.
                pass

        # Either failed to parse the new string, or don't need to make changes.
        return updated_node


class ConvertFormatStringCommand(VisitorBasedCodemodCommand):
    DESCRIPTION: str = "Converts instances of str.format() to f-string."

    @staticmethod
    def add_args(arg_parser: argparse.ArgumentParser) -> None:
        arg_parser.add_argument(
            "--allow-strip-comments",
            dest="allow_strip_comments",
            help=(
                "Allow stripping comments inside .format() calls when converting "
                + "to f-strings."
            ),
            action="store_true",
        )
        arg_parser.add_argument(
            "--allow-await",
            dest="allow_await",
            help=(
                "Allow converting expressions inside .format() calls that contain "
                + "an await expression (only compatible with Python 3.7+)."
            ),
            action="store_true",
        )

    def __init__(
        self,
        context: CodemodContext,
        allow_strip_comments: bool = False,
        allow_await: bool = False,
    ) -> None:
        super().__init__(context)
        self.allow_strip_comments = allow_strip_comments
        self.allow_await = allow_await

    def leave_Call(  # noqa: C901
        self, original_node: cst.Call, updated_node: cst.Call
    ) -> cst.BaseExpression:
        # Lets figure out if this is a "".format() call
        extraction = self.extract(
            updated_node,
            m.Call(
                func=m.Attribute(
                    value=m.SaveMatchedNode(m.SimpleString(), "string"),
                    attr=m.Name("format"),
                )
            ),
        )
        if extraction is not None:
            fstring: List[cst.BaseFormattedStringContent] = []
            inserted_sequence: int = 0
            stringnode = cst.ensure_type(extraction["string"], cst.SimpleString)
            tokens = _get_tokens(stringnode.raw_value)
            for literal_text, field_name, format_spec, conversion in tokens:
                if literal_text:
                    fstring.append(cst.FormattedStringText(literal_text))
                if field_name is None:
                    # This is not a format-specification
                    continue
                # Auto-insert field sequence if it is empty
                if field_name == "":
                    field_name = str(inserted_sequence)
                    inserted_sequence += 1

                # Now, if there is a valid format spec, parse it as a f-string
                # as well, since it allows for insertion of parameters just
                # like regular f-strings.
                format_spec_parts: List[cst.BaseFormattedStringContent] = []
                if format_spec is not None and len(format_spec) > 0:
                    # Parse the format spec out as a series of tokens as well.
                    format_spec_tokens = _get_tokens(format_spec)
                    for (
                        spec_literal_text,
                        spec_field_name,
                        spec_format_spec,
                        spec_conversion,
                    ) in format_spec_tokens:
                        if spec_format_spec is not None:
                            # This shouldn't be possible, we don't allow it in the spec!
                            raise CSTLogicError("Logic error!")
                        if spec_literal_text:
                            format_spec_parts.append(
                                cst.FormattedStringText(spec_literal_text)
                            )
                        if spec_field_name is None:
                            # This is not a format-specification
                            continue
                        # Auto-insert field sequence if it is empty
                        if spec_field_name == "":
                            spec_field_name = str(inserted_sequence)
                            inserted_sequence += 1

                        # Now, convert the spec expression itself.
                        fstring_expression = self._convert_token_to_fstring_expression(
                            spec_field_name,
                            spec_conversion,
                            updated_node.args,
                            stringnode,
                        )
                        if fstring_expression is None:
                            return updated_node
                        format_spec_parts.append(fstring_expression)

                # Finally, output the converted value.
                fstring_expression = self._convert_token_to_fstring_expression(
                    field_name, conversion, updated_node.args, stringnode
                )
                if fstring_expression is None:
                    return updated_node
                # Technically its valid to add the parts even if it is empty, but
                # it results in an empty format spec being added which is ugly.
                if format_spec_parts:
                    fstring_expression = fstring_expression.with_changes(
                        format_spec=format_spec_parts
                    )
                fstring.append(fstring_expression)

            # We converted each part, so lets bang together the f-string itself.
            return cst.FormattedString(
                parts=fstring,
                start=f"f{stringnode.prefix}{stringnode.quote}",
                end=stringnode.quote,
            )

        return updated_node

    def _convert_token_to_fstring_expression(
        self,
        field_name: str,
        conversion: Optional[str],
        arguments: Sequence[cst.Arg],
        containing_string: cst.SimpleString,
    ) -> Optional[cst.FormattedStringExpression]:
        expr = _find_expr_from_field_name(field_name, arguments)
        if expr is None:
            # Most likely they used * expansion in a format.
            self.warn(f"Unsupported field_name {field_name} in format() call")
            return None

        # Verify that we don't have any comments or newlines. Comments aren't
        # allowed in f-strings, and newlines need parenthesization. We can
        # have formattedstrings inside other formattedstrings, but I chose not
        # to doeal with that for now.
        if self.findall(expr, m.Comment()) and not self.allow_strip_comments:
            # We could strip comments, but this is a formatting change so
            # we choose not to for now.
            self.warn("Unsupported comment in format() call")
            return None
        if self.findall(expr, m.FormattedString()):
            self.warn("Unsupported f-string in format() call")
            return None
        if self.findall(expr, m.Await()) and not self.allow_await:
            # This is fixed in 3.7 but we don't currently have a flag
            # to enable/disable it.
            self.warn("Unsupported await in format() call")
            return None

        # Stripping newlines is effectively a format-only change.
        expr = cst.ensure_type(
            expr.visit(StripNewlinesTransformer(self.context)),
            cst.BaseExpression,
        )

        # Try our best to swap quotes on any strings that won't fit
        expr = cst.ensure_type(
            expr.visit(
                SwitchStringQuotesTransformer(self.context, containing_string.quote[0])
            ),
            cst.BaseExpression,
        )

        # Verify that the resulting expression doesn't have a backslash
        # in it.
        raw_expr_string = self.module.code_for_node(expr)
        if "\\" in raw_expr_string:
            self.warn("Unsupported backslash in format expression")
            return None

        # For safety sake, if this is a dict/set or dict/set comprehension,
        # wrap it in parens so that it doesn't accidentally create an
        # escape.
        if (raw_expr_string.startswith("{") or raw_expr_string.endswith("}")) and (
            not expr.lpar or not expr.rpar
        ):
            expr = expr.with_changes(lpar=[cst.LeftParen()], rpar=[cst.RightParen()])

        # Verify that any strings we insert don't have the same quote
        quote_gatherer = StringQuoteGatherer(self.context)
        expr.visit(quote_gatherer)
        for stringend in quote_gatherer.stringends:
            if stringend in containing_string.quote:
                self.warn("Cannot embed string with same quote from format() call")
                return None

        return cst.FormattedStringExpression(expression=expr, conversion=conversion)
