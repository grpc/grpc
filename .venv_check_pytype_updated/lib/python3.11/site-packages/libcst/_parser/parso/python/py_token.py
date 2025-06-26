# Copyright 2004-2005 Elemental Security, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# Modifications:
# Copyright David Halter and Contributors
# Modifications are dual-licensed: MIT and PSF.
# 99% of the code is different from pgen2, now.
#
# A fork of `parso.python.token`.
# https://github.com/davidhalter/parso/blob/master/parso/python/token.py
#
# The following changes were made:
# - Explicit TokenType references instead of dynamic creation.
# - Use dataclasses instead of raw classes.
# pyre-unsafe

from dataclasses import dataclass


@dataclass(frozen=True)
class TokenType:
    name: str
    contains_syntax: bool = False

    def __repr__(self) -> str:
        return "%s(%s)" % (self.__class__.__name__, self.name)


class PythonTokenTypes:
    """
    Basically an enum, but Python 2 doesn't have enums in the standard library.
    """

    STRING: TokenType = TokenType("STRING")
    NUMBER: TokenType = TokenType("NUMBER")
    NAME: TokenType = TokenType("NAME", contains_syntax=True)
    ERRORTOKEN: TokenType = TokenType("ERRORTOKEN")
    NEWLINE: TokenType = TokenType("NEWLINE")
    INDENT: TokenType = TokenType("INDENT")
    DEDENT: TokenType = TokenType("DEDENT")
    ERROR_DEDENT: TokenType = TokenType("ERROR_DEDENT")
    ASYNC: TokenType = TokenType("ASYNC")
    AWAIT: TokenType = TokenType("AWAIT")
    FSTRING_STRING: TokenType = TokenType("FSTRING_STRING")
    FSTRING_START: TokenType = TokenType("FSTRING_START")
    FSTRING_END: TokenType = TokenType("FSTRING_END")
    OP: TokenType = TokenType("OP", contains_syntax=True)
    ENDMARKER: TokenType = TokenType("ENDMARKER")
