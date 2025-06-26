import abc
from _typeshed import Incomplete
from decimal import Decimal
from typing import NamedTuple

from qrcode.image.styles.moduledrawers.base import QRModuleDrawer
from qrcode.image.svg import SvgFragmentImage, SvgPathImage

ANTIALIASING_FACTOR: int

class Coords(NamedTuple):
    x0: Decimal
    y0: Decimal
    x1: Decimal
    y1: Decimal
    xh: Decimal
    yh: Decimal

class BaseSvgQRModuleDrawer(QRModuleDrawer, metaclass=abc.ABCMeta):
    img: SvgFragmentImage
    size_ratio: Incomplete
    def __init__(self, *, size_ratio: Decimal = ..., **kwargs) -> None: ...
    box_delta: Incomplete
    box_size: Incomplete
    box_half: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def coords(self, box) -> Coords: ...

class SvgQRModuleDrawer(BaseSvgQRModuleDrawer, metaclass=abc.ABCMeta):
    tag: str
    tag_qname: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def drawrect(self, box, is_active: bool): ...
    @abc.abstractmethod
    def el(self, box): ...

class SvgSquareDrawer(SvgQRModuleDrawer):
    unit_size: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def el(self, box): ...

class SvgCircleDrawer(SvgQRModuleDrawer):
    tag: str
    radius: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def el(self, box): ...

class SvgPathQRModuleDrawer(BaseSvgQRModuleDrawer, metaclass=abc.ABCMeta):
    img: SvgPathImage
    def drawrect(self, box, is_active: bool): ...
    @abc.abstractmethod
    def subpath(self, box) -> str: ...

class SvgPathSquareDrawer(SvgPathQRModuleDrawer):
    def subpath(self, box) -> str: ...

class SvgPathCircleDrawer(SvgPathQRModuleDrawer):
    def initialize(self, *args, **kwargs) -> None: ...
    def subpath(self, box) -> str: ...
