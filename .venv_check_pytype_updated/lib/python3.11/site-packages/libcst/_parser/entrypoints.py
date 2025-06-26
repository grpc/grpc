# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

"""
Parser entrypoints define the way users of our API are allowed to interact with the
parser. A parser entrypoint should take the source code and some configuration
information
"""

import os
from functools import partial
from typing import Union

from libcst._nodes.base import CSTNode
from libcst._nodes.expression import BaseExpression
from libcst._nodes.module import Module
from libcst._nodes.statement import BaseCompoundStatement, SimpleStatementLine
from libcst._parser.detect_config import convert_to_utf8, detect_config
from libcst._parser.grammar import get_grammar, validate_grammar
from libcst._parser.python_parser import PythonCSTParser
from libcst._parser.types.config import PartialParserConfig

_DEFAULT_PARTIAL_PARSER_CONFIG: PartialParserConfig = PartialParserConfig()


def is_native() -> bool:
    typ = os.environ.get("LIBCST_PARSER_TYPE")
    return typ != "pure"


def _parse(
    entrypoint: str,
    source: Union[str, bytes],
    config: PartialParserConfig,
    *,
    detect_trailing_newline: bool,
    detect_default_newline: bool,
) -> CSTNode:
    if is_native():
        from libcst.native import parse_expression, parse_module, parse_statement

        encoding, source_str = convert_to_utf8(source, partial=config)

        if entrypoint == "file_input":
            parse = partial(parse_module, encoding=encoding)
        elif entrypoint == "stmt_input":
            parse = parse_statement
        elif entrypoint == "expression_input":
            parse = parse_expression
        else:
            raise ValueError(f"Unknown parser entry point: {entrypoint}")

        return parse(source_str)
    return _pure_python_parse(
        entrypoint,
        source,
        config,
        detect_trailing_newline=detect_trailing_newline,
        detect_default_newline=detect_default_newline,
    )


def _pure_python_parse(
    entrypoint: str,
    source: Union[str, bytes],
    config: PartialParserConfig,
    *,
    detect_trailing_newline: bool,
    detect_default_newline: bool,
) -> CSTNode:
    detection_result = detect_config(
        source,
        partial=config,
        detect_trailing_newline=detect_trailing_newline,
        detect_default_newline=detect_default_newline,
    )
    validate_grammar()
    grammar = get_grammar(config.parsed_python_version, config.future_imports)

    parser = PythonCSTParser(
        tokens=detection_result.tokens,
        config=detection_result.config,
        pgen_grammar=grammar,
        start_nonterminal=entrypoint,
    )
    # The parser has an Any return type, we can at least refine it to CSTNode here.
    result = parser.parse()
    assert isinstance(result, CSTNode)
    return result


def parse_module(
    source: Union[str, bytes],  # the only entrypoint that accepts bytes
    config: PartialParserConfig = _DEFAULT_PARTIAL_PARSER_CONFIG,
) -> Module:
    """
    Accepts an entire python module, including all leading and trailing whitespace.

    If source is ``bytes``, the encoding will be inferred and preserved. If
    the source is a ``string``, we will default to assuming UTF-8 encoding if the
    module is rendered back out to source as bytes. It is recommended that when
    calling :func:`~libcst.parse_module` with a string you access the serialized
    code using :class:`~libcst.Module`'s code attribute, and when calling it with
    bytes you access the serialized code using :class:`~libcst.Module`'s bytes
    attribute.
    """
    result = _parse(
        "file_input",
        source,
        config,
        detect_trailing_newline=True,
        detect_default_newline=True,
    )
    assert isinstance(result, Module)
    return result


def parse_statement(
    source: str, config: PartialParserConfig = _DEFAULT_PARTIAL_PARSER_CONFIG
) -> Union[SimpleStatementLine, BaseCompoundStatement]:
    """
    Accepts a statement followed by a trailing newline. If a trailing newline is not
    provided, one will be added. :func:`parse_statement` is provided mainly as a
    convenience function to generate semi-complex trees from code snippetes. If you
    need to represent a statement exactly, including all leading/trailing comments,
    you should instead use :func:`parse_module`.

    Leading comments and trailing comments (on the same line) are accepted, but
    whitespace (or anything else) after the statement's trailing newline is not valid
    (there's nowhere to store it on the statement node). Note that since there is
    nowhere to store leading and trailing comments/empty lines, code rendered out
    from a parsed statement using ``cst.Module([]).code_for_node(statement)`` will
    not include leading/trailing comments.
    """
    # use detect_trailing_newline to insert a newline
    result = _parse(
        "stmt_input",
        source,
        config,
        detect_trailing_newline=True,
        detect_default_newline=False,
    )
    assert isinstance(result, (SimpleStatementLine, BaseCompoundStatement))
    return result


def parse_expression(
    source: str, config: PartialParserConfig = _DEFAULT_PARTIAL_PARSER_CONFIG
) -> BaseExpression:
    """
    Accepts an expression on a single line. Leading and trailing whitespace is not
    valid (there's nowhere to store it on the expression node).
    :func:`parse_expression` is provided mainly as a convenience function to generate
    semi-complex trees from code snippets. If you need to represent an expression
    exactly, including all leading/trailing comments, you should instead use
    :func:`parse_module`.
    """
    result = _parse(
        "expression_input",
        source,
        config,
        detect_trailing_newline=False,
        detect_default_newline=False,
    )
    assert isinstance(result, BaseExpression)
    return result
