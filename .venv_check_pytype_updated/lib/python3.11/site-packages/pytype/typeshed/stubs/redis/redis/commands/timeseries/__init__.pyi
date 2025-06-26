from _typeshed import Incomplete
from typing import Any

from ...client import Pipeline as ClientPipeline
from .commands import TimeSeriesCommands

class TimeSeries(TimeSeriesCommands):
    MODULE_CALLBACKS: dict[str, Any]
    client: Any
    execute_command: Any
    def __init__(self, client: Incomplete | None = None, **kwargs) -> None: ...
    def pipeline(self, transaction: bool = True, shard_hint: Incomplete | None = None) -> Pipeline: ...

class Pipeline(TimeSeriesCommands, ClientPipeline[Incomplete]): ...  # type: ignore[misc]
