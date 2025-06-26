from typing import Any

log: Any

class Throwable:
    id: Any
    message: Any
    type: Any
    remote: Any
    stack: Any
    def __init__(self, exception, stack, remote: bool = False) -> None: ...
    def to_dict(self): ...
