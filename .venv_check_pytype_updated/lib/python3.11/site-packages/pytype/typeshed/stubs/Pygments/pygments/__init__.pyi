from _typeshed import SupportsWrite
from collections.abc import Iterator
from typing import TypeVar, overload

from pygments.formatter import Formatter
from pygments.lexer import Lexer
from pygments.token import _TokenType

_T = TypeVar("_T", str, bytes)

__version__: str
__all__ = ["lex", "format", "highlight"]

def lex(code: str, lexer: Lexer) -> Iterator[tuple[_TokenType, str]]: ...
@overload
def format(tokens, formatter: Formatter[_T], outfile: SupportsWrite[_T]) -> None: ...
@overload
def format(tokens, formatter: Formatter[_T], outfile: None = None) -> _T: ...
@overload
def highlight(code, lexer, formatter: Formatter[_T], outfile: SupportsWrite[_T]) -> None: ...
@overload
def highlight(code, lexer, formatter: Formatter[_T], outfile: None = None) -> _T: ...
