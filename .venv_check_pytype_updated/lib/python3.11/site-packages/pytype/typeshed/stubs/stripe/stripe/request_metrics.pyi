from typing import Any

class RequestMetrics:
    request_id: Any
    request_duration_ms: Any
    def __init__(self, request_id, request_duration_ms) -> None: ...
    def payload(self): ...
