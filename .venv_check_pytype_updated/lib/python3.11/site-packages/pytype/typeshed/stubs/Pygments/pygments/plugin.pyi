from collections.abc import Generator, Iterable
from typing import Any

from pkg_resources import EntryPoint
from pygments.filter import Filter
from pygments.formatter import Formatter
from pygments.lexer import Lexer
from pygments.style import Style

LEXER_ENTRY_POINT: str
FORMATTER_ENTRY_POINT: str
STYLE_ENTRY_POINT: str
FILTER_ENTRY_POINT: str

def iter_entry_points(group_name: str) -> Iterable[EntryPoint]: ...
def find_plugin_lexers() -> Generator[type[Lexer], None, None]: ...
def find_plugin_formatters() -> Generator[tuple[str, type[Formatter[Any]]], None, None]: ...
def find_plugin_styles() -> Generator[tuple[str, type[Style]], None, None]: ...
def find_plugin_filters() -> Generator[tuple[str, type[Filter]], None, None]: ...
