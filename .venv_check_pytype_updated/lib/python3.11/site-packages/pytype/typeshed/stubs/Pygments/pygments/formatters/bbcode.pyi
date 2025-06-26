from typing import Any, TypeVar

from pygments.formatter import Formatter

_T = TypeVar("_T", str, bytes)

class BBCodeFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    styles: Any
    def format_unencoded(self, tokensource, outfile) -> None: ...
