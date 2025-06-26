from typing import Any

from . import base

class Filter(base.Filter):
    require_matching_tags: Any
    def __init__(self, source, require_matching_tags: bool = True) -> None: ...
    def __iter__(self): ...
