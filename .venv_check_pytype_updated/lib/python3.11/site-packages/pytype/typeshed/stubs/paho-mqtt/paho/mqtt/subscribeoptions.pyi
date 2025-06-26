from typing import Any, TypedDict

from . import MQTTException as MQTTException

class _SubscribeOptionsJson(TypedDict):
    QoS: int
    noLocal: bool
    retainAsPublished: bool
    retainHandling: int

class SubscribeOptions:
    RETAIN_SEND_ON_SUBSCRIBE: int
    RETAIN_SEND_IF_NEW_SUB: int
    RETAIN_DO_NOT_SEND: int
    names: list[str]
    Qos: int
    noLocal: bool
    retainAsPublished: bool
    retainHandling: int
    def __init__(self, qos: int = 0, noLocal: bool = False, retainAsPublished: bool = False, retainHandling: int = 0) -> None: ...
    def __setattr__(self, name: str, value: Any) -> None: ...
    def pack(self) -> bytes: ...
    def unpack(self, buffer: bytes | bytearray) -> int: ...
    def json(self) -> _SubscribeOptionsJson: ...
