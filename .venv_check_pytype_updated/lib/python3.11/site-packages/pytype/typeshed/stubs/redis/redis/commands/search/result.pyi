from typing import Any

class Result:
    total: Any
    duration: Any
    docs: Any
    def __init__(self, res, hascontent, duration: int = 0, has_payload: bool = False, with_scores: bool = False) -> None: ...
