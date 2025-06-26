import datetime
import io
import logging.handlers
import subprocess
import time
from _typeshed import StrOrBytesPath
from collections.abc import Callable, Iterable, Mapping, Sequence
from contextlib import AbstractContextManager
from email.message import Message
from hashlib import _Hash
from typing import IO, Any, TypeVar
from typing_extensions import TypeAlias

import boto.connection

_KT = TypeVar("_KT")
_VT = TypeVar("_VT")

_Provider: TypeAlias = Any  # TODO replace this with boto.provider.Provider once stubs exist
_LockType: TypeAlias = Any  # TODO replace this with _thread.LockType once stubs exist

JSONDecodeError: type[ValueError]
qsa_of_interest: list[str]

def unquote_v(nv: str) -> str | tuple[str, str]: ...
def canonical_string(
    method: str, path: str, headers: Mapping[str, str | None], expires: int | None = None, provider: _Provider | None = None
) -> str: ...
def merge_meta(
    headers: Mapping[str, str], metadata: Mapping[str, str], provider: _Provider | None = None
) -> Mapping[str, str]: ...
def get_aws_metadata(headers: Mapping[str, str], provider: _Provider | None = None) -> Mapping[str, str]: ...
def retry_url(url: str, retry_on_404: bool = True, num_retries: int = 10, timeout: int | None = None) -> str: ...

class LazyLoadMetadata(dict[_KT, _VT]):
    def __init__(self, url: str, num_retries: int, timeout: int | None = None) -> None: ...

def get_instance_metadata(
    version: str = "latest",
    url: str = "http://169.254.169.254",
    data: str = "meta-data/",
    timeout: int | None = None,
    num_retries: int = 5,
) -> LazyLoadMetadata[Any, Any] | None: ...
def get_instance_identity(
    version: str = "latest", url: str = "http://169.254.169.254", timeout: int | None = None, num_retries: int = 5
) -> Mapping[str, Any] | None: ...
def get_instance_userdata(
    version: str = "latest",
    sep: str | None = None,
    url: str = "http://169.254.169.254",
    timeout: int | None = None,
    num_retries: int = 5,
) -> Mapping[str, str]: ...

ISO8601: str
ISO8601_MS: str
RFC1123: str
LOCALE_LOCK: _LockType

def setlocale(name: str | tuple[str, str]) -> AbstractContextManager[str]: ...
def get_ts(ts: time.struct_time | None = None) -> str: ...
def parse_ts(ts: str) -> datetime.datetime: ...
def find_class(module_name: str, class_name: str | None = None) -> type[Any] | None: ...
def update_dme(username: str, password: str, dme_id: str, ip_address: str) -> str: ...
def fetch_file(
    uri: str, file: IO[str] | None = None, username: str | None = None, password: str | None = None
) -> IO[str] | None: ...

class ShellCommand:
    exit_code: int
    command: subprocess._CMD
    log_fp: io.StringIO
    wait: bool
    fail_fast: bool
    def __init__(
        self, command: subprocess._CMD, wait: bool = True, fail_fast: bool = False, cwd: StrOrBytesPath | None = None
    ) -> None: ...
    process: subprocess.Popen[Any]
    def run(self, cwd: subprocess._CMD | None = None) -> int | None: ...
    def setReadOnly(self, value) -> None: ...
    def getStatus(self) -> int | None: ...
    status: int | None
    def getOutput(self) -> str: ...
    output: str

class AuthSMTPHandler(logging.handlers.SMTPHandler):
    username: str
    password: str
    def __init__(
        self, mailhost: str, username: str, password: str, fromaddr: str, toaddrs: Sequence[str], subject: str
    ) -> None: ...

class LRUCache(dict[_KT, _VT]):
    class _Item:
        previous: LRUCache._Item | None
        next: LRUCache._Item | None
        key = ...
        value = ...
        def __init__(self, key, value) -> None: ...

    _dict: dict[_KT, LRUCache._Item]
    capacity: int
    head: LRUCache._Item | None
    tail: LRUCache._Item | None
    def __init__(self, capacity: int) -> None: ...

# This exists to work around Password.str's name shadowing the str type
_Str: TypeAlias = str

class Password:
    hashfunc: Callable[[bytes], _Hash]
    str: _Str | None
    def __init__(self, str: _Str | None = None, hashfunc: Callable[[bytes], _Hash] | None = None) -> None: ...
    def set(self, value: bytes | _Str) -> None: ...
    def __eq__(self, other: _Str | bytes | None) -> bool: ...  # type: ignore[override]
    def __len__(self) -> int: ...

def notify(
    subject: str,
    body: str | None = None,
    html_body: Sequence[str] | str | None = None,
    to_string: str | None = None,
    attachments: Iterable[Message] | None = None,
    append_instance_id: bool = True,
) -> None: ...
def get_utf8_value(value: str) -> bytes: ...
def mklist(value: Any) -> list[Any]: ...
def pythonize_name(name: str) -> str: ...
def write_mime_multipart(
    content: list[tuple[str, str]], compress: bool = False, deftype: str = "text/plain", delimiter: str = ":"
) -> str: ...
def guess_mime_type(content: str, deftype: str) -> str: ...
def compute_md5(fp: IO[Any], buf_size: int = 8192, size: int | None = None) -> tuple[str, str, int]: ...
def compute_hash(
    fp: IO[Any], buf_size: int = 8192, size: int | None = None, hash_algorithm: Any = ...
) -> tuple[str, str, int]: ...
def find_matching_headers(name: str, headers: Mapping[str, str | None]) -> list[str]: ...
def merge_headers_by_name(name: str, headers: Mapping[str, str | None]) -> str: ...

class RequestHook:
    def handle_request_data(
        self, request: boto.connection.HTTPRequest, response: boto.connection.HTTPResponse, error: bool = False
    ) -> Any: ...

def host_is_ipv6(hostname: str) -> bool: ...
def parse_host(hostname: str) -> str: ...
