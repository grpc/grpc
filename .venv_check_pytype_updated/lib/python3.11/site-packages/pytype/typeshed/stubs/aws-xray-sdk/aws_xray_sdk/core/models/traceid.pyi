from typing import Any

class TraceId:
    VERSION: str
    DELIMITER: str
    start_time: Any
    def __init__(self) -> None: ...
    def to_id(self): ...
