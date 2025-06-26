from .Image import Image

class _Enhance:
    def enhance(self, factor: float) -> Image: ...

class Color(_Enhance):
    image: Image
    intermediate_mode: str
    degenerate: Image
    def __init__(self, image: Image) -> None: ...

class Contrast(_Enhance):
    image: Image
    degenerate: Image
    def __init__(self, image: Image) -> None: ...

class Brightness(_Enhance):
    image: Image
    degenerate: Image
    def __init__(self, image: Image) -> None: ...

class Sharpness(_Enhance):
    image: Image
    degenerate: Image
    def __init__(self, image: Image) -> None: ...
