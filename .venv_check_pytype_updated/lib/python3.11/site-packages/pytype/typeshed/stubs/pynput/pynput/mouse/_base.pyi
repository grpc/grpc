import enum
import sys
from collections.abc import Callable
from types import TracebackType
from typing import Any
from typing_extensions import Self

from pynput._util import AbstractListener

class Button(enum.Enum):
    unknown: int
    left: int
    middle: int
    right: int
    if sys.platform == "linux":
        button8: int
        button9: int
        button10: int
        button11: int
        button12: int
        button13: int
        button14: int
        button15: int
        button16: int
        button17: int
        button18: int
        button19: int
        button20: int
        button21: int
        button22: int
        button23: int
        button24: int
        button25: int
        button26: int
        button27: int
        button28: int
        button29: int
        button30: int
        scroll_down: int
        scroll_left: int
        scroll_right: int
        scroll_up: int
    if sys.platform == "win32":
        x1: int
        x2: int

class Controller:
    def __init__(self) -> None: ...
    @property
    def position(self) -> tuple[int, int]: ...
    @position.setter
    def position(self, position: tuple[int, int]) -> None: ...
    def scroll(self, dx: int, dy: int) -> None: ...
    def press(self, button: Button) -> None: ...
    def release(self, button: Button) -> None: ...
    def move(self, dx: int, dy: int) -> None: ...
    def click(self, button: Button, count: int = 1) -> None: ...
    def __enter__(self) -> Self: ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_val: BaseException | None, exc_tb: TracebackType | None
    ) -> None: ...

class Listener(AbstractListener):
    if sys.platform == "win32":
        WM_LBUTTONDOWN: int
        WM_LBUTTONUP: int
        WM_MBUTTONDOWN: int
        WM_MBUTTONUP: int
        WM_MOUSEMOVE: int
        WM_MOUSEWHEEL: int
        WM_MOUSEHWHEEL: int
        WM_RBUTTONDOWN: int
        WM_RBUTTONUP: int
        WM_XBUTTONDOWN: int
        WM_XBUTTONUP: int

        MK_XBUTTON1: int
        MK_XBUTTON2: int

        XBUTTON1: int
        XBUTTON2: int

        CLICK_BUTTONS: dict[int, tuple[Button, bool]]
        X_BUTTONS: dict[int, dict[int, tuple[Button, bool]]]
        SCROLL_BUTTONS: dict[int, tuple[int, int]]

    def __init__(
        self,
        on_move: Callable[[int, int], bool | None] | None = None,
        on_click: Callable[[int, int, Button, bool], bool | None] | None = None,
        on_scroll: Callable[[int, int, int, int], bool | None] | None = None,
        suppress: bool = False,
        **kwargs: Any,
    ) -> None: ...
