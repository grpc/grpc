import dataclasses
from _typeshed import Incomplete
from collections.abc import Generator
from dataclasses import dataclass
from typing import overload
from typing_extensions import Self

from .drawing import DeviceGray, DeviceRGB, Number
from .enums import TextEmphasis
from .syntax import PDFObject

# Only defined if harfbuzz is installed.
class HarfBuzzFont(Incomplete):  # derives from uharfbuzz.Font
    def __deepcopy__(self, _memo: object) -> Self: ...

@dataclass
class FontFace:
    family: str | None
    emphasis: TextEmphasis | None
    size_pt: int | None
    color: DeviceGray | DeviceRGB | None
    fill_color: DeviceGray | DeviceRGB | None

    def __init__(
        self,
        family: str | None = None,
        emphasis: Incomplete | None = None,
        size_pt: int | None = None,
        color: int | tuple[Number, Number, Number] | DeviceGray | DeviceRGB | None = None,
        fill_color: int | tuple[Number, Number, Number] | DeviceGray | DeviceRGB | None = None,
    ) -> None: ...

    replace = dataclasses.replace

    @overload
    @staticmethod
    def combine(default_style: None, override_style: None) -> None: ...  # type: ignore[misc]
    @overload
    @staticmethod
    def combine(default_style: FontFace | None, override_style: FontFace | None) -> FontFace: ...

class _FontMixin:
    i: int
    type: str
    name: str
    up: int
    ut: int
    cw: int
    fontkey: str
    emphasis: TextEmphasis
    def encode_text(self, text: str): ...

class CoreFont(_FontMixin):
    def __init__(self, fpdf, fontkey: str, style: int) -> None: ...
    def get_text_width(self, text: str, font_size_pt: int, _): ...

class TTFFont(_FontMixin):
    ttffile: Incomplete
    ttfont: Incomplete
    scale: Incomplete
    desc: Incomplete
    cmap: Incomplete
    glyph_ids: Incomplete
    missing_glyphs: Incomplete
    subset: Incomplete
    hbfont: HarfBuzzFont | None  # Not always defined.
    def __init__(self, fpdf, font_file_path, fontkey: str, style: int) -> None: ...
    def close(self) -> None: ...
    def get_text_width(self, text: str, font_size_pt: int, text_shaping_parms): ...
    def shaped_text_width(self, text: str, font_size_pt: int, text_shaping_parms): ...
    def perform_harfbuzz_shaping(self, text: str, font_size_pt: int, text_shaping_parms): ...
    def shape_text(self, text: str, font_size_pt: int, text_shaping_parms): ...

class PDFFontDescriptor(PDFObject):
    type: Incomplete
    ascent: Incomplete
    descent: Incomplete
    cap_height: Incomplete
    flags: Incomplete
    font_b_box: Incomplete
    italic_angle: Incomplete
    stem_v: Incomplete
    missing_width: Incomplete
    font_name: Incomplete
    def __init__(self, ascent, descent, cap_height, flags, font_b_box, italic_angle, stem_v, missing_width) -> None: ...

class Glyph:
    glyph_id: int
    unicode: tuple[Incomplete, ...]
    glyph_name: str
    glyph_width: int
    def __hash__(self): ...
    def __init__(self, glyph_id, unicode, glyph_name, glyph_width) -> None: ...
    def __lt__(self, other): ...
    def __gt__(self, other): ...
    def __le__(self, other): ...
    def __ge__(self, other): ...

    __match_args__ = ("glyph_id", "unicode", "glyph_name", "glyph_width")

class SubsetMap:
    font: TTFFont
    def __init__(self, font: TTFFont, identities: list[int]) -> None: ...
    def __len__(self) -> int: ...
    def items(self) -> Generator[Incomplete, None, None]: ...
    def pick(self, unicode: int): ...
    def pick_glyph(self, glyph): ...
    def get_glyph(
        self,
        glyph: Incomplete | None = None,
        unicode: Incomplete | None = None,
        glyph_name: Incomplete | None = None,
        glyph_width: Incomplete | None = None,
    ) -> Glyph: ...
    def get_all_glyph_names(self): ...

CORE_FONTS: dict[str, str]
COURIER_FONT: dict[str, int]
CORE_FONTS_CHARWIDTHS: dict[str, dict[str, int]]
