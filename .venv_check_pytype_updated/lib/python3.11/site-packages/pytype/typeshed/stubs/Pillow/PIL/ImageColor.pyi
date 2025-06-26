from typing_extensions import TypeAlias

_RGB: TypeAlias = tuple[int, int, int] | tuple[int, int, int, int]
_Ink: TypeAlias = str | int | _RGB
_GreyScale: TypeAlias = tuple[int, int]

def getrgb(color: _Ink) -> _RGB: ...
def getcolor(color: _Ink, mode: str) -> _RGB | _GreyScale: ...

colormap: dict[str, str]
