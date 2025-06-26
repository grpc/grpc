from typing import Any, Generic, TypeVar, overload

_T = TypeVar("_T", str, bytes)

class Formatter(Generic[_T]):
    name: Any
    aliases: Any
    filenames: Any
    unicodeoutput: bool
    style: Any
    full: Any
    title: Any
    encoding: Any
    options: Any
    @overload
    def __init__(self: Formatter[str], *, encoding: None = None, outencoding: None = None, **options) -> None: ...
    @overload
    def __init__(self: Formatter[bytes], *, encoding: str, outencoding: None = None, **options) -> None: ...
    @overload
    def __init__(self: Formatter[bytes], *, encoding: None = None, outencoding: str, **options) -> None: ...
    def get_style_defs(self, arg: str = ""): ...
    def format(self, tokensource, outfile): ...
