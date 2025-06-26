from _typeshed import Incomplete
from typing import Any, TypeVar

from pygments.formatter import Formatter

_T = TypeVar("_T", str, bytes)

class EscapeSequence:
    fg: Any
    bg: Any
    bold: Any
    underline: Any
    italic: Any
    def __init__(
        self,
        fg: Incomplete | None = None,
        bg: Incomplete | None = None,
        bold: bool = False,
        underline: bool = False,
        italic: bool = False,
    ) -> None: ...
    def escape(self, attrs): ...
    def color_string(self): ...
    def true_color_string(self): ...
    def reset_string(self): ...

class Terminal256Formatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    xterm_colors: Any
    best_match: Any
    style_string: Any
    usebold: Any
    useunderline: Any
    useitalic: Any
    linenos: Any
    def format(self, tokensource, outfile): ...
    def format_unencoded(self, tokensource, outfile) -> None: ...

class TerminalTrueColorFormatter(Terminal256Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
