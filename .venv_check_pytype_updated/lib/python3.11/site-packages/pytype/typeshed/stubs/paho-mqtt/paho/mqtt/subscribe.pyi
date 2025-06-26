from collections.abc import Callable

from .client import Client, MQTTMessage, _UserData
from .publish import _TLS, _Auth, _Msg, _Proxy

def callback(
    callback: Callable[[Client, _UserData, MQTTMessage], None],
    topics: list[str],
    qos: int = 0,
    userdata: _UserData | None = None,
    hostname: str = "localhost",
    port: int = 1883,
    client_id: str = "",
    keepalive: int = 60,
    will: _Msg | None = None,
    auth: _Auth | None = None,
    tls: _TLS | None = None,
    protocol: int = 4,
    transport: str = "tcp",
    clean_session: bool = True,
    proxy_args: _Proxy | None = None,
) -> None: ...
def simple(
    topics: str | list[str],
    qos: int = 0,
    msg_count: int = 1,
    retained: bool = True,
    hostname: str = "localhost",
    port: int = 1883,
    client_id: str = "",
    keepalive: int = 60,
    will: _Msg | None = None,
    auth: _Auth | None = None,
    tls: _TLS | None = None,
    protocol: int = 4,
    transport: str = "tcp",
    clean_session: bool = True,
    proxy_args: _Proxy | None = None,
) -> list[MQTTMessage] | MQTTMessage: ...
