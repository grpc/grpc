from builtins import (
    ChildProcessError as ChildProcessError,
    FileExistsError as FileExistsError,
    FileNotFoundError as FileNotFoundError,
    InterruptedError as InterruptedError,
    PermissionError as PermissionError,
    ProcessLookupError as ProcessLookupError,
    range as range,
    super as super,
)
from contextlib import redirect_stderr as redirect_stderr
from functools import lru_cache as lru_cache
from shutil import get_terminal_size as get_terminal_size, which as which
from subprocess import TimeoutExpired
from typing import Literal

PY3: Literal[True]
long = int
xrange = range
unicode = str
basestring = str

def u(s): ...
def b(s): ...

SubprocessTimeoutExpired = TimeoutExpired
