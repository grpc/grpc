import abc
from _typeshed import Incomplete

import qrcode.image.base
from qrcode.image.styles.colormasks import QRColorMask
from qrcode.image.styles.moduledrawers import SquareModuleDrawer

class StyledPilImage(qrcode.image.base.BaseImageWithDrawer, metaclass=abc.ABCMeta):
    kind: str
    needs_processing: bool
    color_mask: QRColorMask
    default_drawer_class = SquareModuleDrawer
    embeded_image: Incomplete
    embeded_image_resample: Incomplete
    paint_color: Incomplete
    def __init__(self, *args, **kwargs) -> None: ...
    def new_image(self, **kwargs): ...
    def init_new_image(self) -> None: ...
    def process(self) -> None: ...
    def draw_embeded_image(self) -> None: ...
    def save(self, stream, format: Incomplete | None = None, **kwargs) -> None: ...  # type: ignore[override]
    def __getattr__(self, name): ...
