import contextlib
import enum
import sys
from collections.abc import Callable, Iterable, Iterator
from typing import Any, ClassVar
from typing_extensions import Self

from pynput._util import AbstractListener

class KeyCode:
    _PLATFORM_EXTENSIONS: ClassVar[Iterable[str]]  # undocumented
    vk: int | None
    char: str | None
    is_dead: bool | None
    combining: str | None
    def __init__(self, vk: str | None = None, char: str | None = None, is_dead: bool = False, **kwargs: str) -> None: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def join(self, key: Self) -> Self: ...
    @classmethod
    def from_vk(cls, vk: int, **kwargs: Any) -> Self: ...
    @classmethod
    def from_char(cls, char: str, **kwargs: Any) -> Self: ...
    @classmethod
    def from_dead(cls, char: str, **kwargs: Any) -> Self: ...

class Key(enum.Enum):
    alt: int
    alt_l: int
    alt_r: int
    alt_gr: int
    backspace: int
    caps_lock: int
    cmd: int
    cmd_l: int
    cmd_r: int
    ctrl: int
    ctrl_l: int
    ctrl_r: int
    delete: int
    down: int
    end: int
    enter: int
    esc: int
    f1: int
    f2: int
    f3: int
    f4: int
    f5: int
    f6: int
    f7: int
    f8: int
    f9: int
    f10: int
    f11: int
    f12: int
    f13: int
    f14: int
    f15: int
    f16: int
    f17: int
    f18: int
    f19: int
    f20: int
    if sys.platform == "win32":
        f21: int
        f22: int
        f23: int
        f24: int
    home: int
    left: int
    page_down: int
    page_up: int
    right: int
    shift: int
    shift_l: int
    shift_r: int
    space: int
    tab: int
    up: int
    media_play_pause: int
    media_volume_mute: int
    media_volume_down: int
    media_volume_up: int
    media_previous: int
    media_next: int
    insert: int
    menu: int
    num_lock: int
    pause: int
    print_screen: int
    scroll_lock: int

class Controller:
    _KeyCode: ClassVar[type[KeyCode]]  # undocumented
    _Key: ClassVar[type[Key]]  # undocumented

    if sys.platform == "linux":
        CTRL_MASK: ClassVar[int]
        SHIFT_MASK: ClassVar[int]

    class InvalidKeyException(Exception): ...
    class InvalidCharacterException(Exception): ...

    def __init__(self) -> None: ...
    def press(self, key: str | Key | KeyCode) -> None: ...
    def release(self, key: str | Key | KeyCode) -> None: ...
    def tap(self, key: str | Key | KeyCode) -> None: ...
    def touch(self, key: str | Key | KeyCode, is_press: bool) -> None: ...
    @contextlib.contextmanager
    def pressed(self, *args: str | Key | KeyCode) -> Iterator[None]: ...
    def type(self, string: str) -> None: ...
    @property
    def modifiers(self) -> contextlib.AbstractContextManager[Iterator[set[Key]]]: ...
    @property
    def alt_pressed(self) -> bool: ...
    @property
    def alt_gr_pressed(self) -> bool: ...
    @property
    def ctrl_pressed(self) -> bool: ...
    @property
    def shift_pressed(self) -> bool: ...

class Listener(AbstractListener):
    def __init__(
        self,
        on_press: Callable[[Key | KeyCode | None], None] | None = None,
        on_release: Callable[[Key | KeyCode | None], None] | None = None,
        suppress: bool = False,
        **kwargs: Any,
    ) -> None: ...
    def canonical(self, key: Key | KeyCode) -> Key | KeyCode: ...
