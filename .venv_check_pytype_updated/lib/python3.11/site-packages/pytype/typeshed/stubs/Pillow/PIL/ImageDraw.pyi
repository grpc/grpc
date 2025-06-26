from _typeshed import Incomplete
from collections.abc import Container, Sequence
from typing import Any, Literal, overload
from typing_extensions import TypeAlias

from .Image import Image
from .ImageColor import _Ink
from .ImageFont import _Font

_XY: TypeAlias = Sequence[float | tuple[float, float]]
_Outline: TypeAlias = Any

class ImageDraw:
    font: Incomplete
    palette: Incomplete
    im: Incomplete
    draw: Incomplete
    mode: Incomplete
    ink: Incomplete
    fontmode: str
    fill: bool
    def __init__(self, im: Image, mode: str | None = None) -> None: ...
    def getfont(self): ...
    def arc(self, xy: _XY, start: float, end: float, fill: _Ink | None = None, width: float = 1) -> None: ...
    def bitmap(self, xy: _XY, bitmap: Image, fill: _Ink | None = None) -> None: ...
    def chord(
        self, xy: _XY, start: float, end: float, fill: _Ink | None = None, outline: _Ink | None = None, width: float = 1
    ) -> None: ...
    def ellipse(self, xy: _XY, fill: _Ink | None = None, outline: _Ink | None = None, width: float = 1) -> None: ...
    def line(self, xy: _XY, fill: _Ink | None = None, width: float = 0, joint: Literal["curve"] | None = None) -> None: ...
    def shape(self, shape: _Outline, fill: _Ink | None = None, outline: _Ink | None = None) -> None: ...
    def pieslice(
        self,
        xy: tuple[tuple[float, float], tuple[float, float]],
        start: float,
        end: float,
        fill: _Ink | None = None,
        outline: _Ink | None = None,
        width: float = 1,
    ) -> None: ...
    def point(self, xy: _XY, fill: _Ink | None = None) -> None: ...
    def polygon(self, xy: _XY, fill: _Ink | None = None, outline: _Ink | None = None, width: float = 1) -> None: ...
    def regular_polygon(
        self,
        bounding_circle: tuple[float, float] | tuple[float, float, float] | list[int],
        n_sides: int,
        rotation: float = 0,
        fill: _Ink | None = None,
        outline: _Ink | None = None,
        width: float = 1,
    ) -> None: ...
    def rectangle(
        self,
        xy: tuple[float, float, float, float] | tuple[tuple[float, float], tuple[float, float]],
        fill: _Ink | None = None,
        outline: _Ink | None = None,
        width: float = 1,
    ) -> None: ...
    def rounded_rectangle(
        self,
        xy: tuple[float, float, float, float] | tuple[tuple[float, float], tuple[float, float]],
        radius: float = 0,
        fill: _Ink | None = None,
        outline: _Ink | None = None,
        width: float = 1,
        *,
        corners: tuple[bool, bool, bool, bool] | None = None,
    ) -> None: ...
    def text(
        self,
        xy: tuple[float, float],
        text: str | bytes,
        fill: _Ink | None = None,
        font: _Font | None = None,
        anchor: str | None = None,
        spacing: float = 4,
        align: Literal["left", "center", "right"] = "left",
        direction: Literal["rtl", "ltr", "ttb"] | None = None,
        features: Sequence[str] | None = None,
        language: str | None = None,
        stroke_width: int = 0,
        stroke_fill: _Ink | None = None,
        embedded_color: bool = False,
        *args,
        **kwargs,
    ) -> None: ...
    def multiline_text(
        self,
        xy: tuple[float, float],
        text: str | bytes,
        fill: _Ink | None = None,
        font: _Font | None = None,
        anchor: str | None = None,
        spacing: float = 4,
        align: Literal["left", "center", "right"] = "left",
        direction: Literal["rtl", "ltr", "ttb"] | None = None,
        features: Incomplete | None = None,
        language: str | None = None,
        stroke_width: int = 0,
        stroke_fill: _Ink | None = None,
        embedded_color: bool = False,
        *,
        font_size: int | None = None,
    ) -> None: ...
    def textlength(
        self,
        text: str | bytes,
        font: _Font | None = None,
        direction: Literal["rtl", "ltr", "ttb"] | None = None,
        features: Sequence[str] | None = None,
        language: str | None = None,
        embedded_color: bool = False,
        *,
        font_size: int | None = None,
    ) -> float: ...
    def textbbox(
        self,
        xy: tuple[float, float],
        text: str | bytes,
        font: _Font | None = None,
        anchor: str | None = None,
        spacing: float = 4,
        align: Literal["left", "center", "right"] = "left",
        direction: Literal["rtl", "ltr", "ttb"] | None = None,
        features: Incomplete | None = None,
        language: str | None = None,
        stroke_width: int = 0,
        embedded_color: bool = False,
        *,
        font_size: int | None = None,
    ) -> tuple[int, int, int, int]: ...
    def multiline_textbbox(
        self,
        xy: tuple[float, float],
        text: str | bytes,
        font: _Font | None = None,
        anchor: str | None = None,
        spacing: float = 4,
        align: Literal["left", "center", "right"] = "left",
        direction: Literal["rtl", "ltr", "ttb"] | None = None,
        features: Incomplete | None = None,
        language: str | None = None,
        stroke_width: int = 0,
        embedded_color: bool = False,
        *,
        font_size: int | None = None,
    ) -> tuple[int, int, int, int]: ...

def Draw(im: Image, mode: str | None = None) -> ImageDraw: ...
def Outline() -> _Outline: ...
@overload
def getdraw(im: None = None, hints: Container[Literal["nicest"]] | None = None) -> tuple[None, Any]: ...
@overload
def getdraw(im: Image, hints: Container[Literal["nicest"]] | None = None) -> tuple[Image, Any]: ...
def floodfill(image: Image, xy: tuple[float, float], value, border=None, thresh: float = 0) -> None: ...
