from _typeshed import Incomplete

import qrcode.image.base

class PilImage(qrcode.image.base.BaseImage):
    kind: str
    fill_color: Incomplete
    def new_image(self, **kwargs): ...
    def drawrect(self, row, col) -> None: ...
    def save(self, stream, format: Incomplete | None = None, **kwargs) -> None: ...  # type: ignore[override]
    def __getattr__(self, name): ...
