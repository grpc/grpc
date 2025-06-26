from collections.abc import Iterator, Mapping, Set as AbstractSet
from typing import TypedDict

from pygments.token import _TokenType

ansicolors: AbstractSet[str]  # not intended to be mutable

class _StyleDict(TypedDict):
    color: str | None
    bold: bool
    italic: bool
    underline: bool
    bgcolor: str | None
    border: str | None
    roman: bool | None  # lol yes, can be True or False or None
    sans: bool | None
    mono: bool | None
    ansicolor: str | None
    bgansicolor: str | None

class StyleMeta(type):
    def __new__(cls, name, bases, dct): ...
    def style_for_token(cls, token: _TokenType) -> _StyleDict: ...
    def styles_token(cls, ttype: _TokenType) -> bool: ...
    def list_styles(cls) -> list[tuple[_TokenType, _StyleDict]]: ...
    def __iter__(cls) -> Iterator[tuple[_TokenType, _StyleDict]]: ...
    def __len__(cls) -> int: ...
    # These are a bit tricky.
    # Technically should be ClassVar in class Style.
    # But then we can't use StyleMeta to denote a style class.
    # We need that because Type[Style] is not iterable, for example.
    background_color: str
    highlight_color: str
    line_number_color: str
    line_number_background_color: str
    line_number_special_color: str
    line_number_special_background_color: str
    styles: Mapping[_TokenType, str]  # not intended to be mutable
    name: str
    aliases: list[str]
    web_style_gallery_exclude: bool

class Style(metaclass=StyleMeta): ...
