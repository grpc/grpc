import sys
from _typeshed import BytesPath, FileDescriptorOrPath, GenericPath, ReadableBuffer, StrOrBytesPath, StrPath
from asyncio.events import AbstractEventLoop
from collections.abc import Sequence
from os import _ScandirIterator, stat_result
from typing import Any, AnyStr, overload

from aiofiles import ospath
from aiofiles.ospath import wrap as wrap

__all__ = [
    "path",
    "stat",
    "rename",
    "renames",
    "replace",
    "remove",
    "unlink",
    "mkdir",
    "makedirs",
    "rmdir",
    "removedirs",
    "link",
    "symlink",
    "readlink",
    "listdir",
    "scandir",
    "access",
    "wrap",
]

if sys.platform != "win32":
    __all__ += ["statvfs", "sendfile"]

path = ospath

async def stat(
    path: FileDescriptorOrPath,
    *,
    dir_fd: int | None = None,
    follow_symlinks: bool = True,
    loop: AbstractEventLoop | None = ...,
    executor: Any = ...,
) -> stat_result: ...
async def rename(
    src: StrOrBytesPath,
    dst: StrOrBytesPath,
    *,
    src_dir_fd: int | None = None,
    dst_dir_fd: int | None = None,
    loop: AbstractEventLoop | None = ...,
    executor: Any = ...,
) -> None: ...
async def renames(
    old: StrOrBytesPath, new: StrOrBytesPath, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def replace(
    src: StrOrBytesPath,
    dst: StrOrBytesPath,
    *,
    src_dir_fd: int | None = None,
    dst_dir_fd: int | None = None,
    loop: AbstractEventLoop | None = ...,
    executor: Any = ...,
) -> None: ...
async def remove(
    path: StrOrBytesPath, *, dir_fd: int | None = None, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def unlink(
    path: StrOrBytesPath, *, dir_fd: int | None = ..., loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def mkdir(
    path: StrOrBytesPath, mode: int = 511, *, dir_fd: int | None = None, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def makedirs(
    name: StrOrBytesPath, mode: int = 511, exist_ok: bool = False, *, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def link(
    src: StrOrBytesPath,
    dst: StrOrBytesPath,
    *,
    src_dir_fd: int | None = ...,
    dst_dir_fd: int | None = ...,
    follow_symlinks: bool = ...,
    loop: AbstractEventLoop | None = ...,
    executor: Any = ...,
) -> None: ...
async def symlink(
    src: StrOrBytesPath,
    dst: StrOrBytesPath,
    target_is_directory: bool = ...,
    *,
    dir_fd: int | None = ...,
    loop: AbstractEventLoop | None = ...,
    executor: Any = ...,
) -> None: ...
async def readlink(
    path: AnyStr, *, dir_fd: int | None = ..., loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> AnyStr: ...
async def rmdir(
    path: StrOrBytesPath, *, dir_fd: int | None = None, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> None: ...
async def removedirs(name: StrOrBytesPath, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> None: ...
@overload
async def scandir(path: None = None, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> _ScandirIterator[str]: ...
@overload
async def scandir(path: int, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> _ScandirIterator[str]: ...
@overload
async def scandir(
    path: GenericPath[AnyStr], *, loop: AbstractEventLoop | None = ..., executor: Any = ...
) -> _ScandirIterator[AnyStr]: ...
@overload
async def listdir(path: StrPath | None, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> list[str]: ...
@overload
async def listdir(path: BytesPath, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> list[bytes]: ...
@overload
async def listdir(path: int, *, loop: AbstractEventLoop | None = ..., executor: Any = ...) -> list[str]: ...
async def access(
    path: FileDescriptorOrPath, mode: int, *, dir_fd: int | None = None, effective_ids: bool = False, follow_symlinks: bool = True
) -> bool: ...

if sys.platform != "win32":
    from os import statvfs_result

    @overload
    async def sendfile(
        out_fd: int, in_fd: int, offset: int | None, count: int, *, loop: AbstractEventLoop | None = ..., executor: Any = ...
    ) -> int: ...
    @overload
    async def sendfile(
        out_fd: int,
        in_fd: int,
        offset: int,
        count: int,
        headers: Sequence[ReadableBuffer] = ...,
        trailers: Sequence[ReadableBuffer] = ...,
        flags: int = ...,
        *,
        loop: AbstractEventLoop | None = ...,
        executor: Any = ...,
    ) -> int: ...  # FreeBSD and Mac OS X only
    async def statvfs(path: FileDescriptorOrPath) -> statvfs_result: ...  # Unix only
