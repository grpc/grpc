import sys
from _typeshed import Incomplete, Unused

from PIL.BmpImagePlugin import DibImageFile
from PIL.PngImagePlugin import PngImageFile

from .Image import Image, _Box

# include_layered_windows and all_screens are Windows only
# xdisplay must be None on non-linux platforms7
if sys.platform == "linux":
    def grab(
        bbox: _Box | None = None,
        include_layered_windows: Unused = False,
        all_screens: Unused = False,
        xdisplay: Incomplete | None = None,
    ) -> Image: ...

elif sys.platform == "win32":
    def grab(
        bbox: _Box | None = None, include_layered_windows: bool = False, all_screens: bool = False, xdisplay: None = None
    ) -> Image: ...

else:
    def grab(
        bbox: _Box | None = None, include_layered_windows: Unused = False, all_screens: Unused = False, xdisplay: None = None
    ) -> Image: ...

if sys.platform == "darwin":
    def grabclipboard() -> Image | None: ...

elif sys.platform == "win32":
    def grabclipboard() -> list[str] | PngImageFile | DibImageFile | None: ...

else:
    def grabclipboard() -> Image: ...
