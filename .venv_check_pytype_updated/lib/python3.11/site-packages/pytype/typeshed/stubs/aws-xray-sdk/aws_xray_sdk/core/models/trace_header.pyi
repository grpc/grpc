from typing import Any
from typing_extensions import Self

log: Any
ROOT: str
PARENT: str
SAMPLE: str
SELF: str
HEADER_DELIMITER: str

class TraceHeader:
    def __init__(
        self, root: str | None = None, parent: str | None = None, sampled: bool | None = None, data: dict[str, Any] | None = None
    ) -> None: ...
    @classmethod
    def from_header_str(cls, header) -> Self: ...
    def to_header_str(self): ...
    @property
    def root(self): ...
    @property
    def parent(self): ...
    @property
    def sampled(self): ...
    @property
    def data(self): ...
