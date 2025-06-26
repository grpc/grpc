import abc
from _typeshed import Incomplete

from qrcode.image.styledpil import StyledPilImage
from qrcode.image.styles.moduledrawers.base import QRModuleDrawer
from qrcode.main import ActiveWithNeighbors

ANTIALIASING_FACTOR: int

class StyledPilQRModuleDrawer(QRModuleDrawer, metaclass=abc.ABCMeta):
    img: StyledPilImage

class SquareModuleDrawer(StyledPilQRModuleDrawer):
    imgDraw: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def drawrect(self, box, is_active: bool): ...

class GappedSquareModuleDrawer(StyledPilQRModuleDrawer):
    size_ratio: Incomplete
    def __init__(self, size_ratio: float = 0.8) -> None: ...
    imgDraw: Incomplete
    delta: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def drawrect(self, box, is_active: bool): ...

class CircleModuleDrawer(StyledPilQRModuleDrawer):
    circle: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    def drawrect(self, box, is_active: bool): ...

class RoundedModuleDrawer(StyledPilQRModuleDrawer):
    needs_neighbors: bool
    radius_ratio: Incomplete
    def __init__(self, radius_ratio: int = 1) -> None: ...
    corner_width: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    SQUARE: Incomplete
    NW_ROUND: Incomplete
    SW_ROUND: Incomplete
    SE_ROUND: Incomplete
    NE_ROUND: Incomplete
    def setup_corners(self) -> None: ...
    def drawrect(self, box: list[list[int]], is_active: ActiveWithNeighbors): ...

class VerticalBarsDrawer(StyledPilQRModuleDrawer):
    needs_neighbors: bool
    horizontal_shrink: Incomplete
    def __init__(self, horizontal_shrink: float = 0.8) -> None: ...
    half_height: Incomplete
    delta: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    SQUARE: Incomplete
    ROUND_TOP: Incomplete
    ROUND_BOTTOM: Incomplete
    def setup_edges(self) -> None: ...
    def drawrect(self, box, is_active: ActiveWithNeighbors): ...

class HorizontalBarsDrawer(StyledPilQRModuleDrawer):
    needs_neighbors: bool
    vertical_shrink: Incomplete
    def __init__(self, vertical_shrink: float = 0.8) -> None: ...
    half_width: Incomplete
    delta: Incomplete
    def initialize(self, *args, **kwargs) -> None: ...
    SQUARE: Incomplete
    ROUND_LEFT: Incomplete
    ROUND_RIGHT: Incomplete
    def setup_edges(self) -> None: ...
    def drawrect(self, box, is_active: ActiveWithNeighbors): ...
