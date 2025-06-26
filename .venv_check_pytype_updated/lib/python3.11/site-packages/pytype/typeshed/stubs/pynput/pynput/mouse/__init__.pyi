from typing import Any

from pynput import _util

from ._base import Button as Button, Controller as Controller, Listener as Listener

class Events(_util.Events[Any, Listener]):
    class Move(_util.Events.Event):
        x: int
        y: int
        def __init__(self, x: int, y: int) -> None: ...

    class Click(_util.Events.Event):
        x: int
        y: int
        button: Button
        pressed: bool
        def __init__(self, x: int, y: int, button: Button, pressed: bool) -> None: ...

    class Scroll(_util.Events.Event):
        x: int
        y: int
        dx: int
        dy: int
        def __init__(self, x: int, y: int, dx: int, dy: int) -> None: ...

    def __init__(self) -> None: ...
    def __next__(self) -> Move | Click | Scroll: ...
    def get(self, timeout: float | None = None) -> Move | Click | Scroll | None: ...
