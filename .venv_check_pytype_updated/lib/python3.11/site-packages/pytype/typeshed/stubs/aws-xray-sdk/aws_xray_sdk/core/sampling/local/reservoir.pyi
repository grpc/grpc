from typing import Any

class Reservoir:
    traces_per_sec: Any
    used_this_sec: int
    this_sec: Any
    def __init__(self, traces_per_sec: int = 0) -> None: ...
    def take(self): ...
