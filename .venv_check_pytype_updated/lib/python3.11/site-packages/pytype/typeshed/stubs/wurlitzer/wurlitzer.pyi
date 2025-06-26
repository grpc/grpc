__all__ = ["STDOUT", "PIPE", "Wurlitzer", "pipes", "sys_pipes", "sys_pipes_forever", "stop_sys_pipes"]

import contextlib
import io
from _typeshed import SupportsWrite
from contextlib import _GeneratorContextManager
from types import TracebackType
from typing import Any, Final, Literal, Protocol, TextIO, TypeVar, overload
from typing_extensions import TypeAlias

STDOUT: Final = 2
PIPE: Final = 3
_STDOUT: TypeAlias = Literal[2]
_PIPE: TypeAlias = Literal[3]
_T_contra = TypeVar("_T_contra", contravariant=True)
_StreamOutT = TypeVar("_StreamOutT", bound=_Stream[str] | _Stream[bytes])
_StreamErrT = TypeVar("_StreamErrT", bound=_Stream[str] | _Stream[bytes])

class _Stream(SupportsWrite[_T_contra], Protocol):
    def seek(self, offset: int, whence: int = ..., /) -> int: ...

# Alias for IPython.core.interactiveshell.InteractiveShell.
# N.B. Even if we added ipython to the stub-uploader allowlist,
# we wouldn't be able to declare a dependency on ipython here,
# since `wurlitzer` does not declare a dependency on `ipython` at runtime
_InteractiveShell: TypeAlias = Any

class Wurlitzer:
    flush_interval: float
    encoding: str | None

    def __init__(
        self,
        stdout: SupportsWrite[str] | SupportsWrite[bytes] | None = None,
        stderr: _STDOUT | SupportsWrite[str] | SupportsWrite[bytes] | None = None,
        encoding: str | None = ...,
        bufsize: int | None = ...,
    ) -> None: ...
    def __enter__(self): ...
    def __exit__(
        self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None
    ) -> None: ...

def dup2(a: int, b: int, timeout: float = 3) -> int: ...
def sys_pipes(encoding: str = ..., bufsize: int | None = None) -> contextlib._GeneratorContextManager[tuple[TextIO, TextIO]]: ...

# stubtest does not support overloaded context managers, hence the _GeneratorContextManager[Foo] return types.
@overload
def pipes(
    stdout: _PIPE, stderr: _STDOUT, encoding: None, bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.BytesIO, None]]: ...
@overload
def pipes(
    stdout: _PIPE, stderr: _PIPE, encoding: None, bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.BytesIO, io.BytesIO]]: ...
@overload
def pipes(
    stdout: _PIPE, stderr: _StreamErrT, encoding: None, bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.BytesIO, _StreamErrT]]: ...
@overload
def pipes(
    stdout: _PIPE, stderr: _STDOUT, encoding: str = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.StringIO, None]]: ...
@overload
def pipes(
    stdout: _PIPE, stderr: _PIPE, encoding: str = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.StringIO, io.StringIO]]: ...
@overload
def pipes(
    stdout: _PIPE, stderr: _StreamErrT, encoding: str = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[io.StringIO, _StreamErrT]]: ...
@overload
def pipes(
    stdout: _StreamOutT, stderr: _STDOUT, encoding: str | None = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[_StreamOutT, None]]: ...
@overload
def pipes(
    stdout: _StreamOutT, stderr: _PIPE, encoding: None, bufsize: int | None = None
) -> _GeneratorContextManager[tuple[_StreamOutT, io.BytesIO]]: ...
@overload
def pipes(
    stdout: _StreamOutT, stderr: _PIPE, encoding: str = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[_StreamOutT, io.StringIO]]: ...
@overload
def pipes(
    stdout: _StreamOutT, stderr: _StreamErrT, encoding: str | None = ..., bufsize: int | None = None
) -> _GeneratorContextManager[tuple[_StreamOutT, _StreamErrT]]: ...
@overload
def pipes(
    stdout: _PIPE | _StreamOutT = 3,
    stderr: _STDOUT | _PIPE | _StreamErrT = 3,
    encoding: str | None = ...,
    bufsize: int | None = None,
) -> _GeneratorContextManager[tuple[io.BytesIO | io.StringIO | _StreamOutT, io.BytesIO | io.StringIO | _StreamErrT | None]]: ...
def sys_pipes_forever(encoding: str = ..., bufsize: int | None = None) -> None: ...
def stop_sys_pipes() -> None: ...
def load_ipython_extension(ip: _InteractiveShell) -> None: ...
def unload_ipython_extension(ip: _InteractiveShell) -> None: ...
