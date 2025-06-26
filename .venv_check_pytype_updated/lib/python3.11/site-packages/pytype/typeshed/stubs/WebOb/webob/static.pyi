from _typeshed import StrOrBytesPath
from collections.abc import Iterator
from typing import IO, Any

from webob.dec import wsgify
from webob.request import Request
from webob.response import Response

BLOCK_SIZE: int

class FileApp:
    filename: StrOrBytesPath
    kw: dict[str, Any]
    def __init__(self, filename: StrOrBytesPath, **kw: Any) -> None: ...
    @wsgify
    def __call__(self, req: Request) -> Response: ...

class FileIter:
    file: IO[bytes]
    def __init__(self, file: IO[bytes]) -> None: ...
    def app_iter_range(
        self, seek: int | None = None, limit: int | None = None, block_size: int | None = None
    ) -> Iterator[bytes]: ...
    __iter__ = app_iter_range

class DirectoryApp:
    path: str | bytes
    index_page: str | None
    hide_index_with_redirect: bool
    fileapp_kw: dict[str, Any]
    def __init__(
        self, path: StrOrBytesPath, index_page: str = "index.html", hide_index_with_redirect: bool = False, **kw: Any
    ) -> None: ...
    def make_fileapp(self, path: StrOrBytesPath) -> FileApp: ...
    @wsgify
    def __call__(self, req: Request) -> Response | FileApp: ...
    def index(self, req: Request, path: StrOrBytesPath) -> Response | FileApp: ...
