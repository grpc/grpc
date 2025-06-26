# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import itertools
import re
from dataclasses import dataclass
from io import BytesIO
from tokenize import detect_encoding as py_tokenize_detect_encoding
from typing import FrozenSet, Iterable, Iterator, Pattern, Set, Tuple, Union

from libcst._nodes.whitespace import NEWLINE_RE
from libcst._parser.parso.python.token import PythonTokenTypes, TokenType
from libcst._parser.parso.utils import split_lines
from libcst._parser.types.config import AutoConfig, ParserConfig, PartialParserConfig
from libcst._parser.types.token import Token
from libcst._parser.wrapped_tokenize import tokenize_lines

_INDENT: TokenType = PythonTokenTypes.INDENT
_NAME: TokenType = PythonTokenTypes.NAME
_NEWLINE: TokenType = PythonTokenTypes.NEWLINE
_STRING: TokenType = PythonTokenTypes.STRING

_FALLBACK_DEFAULT_NEWLINE = "\n"
_FALLBACK_DEFAULT_INDENT = "    "
_CONTINUATION_RE: Pattern[str] = re.compile(r"\\(\r\n?|\n)", re.UNICODE)


@dataclass(frozen=True)
class ConfigDetectionResult:
    # The config is a set of constant values used by the parser.
    config: ParserConfig
    # The tokens iterator is mutated by the parser.
    tokens: Iterator[Token]


def _detect_encoding(source: Union[str, bytes]) -> str:
    """
    Detects the encoding from the presence of a UTF-8 BOM or an encoding cookie as
    specified in PEP 263.

    If given a string (instead of bytes) the encoding is assumed to be utf-8.
    """

    if isinstance(source, str):
        return "utf-8"
    return py_tokenize_detect_encoding(BytesIO(source).readline)[0]


def _detect_default_newline(source_str: str) -> str:
    """
    Finds the first newline, and uses that value as the default newline.
    """
    # Don't use `NEWLINE_RE` for this, because it might match multiple newlines as a
    # single newline.
    match = NEWLINE_RE.search(source_str)
    return match.group(0) if match is not None else _FALLBACK_DEFAULT_NEWLINE


def _detect_indent(tokens: Iterable[Token]) -> str:
    """
    Finds the first INDENT token, and uses that as the value of the default indent.
    """
    try:
        first_indent = next(t for t in tokens if t.type is _INDENT)
    except StopIteration:
        return _FALLBACK_DEFAULT_INDENT
    first_indent_str = first_indent.relative_indent
    assert first_indent_str is not None, "INDENT tokens must contain a relative_indent"
    return first_indent_str


def _detect_trailing_newline(source_str: str) -> bool:
    if len(source_str) == 0 or not NEWLINE_RE.fullmatch(source_str[-1]):
        return False
    # Make sure that the last newline wasn't following a continuation
    return not (
        _CONTINUATION_RE.fullmatch(source_str[-2:])
        or _CONTINUATION_RE.fullmatch(source_str[-3:])
    )


def _detect_future_imports(tokens: Iterable[Token]) -> FrozenSet[str]:
    """
    Finds __future__ imports in their proper locations.

    See `https://www.python.org/dev/peps/pep-0236/`_
    """
    future_imports: Set[str] = set()
    state = 0
    for tok in tokens:
        if state == 0 and tok.type in (_STRING, _NEWLINE):
            continue
        elif state == 0 and tok.string == "from":
            state = 1
        elif state == 1 and tok.string == "__future__":
            state = 2
        elif state == 2 and tok.string == "import":
            state = 3
        elif state == 3 and tok.string == "as":
            state = 4
        elif state == 3 and tok.type == _NAME:
            future_imports.add(tok.string)
        elif state == 4 and tok.type == _NAME:
            state = 3
        elif state == 3 and tok.string in "(),":
            continue
        elif state == 3 and tok.type == _NEWLINE:
            state = 0
        else:
            break
    return frozenset(future_imports)


def convert_to_utf8(
    source: Union[str, bytes], *, partial: PartialParserConfig
) -> Tuple[str, str]:
    """
    Returns an (original encoding, converted source) tuple.
    """
    partial_encoding = partial.encoding
    encoding = (
        _detect_encoding(source)
        if isinstance(partial_encoding, AutoConfig)
        else partial_encoding
    )

    source_str = source if isinstance(source, str) else source.decode(encoding)
    return (encoding, source_str)


def detect_config(
    source: Union[str, bytes],
    *,
    partial: PartialParserConfig,
    detect_trailing_newline: bool,
    detect_default_newline: bool,
) -> ConfigDetectionResult:
    """
    Computes a ParserConfig given the current source code to be parsed and a partial
    config.
    """

    python_version = partial.parsed_python_version

    encoding, source_str = convert_to_utf8(source, partial=partial)

    partial_default_newline = partial.default_newline
    default_newline = (
        (
            _detect_default_newline(source_str)
            if detect_default_newline
            else _FALLBACK_DEFAULT_NEWLINE
        )
        if isinstance(partial_default_newline, AutoConfig)
        else partial_default_newline
    )

    # HACK: The grammar requires a trailing newline, but python doesn't actually require
    # a trailing newline. Add one onto the end to make the parser happy. We'll strip it
    # out again during cst.Module's codegen.
    #
    # I think parso relies on error recovery support to handle this, which we don't
    # have. lib2to3 doesn't handle this case at all AFAICT.
    has_trailing_newline = detect_trailing_newline and _detect_trailing_newline(
        source_str
    )
    if detect_trailing_newline and not has_trailing_newline:
        source_str += default_newline

    lines = split_lines(source_str, keepends=True)

    tokens = tokenize_lines(source_str, lines, python_version)

    partial_default_indent = partial.default_indent
    if isinstance(partial_default_indent, AutoConfig):
        # We need to clone `tokens` before passing it to `_detect_indent`, because
        # `_detect_indent` consumes some tokens, mutating `tokens`.
        #
        # Implementation detail: CPython's `itertools.tee` uses weakrefs to reduce the
        # size of its FIFO, so this doesn't retain items (leak memory) for `tokens_dup`
        # once `token_dup` is freed at the end of this method (subject to
        # GC/refcounting).
        tokens, tokens_dup = itertools.tee(tokens)
        default_indent = _detect_indent(tokens_dup)
    else:
        default_indent = partial_default_indent

    partial_future_imports = partial.future_imports
    if isinstance(partial_future_imports, AutoConfig):
        # Same note as above re itertools.tee, we will consume tokens.
        tokens, tokens_dup = itertools.tee(tokens)
        future_imports = _detect_future_imports(tokens_dup)
    else:
        future_imports = partial_future_imports

    return ConfigDetectionResult(
        config=ParserConfig(
            lines=lines,
            encoding=encoding,
            default_indent=default_indent,
            default_newline=default_newline,
            has_trailing_newline=has_trailing_newline,
            version=python_version,
            future_imports=future_imports,
        ),
        tokens=tokens,
    )
