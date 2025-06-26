# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Iterable, Union

from libcst._exceptions import EOFSentinel
from libcst._parser.parso.pgen2.generator import ReservedString
from libcst._parser.parso.python.token import PythonTokenTypes, TokenType
from libcst._parser.types.token import Token

_EOF_STR: str = "end of file (EOF)"
_INDENT_STR: str = "an indent"
_DEDENT_STR: str = "a dedent"


def get_expected_str(
    encountered: Union[Token, EOFSentinel],
    expected: Union[Iterable[Union[TokenType, ReservedString]], EOFSentinel],
) -> str:
    if (
        isinstance(encountered, EOFSentinel)
        or encountered.type is PythonTokenTypes.ENDMARKER
    ):
        encountered_str = _EOF_STR
    elif encountered.type is PythonTokenTypes.INDENT:
        encountered_str = _INDENT_STR
    elif encountered.type is PythonTokenTypes.DEDENT:
        encountered_str = _DEDENT_STR
    else:
        encountered_str = repr(encountered.string)

    if isinstance(expected, EOFSentinel):
        expected_names = [_EOF_STR]
    else:
        expected_names = sorted(
            [
                repr(el.name) if isinstance(el, TokenType) else repr(el.value)
                for el in expected
            ]
        )

    if len(expected_names) > 10:
        # There's too many possibilities, so it's probably not useful to list them.
        # Instead, let's just abbreviate the message.
        return f"Unexpectedly encountered {encountered_str}."
    else:
        if len(expected_names) == 1:
            expected_str = expected_names[0]
        else:
            expected_str = f"{', '.join(expected_names[:-1])}, or {expected_names[-1]}"
        return f"Encountered {encountered_str}, but expected {expected_str}."
