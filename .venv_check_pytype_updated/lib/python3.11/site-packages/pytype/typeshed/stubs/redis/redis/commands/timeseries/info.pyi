from _typeshed import Incomplete
from typing import Any

class TSInfo:
    rules: list[Any]
    labels: list[Any]
    sourceKey: Incomplete | None
    chunk_count: Incomplete | None
    memory_usage: Incomplete | None
    total_samples: Incomplete | None
    retention_msecs: Incomplete | None
    last_time_stamp: Incomplete | None
    first_time_stamp: Incomplete | None

    max_samples_per_chunk: Incomplete | None
    chunk_size: Incomplete | None
    duplicate_policy: Incomplete | None
    def __init__(self, args) -> None: ...
