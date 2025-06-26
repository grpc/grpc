from typing import Any, TypeVar

from pygments.formatter import Formatter
from pygments.lexer import Lexer

_T = TypeVar("_T", str, bytes)

class LatexFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    docclass: Any
    preamble: Any
    linenos: Any
    linenostart: Any
    linenostep: Any
    verboptions: Any
    nobackground: Any
    commandprefix: Any
    texcomments: Any
    mathescape: Any
    escapeinside: Any
    left: Any
    right: Any
    envname: Any
    def get_style_defs(self, arg: str = ""): ...
    def format_unencoded(self, tokensource, outfile) -> None: ...

class LatexEmbeddedLexer(Lexer):
    left: Any
    right: Any
    lang: Any
    def __init__(self, left, right, lang, **options) -> None: ...
    def get_tokens_unprocessed(self, text): ...
