import ssl
from collections.abc import Iterable
from typing import TypedDict
from typing_extensions import NotRequired, TypeAlias

_Payload: TypeAlias = str | bytes | bytearray | float

class _Msg(TypedDict):
    topic: str
    payload: NotRequired[_Payload | None]
    qos: NotRequired[int]
    retain: NotRequired[int]

class _Auth(TypedDict):
    username: str
    password: NotRequired[str]

class _TLS(TypedDict):
    ca_certs: str
    certfile: NotRequired[str]
    keyfile: NotRequired[str]
    tls_version: NotRequired[ssl._SSLMethod]
    ciphers: NotRequired[str]
    insecure: NotRequired[str]
    cert_reqs: NotRequired[ssl.VerifyMode]
    keyfile_password: NotRequired[ssl._PasswordType]

class _Proxy(TypedDict):
    proxy_type: int
    proxy_addr: str
    proxy_rdns: NotRequired[bool]
    proxy_username: NotRequired[str]
    proxy_passwor: NotRequired[str]

def multiple(
    msgs: Iterable[_Msg],
    hostname: str = "localhost",
    port: int = 1883,
    client_id: str = "",
    keepalive: int = 60,
    will: _Msg | None = None,
    auth: _Auth | None = None,
    tls: _TLS | None = None,
    protocol: int = 4,
    transport: str = "tcp",
    proxy_args: _Proxy | None = None,
) -> None: ...
def single(
    topic: str,
    payload: _Payload | None = None,
    qos: int | None = 0,
    retain: bool | None = False,
    hostname: str = "localhost",
    port: int = 1883,
    client_id: str = "",
    keepalive: int = 60,
    will: _Msg | None = None,
    auth: _Auth | None = None,
    tls: _TLS | None = None,
    protocol: int = 4,
    transport: str = "tcp",
    proxy_args: _Proxy | None = None,
) -> None: ...
