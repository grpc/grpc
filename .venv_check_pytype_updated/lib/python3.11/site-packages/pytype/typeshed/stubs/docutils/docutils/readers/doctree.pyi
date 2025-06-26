from typing import TypeVar

from docutils import readers

_S = TypeVar("_S")

class Reader(readers.ReReader[_S]): ...
