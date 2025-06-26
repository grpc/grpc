from typing import Any, TypeVar

from pygments.formatter import Formatter

_T = TypeVar("_T", str, bytes)

class TerminalFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    darkbg: Any
    colorscheme: Any
    linenos: Any
    def format(self, tokensource, outfile): ...
    def format_unencoded(self, tokensource, outfile) -> None: ...
