from _typeshed import Incomplete
from types import TracebackType
from typing import Any

GLYPHS: Any
MINIMUM_INTERVAL: float

class Spinner:
    interactive: Any
    interval: Any
    label: Any
    states: Any
    stream: Any
    timer: Any
    total: Any
    counter: int
    last_update: int
    def __init__(self, **options) -> None: ...
    def step(self, progress: int = 0, label: Incomplete | None = None) -> None: ...
    def sleep(self) -> None: ...
    def clear(self) -> None: ...
    def __enter__(self): ...
    def __exit__(
        self,
        exc_type: type[BaseException] | None = None,
        exc_value: BaseException | None = None,
        traceback: TracebackType | None = None,
    ) -> None: ...

class AutomaticSpinner:
    label: Any
    show_time: Any
    shutdown_event: Any
    subprocess: Any
    def __init__(self, label, show_time: bool = True) -> None: ...
    def __enter__(self) -> None: ...
    def __exit__(
        self,
        exc_type: type[BaseException] | None = None,
        exc_value: BaseException | None = None,
        traceback: TracebackType | None = None,
    ) -> None: ...
