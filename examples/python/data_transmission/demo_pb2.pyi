from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class Request(_message.Message):
    __slots__ = ["client_id", "request_data"]
    CLIENT_ID_FIELD_NUMBER: _ClassVar[int]
    REQUEST_DATA_FIELD_NUMBER: _ClassVar[int]
    client_id: int
    request_data: str
    def __init__(self, client_id: _Optional[int] = ..., request_data: _Optional[str] = ...) -> None: ...

class Response(_message.Message):
    __slots__ = ["response_data", "server_id"]
    RESPONSE_DATA_FIELD_NUMBER: _ClassVar[int]
    SERVER_ID_FIELD_NUMBER: _ClassVar[int]
    response_data: str
    server_id: int
    def __init__(self, server_id: _Optional[int] = ..., response_data: _Optional[str] = ...) -> None: ...
