from collections.abc import Callable
from typing import Literal, TypedDict
from typing_extensions import TypeAlias

class _RPi_Info(TypedDict):
    P1_REVISION: int
    REVISION: str
    TYPE: str
    MANUFACTURER: str
    PROCESSOR: str
    RAM: str

VERSION: str
RPI_INFO: _RPi_Info
RPI_REVISION: int

HIGH: Literal[1]
LOW: Literal[0]

OUT: int
IN: int
HARD_PWM: int
SERIAL: int
I2C: int
SPI: int
UNKNOWN: int

BOARD: int
BCM: int

PUD_OFF: int
PUD_UP: int
PUD_DOWN: int

RISING: int
FALLING: int
BOTH: int

_EventCallback: TypeAlias = Callable[[int], object]

def setup(channel: int, dir: int, pull_up_down: int = ..., initial: int = ...) -> None: ...
def cleanup(channel: int = 0) -> None: ...
def output(channel: int, state: int | bool) -> None: ...
def input(channel: int) -> int: ...
def setmode(mode: int) -> None: ...
def getmode() -> int: ...
def add_event_detect(channel: int, edge: int, callback: _EventCallback | None, bouncetime: int = ...) -> None: ...
def remove_event_detect(channel: int) -> None: ...
def event_detected(channel: int) -> bool: ...
def add_event_callback(channel: int, callback: _EventCallback) -> None: ...
def wait_for_edge(channel: int, edge: int, bouncetime: int = ..., timeout: int = ...) -> int | None: ...
def gpio_function(channel: int) -> int: ...
def setwarnings(gpio_warnings: bool) -> None: ...

class PWM:
    def __init__(self, channel: int, frequency: float) -> None: ...
    def start(self, dutycycle: float) -> None: ...
    def ChangeDutyCycle(self, dutycycle: float) -> None: ...
    def ChangeFrequence(self, frequency: float) -> None: ...
    def stop(self) -> None: ...
