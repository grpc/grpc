from _typeshed import (
    FileDescriptorOrPath,
    Incomplete,
    OpenBinaryMode,
    OpenBinaryModeReading,
    OpenBinaryModeUpdating,
    OpenBinaryModeWriting,
    OpenTextMode,
)
from asyncio import AbstractEventLoop
from collections.abc import Callable
from typing import Literal, overload
from typing_extensions import TypeAlias

from ..base import AiofilesContextManager
from .binary import AsyncBufferedIOBase, AsyncBufferedReader, AsyncFileIO, AsyncIndirectBufferedIOBase, _UnknownAsyncBinaryIO
from .text import AsyncTextIndirectIOWrapper, AsyncTextIOWrapper

_Opener: TypeAlias = Callable[[str, int], int]

# Text mode: always returns AsyncTextIOWrapper
@overload
def open(
    file: FileDescriptorOrPath,
    mode: OpenTextMode = "r",
    buffering: int = -1,
    encoding: str | None = None,
    errors: str | None = None,
    newline: str | None = None,
    closefd: bool = True,
    opener: _Opener | None = None,
    *,
    loop: AbstractEventLoop | None = None,
    executor: Incomplete | None = None,
) -> AiofilesContextManager[None, None, AsyncTextIOWrapper]: ...

# Unbuffered binary: returns a FileIO
@overload
def open(
    file: FileDescriptorOrPath,
    mode: OpenBinaryMode,
    buffering: Literal[0],
    encoding: None = None,
    errors: None = None,
    newline: None = None,
    closefd: bool = True,
    opener: _Opener | None = None,
    *,
    loop: AbstractEventLoop | None = None,
    executor: Incomplete | None = None,
) -> AiofilesContextManager[None, None, AsyncFileIO]: ...

# Buffered binary reading/updating: AsyncBufferedReader
@overload
def open(
    file: FileDescriptorOrPath,
    mode: OpenBinaryModeReading | OpenBinaryModeUpdating,
    buffering: Literal[-1, 1] = -1,
    encoding: None = None,
    errors: None = None,
    newline: None = None,
    closefd: bool = True,
    opener: _Opener | None = None,
    *,
    loop: AbstractEventLoop | None = None,
    executor: Incomplete | None = None,
) -> AiofilesContextManager[None, None, AsyncBufferedReader]: ...

# Buffered binary writing: AsyncBufferedIOBase
@overload
def open(
    file: FileDescriptorOrPath,
    mode: OpenBinaryModeWriting,
    buffering: Literal[-1, 1] = -1,
    encoding: None = None,
    errors: None = None,
    newline: None = None,
    closefd: bool = True,
    opener: _Opener | None = None,
    *,
    loop: AbstractEventLoop | None = None,
    executor: Incomplete | None = None,
) -> AiofilesContextManager[None, None, AsyncBufferedIOBase]: ...

# Buffering cannot be determined: fall back to _UnknownAsyncBinaryIO
@overload
def open(
    file: FileDescriptorOrPath,
    mode: OpenBinaryMode,
    buffering: int = -1,
    encoding: None = None,
    errors: None = None,
    newline: None = None,
    closefd: bool = True,
    opener: _Opener | None = None,
    *,
    loop: AbstractEventLoop | None = None,
    executor: Incomplete | None = None,
) -> AiofilesContextManager[None, None, _UnknownAsyncBinaryIO]: ...

stdin: AsyncTextIndirectIOWrapper
stdout: AsyncTextIndirectIOWrapper
stderr: AsyncTextIndirectIOWrapper
stdin_bytes: AsyncIndirectBufferedIOBase
stdout_bytes: AsyncIndirectBufferedIOBase
stderr_bytes: AsyncIndirectBufferedIOBase
