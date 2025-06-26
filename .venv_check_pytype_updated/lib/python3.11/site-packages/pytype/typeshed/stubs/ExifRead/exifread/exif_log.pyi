import logging
from typing import TextIO

TEXT_NORMAL: int
TEXT_BOLD: int
TEXT_RED: int
TEXT_GREEN: int
TEXT_YELLOW: int
TEXT_BLUE: int
TEXT_MAGENTA: int
TEXT_CYAN: int

def get_logger() -> logging.Logger: ...
def setup_logger(debug: bool, color: bool) -> None: ...

class Formatter(logging.Formatter):
    color: bool
    debug: bool
    def __init__(self, debug: bool = False, color: bool = False) -> None: ...

class Handler(logging.StreamHandler[TextIO]):
    color: bool
    debug: bool
    def __init__(self, log_level: logging._Level, debug: bool = False, color: bool = False) -> None: ...
