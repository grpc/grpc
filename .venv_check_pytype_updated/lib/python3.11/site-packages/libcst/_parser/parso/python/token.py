# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

try:
    from libcst_native import token_type as native_token_type

    TokenType = native_token_type.TokenType

    class PythonTokenTypes:
        STRING: TokenType = native_token_type.STRING
        NUMBER: TokenType = native_token_type.NUMBER
        NAME: TokenType = native_token_type.NAME
        NEWLINE: TokenType = native_token_type.NEWLINE
        INDENT: TokenType = native_token_type.INDENT
        DEDENT: TokenType = native_token_type.DEDENT
        ASYNC: TokenType = native_token_type.ASYNC
        AWAIT: TokenType = native_token_type.AWAIT
        FSTRING_STRING: TokenType = native_token_type.FSTRING_STRING
        FSTRING_START: TokenType = native_token_type.FSTRING_START
        FSTRING_END: TokenType = native_token_type.FSTRING_END
        OP: TokenType = native_token_type.OP
        ENDMARKER: TokenType = native_token_type.ENDMARKER
        # unused dummy tokens for backwards compat with the parso tokenizer
        ERRORTOKEN: TokenType = native_token_type.ERRORTOKEN
        ERROR_DEDENT: TokenType = native_token_type.ERROR_DEDENT

except ImportError:
    from libcst._parser.parso.python.py_token import (  # noqa F401
        PythonTokenTypes,
        TokenType,
    )
