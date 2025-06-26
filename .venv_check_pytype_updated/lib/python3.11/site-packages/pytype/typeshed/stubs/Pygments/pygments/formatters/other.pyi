from typing import Any, TypeVar

from pygments.formatter import Formatter

_T = TypeVar("_T", str, bytes)

class NullFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    def format(self, tokensource, outfile) -> None: ...

class RawTokenFormatter(Formatter[_T]):
    name: str
    aliases: Any
    filenames: Any
    unicodeoutput: bool
    encoding: str
    compress: Any
    error_color: Any
    def format(self, tokensource, outfile) -> None: ...

class TestcaseFormatter(Formatter[_T]):
    name: str
    aliases: Any
    def format(self, tokensource, outfile) -> None: ...
