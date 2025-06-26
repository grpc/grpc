from typing import Any

from . import base

class Filter(base.Filter):
    encoding: Any
    def __init__(self, source, encoding) -> None: ...
    def __iter__(self): ...
