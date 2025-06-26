from typing import Any

from . import base

SPACES_REGEX: Any

class Filter(base.Filter):
    spacePreserveElements: Any
    def __iter__(self): ...

def collapse_spaces(text): ...
