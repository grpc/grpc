import abc
from _typeshed import Incomplete

from qrcode.image.base import BaseImage

class QRModuleDrawer(abc.ABC, metaclass=abc.ABCMeta):
    needs_neighbors: bool
    def __init__(self, **kwargs) -> None: ...
    img: Incomplete
    def initialize(self, img: BaseImage) -> None: ...
    @abc.abstractmethod
    def drawrect(self, box, is_active) -> None: ...
